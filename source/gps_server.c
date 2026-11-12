/*
 * gps_server.c  —  HTTPS GPS bridge for Nintendo 3DS
 *
 * Architecture overview:
 *   - gps_server_start() generates a fresh P-256 self-signed identity and
 *     independent private pairing token, then opens a TCP
 *     listen socket on port 8443, and spawns a single libctru thread.
 *
 *   - The thread loops doing a non-blocking accept() (50 ms sleep when
 *     nothing arrives).  For each accepted connection it does a TLS
 *     handshake (~200-500 ms on O3DS, <200 ms on N3DS), then dispatches
 *     the request:
 *       GET  /  or  /gps   →  serve the GPS capture HTML page
 *       POST /location      →  authenticate bearer token, parse and inject fix
 *     After the response the connection is closed.
 *
 *   - gps_server_stop() signals the thread via a flag, closes the listen
 *     socket to unblock any pending accept(), then joins the thread.
 *
 * Thread safety:
 *   gps_inject_fix() in gps.c writes two doubles and a bool.  The worst
 *   case is the main thread reads a partially-written fix once; for a
 *   position display that is acceptable.  A LightLock is used for
 *   correctness.
 */

#include "network.h"
#include <stdatomic.h>
#include <sys/select.h>
#include <math.h>
#include <jansson.h>
#include "gps_server.h"
#include "gps.h"
#include "gps_pairing.h"
#include <mbedtls/ecp.h>
#include <mbedtls/x509.h>
#include <mbedtls/sha256.h>
#include "logging.h"

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>

#include <3ds.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>    /* memalign — required on devkitARM/newlib */
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* =========================================================================
 * HTML page served to the phone browser
 * ========================================================================= */

static const char GPS_PAGE[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>3DS GPS</title>"
    "<style>"
    "body{font-family:sans-serif;padding:20px;background:#1a1a2e;color:#eee;max-width:480px}"
    "h2{color:#7ec8e3;margin-top:0}"
    ".box{padding:12px;background:#16213e;border-radius:8px;margin:10px 0;font-size:.9em;line-height:1.6}"
    ".ok{color:#6fcf97}.err{color:#eb5757}"
    "</style></head><body>"
    "<h2>3DS GPS Bridge</h2>"
    "<div class='box' id='s'>Requesting location permission...</div>"
    "<div class='box'>Keep this page open in the foreground.<br>"
    "Your browser may pause updates when locked or in the background.</div>"
    "<script>\n"
    "var token=location.hash.slice(1);history.replaceState(null,'','/');\n"
    "var n=0, busy=false, lastSend=0;\n"
    "function send(p){\n"
    "  var now=Date.now(),la=p.coords.latitude,lo=p.coords.longitude,ac=p.coords.accuracy;\n"
    "  var speed=p.coords.speed,heading=p.coords.heading;\n"
    "  if(busy || now-lastSend<1500)return;\n"
    "  busy=true;lastSend=now;\n"
    "  var status=document.getElementById('s');\n"
    "  status.textContent='Sending GPS position to 3DS...';\n"
    "  var controller=new AbortController(),timer=setTimeout(function(){controller.abort();},6000);\n"
    "  fetch('/location',{method:'POST',signal:controller.signal,\n"
    "    headers:{'Content-Type':'application/json','Authorization':'Bearer '+token},\n"
    "    body:JSON.stringify({lat:la,lon:lo,acc:ac,speed:speed,heading:heading})})\n"
    "   .then(function(r){if(!r.ok)throw Error('3DS returned HTTP '+r.status);\n"
    "     n++;status.textContent='Fix #'+n+' delivered'\n"
    "       +' (accuracy '+Math.round(ac)+'m)';status.className='box ok';})\n"
    "   .catch(function(e){status.textContent='GPS send failed: '+e.message;status.className='box err';})\n"
    "   .then(function(){clearTimeout(timer);busy=false;});\n"
    "}\n"
    "function onerr(e){\n"
    "  document.getElementById('s').innerHTML=\n"
    "    '<span class=\\'err\\'>GPS error: '+e.message+'</span>';}\n"
    "if(navigator.geolocation){\n"
    "  navigator.geolocation.watchPosition(send,onerr,\n"
    "    {enableHighAccuracy:true,maximumAge:2000,timeout:15000});\n"
    "}else{\n"
    "  document.getElementById('s').textContent='Geolocation not supported';}\n"
    "</script></body></html>\n";

/* =========================================================================
 * Module state
 * ========================================================================= */

#define SVR_STACK_SIZE  (128 * 1024)   /* 128 KB – generous for TLS ops      */
#define SVR_THREAD_PRIO  0x35          /* lower priority than main UI (0x30) */
#define SVR_SOC_BUF_SZ   0x100000     /* 1 MB SOC buffer (same as gps.c)    */
#define HDR_BUF_SIZE     2048          /* max HTTP request header size       */

static mbedtls_ssl_config       s_ssl_conf;
static mbedtls_x509_crt         s_cert;
static mbedtls_pk_context        s_pkey;
static mbedtls_entropy_context   s_entropy;
static mbedtls_ctr_drbg_context  s_ctr_drbg;
static bool                      s_ssl_ready     = false;
static int                       s_listen_fd     = -1;
static Thread                    s_thread        = NULL;
static _Atomic bool             s_thread_alive  = false;
static _Atomic bool             s_stop_flag     = false;
static char s_pair_token[33];
static char                      s_local_ip[32]  = {0};
static int                       s_port          = 0;
static u32                      *s_soc_buf       = NULL;
static _Atomic unsigned int     s_fix_count     = 0;

/* =========================================================================
 * BSD socket send/recv callbacks for mbedTLS
 * ========================================================================= */

typedef struct {
    int fd;
    u64 deadline;
} ClientSocket;

static bool wait_client(ClientSocket *client, bool writing) {
    while (!s_stop_flag && osGetTime() < client->deadline) {
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(client->fd, &sockets);
        struct timeval timeout = { .tv_usec = 100000 };
        int ready = select(client->fd + 1, writing ? NULL : &sockets,
                           writing ? &sockets : NULL, NULL, &timeout);
        if (ready > 0) return true;
        if (ready < 0 && errno != EINTR) return false;
    }
    return false;
}

static int my_net_send(void *ctx, const unsigned char *buf, size_t len) {
    ClientSocket *client = ctx;
    while (wait_client(client, true)) {
        int ret = (int)send(client->fd, buf, len, 0);
        if (ret >= 0) return ret;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
    }
    return MBEDTLS_ERR_SSL_TIMEOUT;
}

static int my_net_recv(void *ctx, unsigned char *buf, size_t len) {
    ClientSocket *client = ctx;
    while (wait_client(client, false)) {
        int ret = (int)recv(client->fd, buf, len, 0);
        if (ret >= 0) return ret;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
    }
    return MBEDTLS_ERR_SSL_TIMEOUT;
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* Write the entire string via mbedTLS — retries on WANT_WRITE. */
static bool ssl_send_all(mbedtls_ssl_context *ssl, const char *data, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = mbedtls_ssl_write(ssl, (const unsigned char *)(data + sent), len - sent);
        if (n > 0) {
            sent += n;
        } else {
            if (n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                svcSleepThread(1000000LL); /* 1 ms */
                continue;
            }
            return false;
        }
    }
    return true;
}

/*
 * Read HTTP headers byte-by-byte until "\r\n\r\n".
 * Returns total bytes in buf (including the terminal \r\n\r\n), or -1.
 */
static int read_headers(mbedtls_ssl_context *ssl, char *buf, int buf_size)
{
    int total = 0;
    while (total < buf_size - 1) {
        int n = mbedtls_ssl_read(ssl, (unsigned char *)(buf + total), 1);
        if (n <= 0) return -1;
        total++;
        buf[total] = '\0';
        if (total >= 4
            && buf[total - 4] == '\r' && buf[total - 3] == '\n'
            && buf[total - 2] == '\r' && buf[total - 1] == '\n')
            return total;
    }
    return -1; /* buffer full without finding end of headers */
}

/* Read exactly `len` bytes. Returns true on success. */
static bool read_exact(mbedtls_ssl_context *ssl, char *buf, int len)
{
    int got = 0;
    while (got < len) {
        int n = mbedtls_ssl_read(ssl, (unsigned char *)(buf + got), len - got);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

/*
 * Minimal JSON parser for {"lat":<float>,"lon":<float>,...}.
 * Does NOT require a full JSON library.
 */
static bool parse_location_json(const char *body, double *lat, double *lon, double *accuracy,
                                double *speed, double *heading) {
    json_t *root = json_loads(body, JSON_REJECT_DUPLICATES, NULL);
    json_t *y = json_object_get(root, "lat"), *x = json_object_get(root, "lon");
    json_t *acc = json_object_get(root, "acc");
    json_t *spd = json_object_get(root, "speed"), *dir = json_object_get(root, "heading");
    *lat = json_number_value(y);
    *lon = json_number_value(x);
    *accuracy = json_is_number(acc) ? json_number_value(acc) : -1;
    *speed = json_is_number(spd) ? json_number_value(spd) : NAN;
    *heading = json_is_number(dir) ? json_number_value(dir) : NAN;
    bool ok = json_is_number(y) && json_is_number(x) &&
              isfinite(*lat) && isfinite(*lon) && fabs(*lat) <= 90 && fabs(*lon) <= 180;
    json_decref(root);
    return ok;
}

/* =========================================================================
 * Per-connection handler
 * ========================================================================= */

static void handle_client(mbedtls_ssl_context *ssl)
{
    char hdr[HDR_BUF_SIZE];
    if (read_headers(ssl, hdr, sizeof(hdr)) < 0) return;

    /* --- Identify request ------------------------------------------------ */
    bool is_get    = (strncmp(hdr, "GET ",  4) == 0);
    bool is_post   = (strncmp(hdr, "POST ", 5) == 0);
    bool is_opt    = (strncmp(hdr, "OPTI",  4) == 0);

    bool want_page = is_get && (
        strstr(hdr, "GET / ")    != NULL ||
        strstr(hdr, "GET /gps ") != NULL ||
        strstr(hdr, "GET /\r")   != NULL );
    bool want_loc  = is_post && strncmp(hdr, "POST /location HTTP/1.", 21) == 0;
    if (want_loc && !gps_pairing_authorized(hdr, s_pair_token)) {
        static const char denied[]="HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        ssl_send_all(ssl,denied,sizeof(denied)-1);
        return;
    }

    /* --- Serve GPS capture page ----------------------------------------- */
    if (want_page) {
        int  page_len = (int)strlen(GPS_PAGE);
        char resp[256];
        int  resp_len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Cache-Control: no-cache, no-store\r\n"
            "Connection: close\r\n"
            "\r\n",
            page_len);
        ssl_send_all(ssl, resp, resp_len);
        ssl_send_all(ssl, GPS_PAGE, page_len);
        return;
    }

    /* --- Receive GPS fix ------------------------------------------------- */
    if (want_loc) {
        /* Find Content-Length header */
        const char *cl_hdr = strstr(hdr, "Content-Length:");
        if (!cl_hdr) cl_hdr = strstr(hdr, "content-length:");
        int body_len = 0;
        if (cl_hdr) {
            cl_hdr += 15; /* skip "Content-Length:" */
            while (*cl_hdr == ' ') cl_hdr++;
            body_len = atoi(cl_hdr);
        }

        bool accepted=false;
        if (body_len > 0 && body_len < 256) {
            char body[257];
            if (read_exact(ssl, body, body_len)) {
                body[body_len] = '\0';
                double lat, lon, accuracy, speed, heading;
                if (parse_location_json(body, &lat, &lon, &accuracy, &speed, &heading)) {
                    gps_inject_fix_with_motion(lat, lon, accuracy, speed, heading);
                    s_fix_count++;
                    accepted=true;
                }
            }
        }

        static const char ok_response[] =
            "HTTP/1.1 204 No Content\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        static const char invalid_response[] =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        if (accepted)
            ssl_send_all(ssl,ok_response,(int)sizeof(ok_response)-1);
        else
            ssl_send_all(ssl,invalid_response,(int)sizeof(invalid_response)-1);
        return;
    }

    /* --- CORS pre-flight (OPTIONS) --------------------------------------- */
    if (is_opt) {
        static const char options_response[] =
            "HTTP/1.1 204 No Content\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        ssl_send_all(ssl,options_response,(int)sizeof(options_response)-1);
        return;
    }

    /* --- Anything else → 404 -------------------------------------------- */
    static const char missing_response[] =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    ssl_send_all(ssl,missing_response,(int)sizeof(missing_response)-1);
}

/* =========================================================================
 * Background server thread
 * ========================================================================= */

static void server_thread_func(void *arg)
{
    (void)arg;

    while (!s_stop_flag) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(s_listen_fd,
                               (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Nothing ready yet — yield for 50 ms and try again */
                svcSleepThread(50000000LL);
                continue;
            }
            /* Any other error (including EBADF when we close s_listen_fd
             * in gps_server_stop) means we should exit. */
            break;
        }

        int cf = fcntl(client_fd, F_GETFL, 0);
        if (client_fd >= FD_SETSIZE || cf < 0 ||
            fcntl(client_fd, F_SETFL, cf | O_NONBLOCK) < 0) {
            close(client_fd);
            continue;
        }
        ClientSocket client = { client_fd, osGetTime() + 10000 };

        /* TLS handshake -------------------------------------------------- */
        mbedtls_ssl_context ssl;
        mbedtls_ssl_init(&ssl);

        if (mbedtls_ssl_setup(&ssl, &s_ssl_conf) != 0) {
            mbedtls_ssl_free(&ssl);
            close(client_fd);
            continue;
        }
        mbedtls_ssl_set_bio(&ssl, &client, my_net_send, my_net_recv, NULL);
        /*
         * Regression from v2.0: accepted sockets are now nonblocking, but
         * TLS handshakes must still be resumed after WANT_READ/WANT_WRITE.
         * Both the BIO callbacks and the enclosing deadline bound the wait.
         */
        int hs_result;
        do {
            hs_result = mbedtls_ssl_handshake(&ssl);
            if (hs_result == MBEDTLS_ERR_SSL_WANT_READ ||
                hs_result == MBEDTLS_ERR_SSL_WANT_WRITE)
                svcSleepThread(1000000LL); /* yield 1 ms */
        } while (!s_stop_flag && osGetTime() < client.deadline &&
                 (hs_result == MBEDTLS_ERR_SSL_WANT_READ ||
                  hs_result == MBEDTLS_ERR_SSL_WANT_WRITE));
        if (hs_result == 0) {
            client.deadline = osGetTime() + 5000;
            handle_client(&ssl);
        } else if (!s_stop_flag) {
            /* Never log the private pairing URL or its bearer token. */
            log_write("[GPS-SVR] TLS handshake failed: -0x%04X\n",
                      hs_result < 0 ? -hs_result : hs_result);
        }

        mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_free(&ssl);
        close(client_fd);
    }

    s_thread_alive = false;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

bool gps_server_start(int port)
{
    log_write("[GPS-SVR] gps_server_start(port=%d) called\n", port);

    if (s_thread_alive) {
        log_write("[GPS-SVR] already running, returning true\n");
        return true;
    }

    if (s_thread) gps_server_stop();
    s_fix_count    = 0;
    s_port         = port;
    s_stop_flag    = false;
    s_local_ip[0]  = '\0';

    /* ---- Initialise SOC service ---------------------------------------- */
    if (!network_socket_service_ready()) {
        s_soc_buf = (u32 *)memalign(0x1000, SVR_SOC_BUF_SZ);
        if (!s_soc_buf) return false;
        if (R_FAILED(socInit(s_soc_buf, SVR_SOC_BUF_SZ))) {
            free(s_soc_buf);
            s_soc_buf = NULL;
            return false;
        }
    }

    /* ---- Determine local WiFi IP --------------------------------------- */
    u32 raw_ip = (u32)gethostid(); /* network-byte-order IP */
    unsigned char *ip_b = (unsigned char *)&raw_ip;
    snprintf(s_local_ip, sizeof(s_local_ip),
             "%u.%u.%u.%u", ip_b[0], ip_b[1], ip_b[2], ip_b[3]);
    log_write("[GPS-SVR] local IP = %s\n", s_local_ip);

    /* ---- Build mbedTLS context ----------------------------------------- */

    mbedtls_ssl_config_init(&s_ssl_conf);
    mbedtls_x509_crt_init(&s_cert);
    mbedtls_pk_init(&s_pkey);
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_ctr_drbg);

    /* Seed the DRBG — mbedTLS on 3DS uses sslcGenerateRandomData as
     * hardware entropy source (MBEDTLS_ENTROPY_HARDWARE_ALT). */
    int ret_tls;
    ret_tls = mbedtls_ctr_drbg_seed(&s_ctr_drbg, mbedtls_entropy_func,
                                     &s_entropy,
                                     (const unsigned char *)"3ds_gps", 7);
    if (ret_tls != 0) {
        log_write("[GPS-SVR] ctr_drbg_seed FAILED: -0x%04X\n", -ret_tls);
        goto fail_ssl;
    }
    log_write("[GPS-SVR] DRBG seeded OK\n");

    /* A fresh P-256 identity per session replaces the public embedded key.
     * Browser trust remains manual for this local self-signed certificate. */
    unsigned char entropy[16], der[2048];
    if (mbedtls_ctr_drbg_random(&s_ctr_drbg, entropy, sizeof(entropy)) != 0)
        goto fail_ssl;
    gps_pairing_token(entropy, s_pair_token);
    /* Certificate serials are public: never reuse pairing-secret entropy. */
    if (mbedtls_ctr_drbg_random(&s_ctr_drbg, entropy, sizeof(entropy)) != 0)
        goto fail_ssl;
    entropy[0] &= 0x7f;
    entropy[15] |= 1;
    if (mbedtls_pk_setup(&s_pkey, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(s_pkey),
                           mbedtls_ctr_drbg_random, &s_ctr_drbg) != 0) goto fail_ssl;

    /*
     * Correct X.509 v3 leaf certificate identity and usage for local HTTPS.
     * SAN (2.5.29.17) must contain the exact IP in the pairing QR URL:
     * DER SEQUENCE { context-specific 7, four-byte IPv4 address }.
     * EKU (2.5.29.37) must specify serverAuth.
     *
     * Self-signing still DOES NOT make the certificate trusted by Safari;
     * users must not be promised no-warning pairing on stock iOS.
     */
    struct in_addr ip_addr;
    if (!inet_aton(s_local_ip, &ip_addr)) goto fail_ssl;
    unsigned char san_der[] = {0x30, 0x06, 0x87, 0x04, 0, 0, 0, 0};
    memcpy(san_der + 4, &ip_addr.s_addr, 4);
    static const unsigned char server_eku_der[] = {
        0x30, 0x0a, 0x06, 0x08, 0x2b, 0x06,
        0x01, 0x05, 0x05, 0x07, 0x03, 0x01
    };

    /* Keep certificate life under Apple's 825-day limit, and reject an
     * incorrect system clock instead of generating an invalid certificate. */
    time_t now = time(NULL);
    if (now < (time_t)1767225600) {
        log_write("[GPS-SVR] system clock is invalid; set 3DS date/time\n");
        goto fail_ssl;
    }
    time_t not_before_time = now - (time_t)(24 * 60 * 60);
    time_t not_after_time = now + (time_t)(365 * 24 * 60 * 60);
    char not_before[16], not_after[16];
    struct tm *t = gmtime(&not_before_time);
    if (!t || strftime(not_before, sizeof(not_before), "%Y%m%d%H%M%S", t) != 14)
        goto fail_ssl;
    t = gmtime(&not_after_time);
    if (!t || strftime(not_after, sizeof(not_after), "%Y%m%d%H%M%S", t) != 14)
        goto fail_ssl;

    mbedtls_x509write_cert writer;
    mbedtls_mpi serial;
    mbedtls_x509write_crt_init(&writer);
    mbedtls_mpi_init(&serial);
    mbedtls_mpi_read_binary(&serial, entropy, sizeof(entropy));
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &s_pkey);
    mbedtls_x509write_crt_set_issuer_key(&writer, &s_pkey);
    int cert_ok =
        mbedtls_x509write_crt_set_serial(&writer, &serial) == 0 &&
        mbedtls_x509write_crt_set_subject_name(&writer, "CN=3DS GPS local pairing") == 0 &&
        mbedtls_x509write_crt_set_issuer_name(&writer, "CN=3DS GPS local pairing") == 0 &&
        mbedtls_x509write_crt_set_validity(&writer, not_before, not_after) == 0 &&
        mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1) == 0 &&
        mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0 &&
        mbedtls_x509write_crt_set_extension(
            &writer, "\x55\x1d\x11", 3, 0, san_der, sizeof(san_der)) == 0 &&
        mbedtls_x509write_crt_set_extension(
            &writer, "\x55\x1d\x25", 3, 0, server_eku_der, sizeof(server_eku_der)) == 0;
    int der_len = cert_ok ? mbedtls_x509write_crt_der(&writer, der, sizeof(der),
                              mbedtls_ctr_drbg_random, &s_ctr_drbg) : -1;
    mbedtls_x509write_crt_free(&writer);
    mbedtls_mpi_free(&serial);
    if (der_len <= 0 || mbedtls_x509_crt_parse_der(&s_cert,
                    der + sizeof(der) - der_len, der_len) != 0) goto fail_ssl;

    /* Configure as TLS server */
    ret_tls = mbedtls_ssl_config_defaults(&s_ssl_conf,
                                           MBEDTLS_SSL_IS_SERVER,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret_tls != 0) {
        log_write("[GPS-SVR] ssl_config_defaults FAILED: -0x%04X\n", -ret_tls);
        goto fail_ssl;
    }

    mbedtls_ssl_conf_rng(&s_ssl_conf, mbedtls_ctr_drbg_random, &s_ctr_drbg);
    mbedtls_ssl_conf_own_cert(&s_ssl_conf, &s_cert, &s_pkey);
    mbedtls_ssl_conf_authmode(&s_ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
    /* Require at least TLS 1.2 */
    mbedtls_ssl_conf_min_version(&s_ssl_conf,
                                  MBEDTLS_SSL_MAJOR_VERSION_3,
                                  MBEDTLS_SSL_MINOR_VERSION_3);

    s_ssl_ready = true;
    log_write("[GPS-SVR] TLS config ready\n");

    /* ---- Create listen socket ------------------------------------------ */
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        log_write("[GPS-SVR] socket() failed: %d\n", errno);
        goto fail_ssl;
    }
    log_write("[GPS-SVR] listen socket fd=%d\n", s_listen_fd);

    {
        int optval = 1;
        setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &optval, sizeof(optval));
    }

    /* Non-blocking so accept() yields immediately when nothing is pending */
    {
        int flags = fcntl(s_listen_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(s_listen_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            goto fail_sock;
    }

    {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons((u16)port);

        if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            log_write("[GPS-SVR] bind() failed: %d\n", errno);
            goto fail_sock;
        }
        log_write("[GPS-SVR] bind OK on port %d\n", port);
        if (listen(s_listen_fd, 4) < 0) {
            log_write("[GPS-SVR] listen() failed: %d\n", errno);
            goto fail_sock;
        }
        log_write("[GPS-SVR] listen OK\n");
    }

    /* ---- Spawn background thread --------------------------------------- */
    s_thread_alive = true;
    s_thread = threadCreate(server_thread_func, NULL,
                            SVR_STACK_SIZE,
                            SVR_THREAD_PRIO,
                            -2,    /* any available core */
                            false  /* joinable */);
    if (!s_thread) {
        log_write("[GPS-SVR] threadCreate FAILED\n");
        s_thread_alive = false;
        goto fail_sock;
    }

    log_write("[GPS-SVR] server thread spawned — listening on https://%s:%d\n", s_local_ip, port);
    return true;

fail_sock:
    close(s_listen_fd);
    s_listen_fd = -1;
fail_ssl:
    mbedtls_ssl_config_free(&s_ssl_conf);
    mbedtls_x509_crt_free(&s_cert);
    mbedtls_pk_free(&s_pkey);
    mbedtls_ctr_drbg_free(&s_ctr_drbg);
    mbedtls_entropy_free(&s_entropy);
    s_ssl_ready = false;
    if (s_soc_buf) socExit();
    free(s_soc_buf);
    s_soc_buf = NULL;
    return false;
}

void gps_server_stop(void)
{
    if (!s_thread) return;

    /* Signal the thread to exit */
    s_stop_flag = true;

    /* Wait for the thread to finish cleanly */
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }

    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }

    s_thread_alive = false;
    s_stop_flag    = false;

    if (s_ssl_ready) {
        mbedtls_ssl_config_free(&s_ssl_conf);
        mbedtls_x509_crt_free(&s_cert);
        mbedtls_pk_free(&s_pkey);
        mbedtls_ctr_drbg_free(&s_ctr_drbg);
        mbedtls_entropy_free(&s_entropy);
        s_ssl_ready = false;
    }

    if (s_soc_buf) {
        socExit();
        free(s_soc_buf);
        s_soc_buf = NULL;
    }

    memset(s_pair_token, 0, sizeof(s_pair_token));
    s_local_ip[0] = '\0';
    s_fix_count   = 0;
}

bool gps_server_is_running(void)
{
    return s_thread_alive;
}

void gps_server_update(void)
{
    /* No-op — the server is fully driven by its background thread. */
}

void gps_server_get_url(char *buf, size_t len)
{
    if (!buf || len == 0) return;

    if (!s_thread_alive || s_local_ip[0] == '\0') {
        buf[0] = '\0';
        return;
    }
    snprintf(buf, len, "https://%s:%d/#%s", s_local_ip, s_port, s_pair_token);
}

unsigned int gps_server_get_fix_count(void)
{
    return s_fix_count;
}

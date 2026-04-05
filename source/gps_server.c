/*
 * gps_server.c  —  HTTPS GPS bridge for Nintendo 3DS
 *
 * Architecture overview:
 *   - gps_server_start() creates an mbedTLS ssl_config with an embedded
 *     self-signed cert (from include/gps_server_cert.h), opens a TCP
 *     listen socket on port 8443, and spawns a single libctru thread.
 *
 *   - The thread loops doing a non-blocking accept() (50 ms sleep when
 *     nothing arrives).  For each accepted connection it does a TLS
 *     handshake (~200-500 ms on O3DS, <200 ms on N3DS), then dispatches
 *     the request:
 *       GET  /  or  /gps   →  serve the GPS capture HTML page
 *       POST /location      →  parse {"lat":...,"lon":...}, call gps_inject_fix()
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

#include "gps_server.h"
#include "gps.h"
#include "gps_server_cert.h"   /* embedded cert + key as C arrays */
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
    "Screen can lock but the browser tab must stay active.</div>"
    "<script>\n"
    "var n=0;\n"
    "function send(p){\n"
    "  var la=p.coords.latitude,lo=p.coords.longitude,ac=p.coords.accuracy;\n"
    "  n++;\n"
    "  fetch('/location',{method:'POST',\n"
    "    headers:{'Content-Type':'application/json'},\n"
    "    body:JSON.stringify({lat:la,lon:lo,acc:ac})}).catch(function(){});\n"
    "  document.getElementById('s').innerHTML=\n"
    "    '<span class=\\'ok\\'>Fix #'+n+'</span><br>'\n"
    "    +la.toFixed(6)+', '+lo.toFixed(6)\n"
    "    +'<br><small>\\xb1'+Math.round(ac)+'m accuracy</small>';\n"
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
static volatile bool             s_thread_alive  = false;
static volatile bool             s_stop_flag     = false;
static char                      s_local_ip[32]  = {0};
static int                       s_port          = 0;
static u32                      *s_soc_buf       = NULL;
static volatile unsigned int     s_fix_count     = 0;

/* =========================================================================
 * BSD socket send/recv callbacks for mbedTLS
 * ========================================================================= */

static int my_net_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int ret = (int)send(fd, buf, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return -1;
    }
    return ret;
}

static int my_net_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int ret = (int)recv(fd, buf, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MBEDTLS_ERR_SSL_WANT_READ;
        return -1;
    }
    return ret;
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
static bool parse_location_json(const char *body, double *lat, double *lon)
{
    if (!body || !lat || !lon) return false;

    const char *p;
    char *end;

    /* lat */
    p = strstr(body, "\"lat\"");
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    *lat = strtod(p, &end);
    if (end == p) return false;

    /* lon */
    p = strstr(body, "\"lon\"");
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    *lon = strtod(p, &end);
    if (end == p) return false;

    /* sanity bounds */
    if (*lat < -90.0  || *lat > 90.0)  return false;
    if (*lon < -180.0 || *lon > 180.0) return false;

    return true;
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
    bool want_loc  = is_post && strstr(hdr, "POST /location") != NULL;

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

        if (body_len > 0 && body_len < 256) {
            char body[257];
            if (read_exact(ssl, body, body_len)) {
                body[body_len] = '\0';
                double lat = 0.0, lon = 0.0;
                if (parse_location_json(body, &lat, &lon)) {
                    gps_inject_fix(lat, lon);
                    s_fix_count++;
                }
            }
        }

        ssl_send_all(ssl,
            "HTTP/1.1 204 No Content\r\n"
            "Connection: close\r\n"
            "\r\n", 38);
        return;
    }

    /* --- CORS pre-flight (OPTIONS) --------------------------------------- */
    if (is_opt) {
        ssl_send_all(ssl,
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Connection: close\r\n"
            "\r\n", 130);
        return;
    }

    /* --- Anything else → 404 -------------------------------------------- */
    ssl_send_all(ssl,
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "\r\n", 40);
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

        /*
         * Make the client socket blocking so mbedtls_ssl_read/write work
         * without WANT_READ loops in handle_client.
         */
        int cf = fcntl(client_fd, F_GETFL, 0);
        if (cf >= 0) fcntl(client_fd, F_SETFL, cf & ~O_NONBLOCK);

        /* TLS handshake -------------------------------------------------- */
        mbedtls_ssl_context ssl;
        mbedtls_ssl_init(&ssl);

        if (mbedtls_ssl_setup(&ssl, &s_ssl_conf) != 0) {
            mbedtls_ssl_free(&ssl);
            close(client_fd);
            continue;
        }
        mbedtls_ssl_set_bio(&ssl, &client_fd, my_net_send, my_net_recv, NULL);

        /*
         * mbedtls_ssl_handshake() may need to be retried if the underlying
         * socket signals WANT_READ / WANT_WRITE.  We retry up to 1000 times
         * with 10 ms pauses (10 seconds total max).
         *
         * On an O3DS (268 MHz ARM11) the RSA-2048 signature step takes
         * approximately 200-500 ms; on an N3DS (804 MHz) under 200 ms.
         * The phone typically waits patiently through the delay.
         */
        int hs_result;
        int retries = 0;
        do {
            hs_result = mbedtls_ssl_handshake(&ssl);
            if (hs_result == MBEDTLS_ERR_SSL_WANT_READ ||
                hs_result == MBEDTLS_ERR_SSL_WANT_WRITE) {
                svcSleepThread(10000000LL); /* 10 ms */
                if (++retries > 1000) break;
            }
        } while (hs_result == MBEDTLS_ERR_SSL_WANT_READ ||
                 hs_result == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (hs_result == 0) {
            handle_client(&ssl);
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

    s_fix_count    = 0;
    s_port         = port;
    s_stop_flag    = false;
    s_local_ip[0]  = '\0';

    /* ---- Initialise SOC service ---------------------------------------- */
    if (!s_soc_buf) {
        s_soc_buf = (u32 *)memalign(0x1000, SVR_SOC_BUF_SZ);
        if (!s_soc_buf) {
            log_write("[GPS-SVR] memalign for SOC buffer failed\n");
            return false;
        }

        Result ret = socInit(s_soc_buf, SVR_SOC_BUF_SZ);
        if (R_FAILED(ret)) {
            /* Already initialised by network module — that's fine,
               BSD sockets are usable; just free our duplicate buffer. */
            log_write("[GPS-SVR] socInit returned 0x%08lX (already init by network — OK)\n", ret);
            free(s_soc_buf);
            s_soc_buf = NULL;
        } else {
            log_write("[GPS-SVR] socInit OK\n");
        }
    } else {
        log_write("[GPS-SVR] SOC buffer already allocated, skipping socInit\n");
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

    /* Parse embedded DER certificate */
    ret_tls = mbedtls_x509_crt_parse_der(&s_cert,
                                          gps_server_cert_der,
                                          gps_server_cert_der_len);
    if (ret_tls != 0) {
        log_write("[GPS-SVR] cert parse FAILED: -0x%04X\n", -ret_tls);
        goto fail_ssl;
    }
    log_write("[GPS-SVR] cert parsed OK (len=%zu)\n", gps_server_cert_der_len);

    /* Parse embedded PKCS#1 DER private key */
    ret_tls = mbedtls_pk_parse_key(&s_pkey,
                                    gps_server_key_der,
                                    gps_server_key_der_len,
                                    NULL, 0);
    if (ret_tls != 0) {
        log_write("[GPS-SVR] pk_parse_key FAILED: -0x%04X\n", -ret_tls);
        goto fail_ssl;
    }
    log_write("[GPS-SVR] private key parsed OK\n");

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
        if (flags >= 0) fcntl(s_listen_fd, F_SETFL, flags | O_NONBLOCK);
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
    socExit();
    free(s_soc_buf);
    s_soc_buf = NULL;
    return false;
}

void gps_server_stop(void)
{
    if (!s_thread_alive) return;

    /* Signal the thread to exit */
    s_stop_flag = true;

    /* Close the listen socket — this causes accept() to return an error,
     * waking the thread immediately rather than waiting up to 50 ms. */
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }

    /* Wait for the thread to finish cleanly */
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
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
    snprintf(buf, len, "https://%s:%d", s_local_ip, s_port);
}

unsigned int gps_server_get_fix_count(void)
{
    return s_fix_count;
}

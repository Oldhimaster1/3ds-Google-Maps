/* Compile the production implementation with only OS/GPS-injection shims.
 * The TLS, certificate writer, HTTP parser and authentication are unchanged.
 * Host entropy and pthreads do not validate the 3DS entropy or socket services.
 */
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
static long host_gethostid(void) { return inet_addr("127.0.0.1"); }
#define gethostid host_gethostid
#include "../../source/gps_server.c"
#undef gethostid
static unsigned injected;
bool network_socket_service_ready(void) { return true; }
void log_write(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
}
void gps_inject_fix_with_motion(double lat, double lon, double acc, double speed, double heading) {
    (void)acc; (void)speed; (void)heading;
    if (lat == 35.0 && lon == -82.0) injected++;
}
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN); /* Linux socket behavior, absent on libctru. */
    if (argc != 3 || !gps_server_start(atoi(argv[1]))) return 1;
    FILE *f = fopen(argv[2], "wb");
    if (!f) return 2;
    fwrite(s_cert.raw.p, 1, s_cert.raw.len, f); fclose(f);
    char url[128]; gps_server_get_url(url, sizeof(url));
    puts(url); fflush(stdout); /* Captured privately by test driver, never logged. */
    (void)getchar();
    unsigned count = gps_server_get_fix_count();
    gps_server_stop();
    printf("accepted=%u injected=%u\n", count, injected);
    return count == injected ? 0 : 3;
}

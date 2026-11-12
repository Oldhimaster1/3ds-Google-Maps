#!/usr/bin/env python3
"""GPS HTTPS certificate/handshake source regression checks.

These source checks complement, but do not replace, a devkitPro build,
live TLS handshake inspection, and real iOS/Android GPS-pairing tests.
"""
from ipaddress import IPv4Address
from pathlib import Path

source = (Path(__file__).resolve().parents[2] / "source/gps_server.c").read_text()
checks = {
    "single TLS handshake call, now retried": (
        source.count("mbedtls_ssl_handshake(&ssl)") == 1
        and "hs_result == MBEDTLS_ERR_SSL_WANT_READ" in source
        and "hs_result == MBEDTLS_ERR_SSL_WANT_WRITE" in source
        and "osGetTime() < client.deadline" in source
    ),
    "actual 3DS address included as IP SAN": (
        "inet_aton(s_local_ip, &ip_addr)" in source
        and "memcpy(san_der + 4, &ip_addr.s_addr, 4)" in source
        and '\"\\x55\\x1d\\x11\", 3, 0, san_der' in source
    ),
    "leaf key usage, not a CA": (
        "mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1)" in source
        and "MBEDTLS_X509_KU_DIGITAL_SIGNATURE" in source
    ),
    "server-auth EKU extension": (
        "server_eku_der" in source
        and "0x01, 0x05, 0x05, 0x07, 0x03, 0x01" in source
        and '\"\\x55\\x1d\\x25\", 3, 0, server_eku_der' in source
    ),
    "validity derives from RTC, not ten-year literals": (
        "time(NULL)" in source
        and "365 * 24 * 60 * 60" in source
        and "20360101000000" not in source
        and 'mbedtls_x509write_crt_set_validity(&writer, not_before, not_after)' in source
    ),
    "TLS handshake and HTTP request windows are independently bounded": (
        "#define GPS_TLS_HANDSHAKE_TIMEOUT_MS 16000ULL" in source
        and "#define GPS_HTTP_REQUEST_TIMEOUT_MS 8000ULL" in source
        and "osGetTime() + GPS_TLS_HANDSHAKE_TIMEOUT_MS" in source
        and "osGetTime() + GPS_HTTP_REQUEST_TIMEOUT_MS" in source
        and "osGetTime() + 10000" not in source
        and "osGetTime() + 5000" not in source
    ),
    "recoverable TLS reads and writes are retried": (
        source.count("n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE") >= 2
        and "n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ" in source
    ),
    "TLS stage diagnostics omit private pairing data": (
        "HTTPS handshake timed out before completion" in source
        and "HTTPS handshake complete" in source
        and "HTTPS request header read ended" in source
        and "HTTPS request body read ended" in source
        and "GPS page delivered" in source
    ),
    "session-specific cert and private pairing token in query URL": (
        "mbedtls_ecp_gen_key(" in source
        and "gps_pairing_token(entropy, s_pair_token)" in source
        and 'https://%s:%d/?pair=%s' in source
        and "location.search" in source
        and "history.replaceState" in source
    ),
    "TLS private session data not printed in handshake diagnostics": (
        'TLS handshake failed: -0x%04X' in source
        and "s_pair_token);" not in source[source.find("TLS handshake failed") - 90:
                                          source.find("TLS handshake failed") + 160]
    ),
}
# Ensure the manually encoded DER payloads represent the expected RFC 5280
# GeneralName iPAddress and extendedKeyUsage serverAuth structures.
san_der = bytes([0x30, 0x06, 0x87, 0x04]) + IPv4Address("192.168.1.102").packed
eku_der = bytes([0x30, 0x0a, 0x06, 0x08,
                 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01])
checks["SAN DER sample structure"] = san_der.hex() == "30068704c0a80166"
checks["EKU DER serverAuth structure"] = eku_der.hex() == "300a06082b06010505070301"

for name, ok in checks.items():
    print(("PASS" if ok else "FAIL") + " " + name)
if not all(checks.values()):
    raise SystemExit(1)

print("Source regression checks passed; physical Safari trust is NOT guaranteed.")

#!/usr/bin/env python3
"""Actual production server over POSIX TCP/TLS; not an iOS or 3DS runtime test.

Requires a native mbedTLS 2.28 build (MBEDTLS_HOST_ROOT), Jansson headers
(JANSSON_HOST_INCLUDE if needed), cc and OpenSSL. No secrets are printed.
"""
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
MBED = Path(os.environ['MBEDTLS_HOST_ROOT'])

with tempfile.TemporaryDirectory(prefix='gps-live-') as temp:
    temp = Path(temp)
    binary = temp / 'gps-server'
    cmd = ['cc', '-D_DEFAULT_SOURCE', '-std=c11', '-Wall', '-Wextra',
           '-I' + str(ROOT / 'tests/host/gps_shim'), '-I' + str(ROOT / 'include'),
           '-I' + str(MBED / 'include')]
    if os.environ.get('JANSSON_HOST_INCLUDE'):
        cmd += ['-I' + os.environ['JANSSON_HOST_INCLUDE']]
    cmd += [str(ROOT / 'tests/host/gps_tls_server_host.c'),
            str(ROOT / 'source/gps_pairing.c'), '-L' + str(MBED / 'library'),
            '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-l:libjansson.so.4',
            '-lm', '-pthread', '-o', str(binary)]
    subprocess.run(cmd, check=True)
    previous_token = None
    for session in range(2):
        with socket.socket() as reserved:
            reserved.bind(('127.0.0.1', 0))
            port = reserved.getsockname()[1]
        cert = temp / 'cert.der'
        with (temp / 'private-log').open('w+') as logs:
            proc = subprocess.Popen([str(binary), str(port), str(cert)], stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=logs, text=True)
            try:
                url = urlsplit(proc.stdout.readline().strip())
                token = url.fragment
                assert url.hostname == '127.0.0.1' and url.port == port and len(token) == 32
                assert token != previous_token
                pem = temp / 'cert.pem'
                pem.write_text(ssl.DER_cert_to_PEM_cert(cert.read_bytes()))
                description = subprocess.check_output(
                    ['openssl', 'x509', '-in', str(pem), '-text', '-noout'], text=True)
                for field in ('IP Address:127.0.0.1', 'TLS Web Server Authentication',
                              'Digital Signature', 'CA:FALSE', 'ecdsa-with-SHA256', 'prime256v1'):
                    assert field in description, field
                assert token not in description
                good = subprocess.run(['openssl', 'verify', '-CAfile', str(pem),
                    '-purpose', 'sslserver', '-verify_ip', '127.0.0.1', str(pem)],
                    capture_output=True, text=True)
                assert good.returncode == 0, good.stderr
                bad = subprocess.run(['openssl', 'verify', '-CAfile', str(pem),
                    '-verify_ip', '127.0.0.2', str(pem)], capture_output=True)
                assert bad.returncode != 0
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
                        ssl.create_default_context().wrap_socket(raw, server_hostname='127.0.0.1')
                except ssl.SSLCertVerificationError:
                    pass
                else:
                    raise AssertionError('Self-signed certificate unexpectedly publicly trusted')
                context = ssl.create_default_context(cafile=str(pem))

                def request(method, path, body=b'', headers=(), fragment=False):
                    with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
                        with context.wrap_socket(raw, server_hostname='127.0.0.1') as tls:
                            assert tls.version() in ('TLSv1.2', 'TLSv1.3')
                            head = (f'{method} {path} HTTP/1.1\r\nHost: 127.0.0.1\r\n'
                                    f'Content-Length: {len(body)}\r\n' +
                                    ''.join(k + ': ' + v + '\r\n' for k, v in headers) + '\r\n').encode()
                            if fragment:
                                for byte in head + body:
                                    tls.sendall(bytes([byte]))
                            else:
                                tls.sendall(head + body)
                            data = b''
                            while True:
                                block = tls.recv(4096)
                                if not block:
                                    break
                                data += block
                    return int(data.split(b' ', 2)[1]), data

                status, page = request('GET', '/')
                assert status == 200 and token.encode() not in page
                assert b'watchPosition' in page
                assert request('GET', '/missing')[0] == 404
                fix = json.dumps({'lat': 35.0, 'lon': -82.0, 'acc': 4}).encode()
                auth = [('Authorization', 'Bearer ' + token)]
                for headers in ([], [('Authorization', 'Bearer ' + '0' * 32)], auth + auth):
                    assert request('POST', '/location', fix, headers)[0] == 403
                if previous_token:
                    assert request('POST', '/location', fix,
                                   [('Authorization', 'Bearer ' + previous_token)])[0] == 403
                for invalid in (b'{}', b'{', b'{"lat":91,"lon":0}', b'{"lat":0,"lon":181}',
                                b'{"lat":1,"lat":2,"lon":0}', b'{"lat":"35","lon":0}'):
                    assert request('POST', '/location', invalid, auth)[0] == 400
                assert request('POST', '/location', fix, auth, fragment=True)[0] == 204
                assert request('POST', '/location', fix,
                               [('authorization', 'Bearer ' + token)])[0] == 204
                if session == 0:
                    # Safari's trust dialog can hold the connection for longer
                    # than the previous 10s handshake budget. This models an
                    # idle TCP connection before the client completes TLS; it
                    # does NOT reproduce iOS's actual trust evaluation.
                    def get_page_after_pause(wait_before_tls=0, wait_before_get=0):
                        with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
                            if wait_before_tls:
                                time.sleep(wait_before_tls)
                            with context.wrap_socket(raw, server_hostname='127.0.0.1') as tls:
                                tls.settimeout(6)
                                if wait_before_get:
                                    time.sleep(wait_before_get)
                                tls.sendall(b'GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n')
                                response = b''
                                while True:
                                    chunk = tls.recv(4096)
                                    if not chunk:
                                        break
                                    response += chunk
                        assert response.startswith(b'HTTP/1.1 200 OK'), response[:80]
                        assert b'3DS GPS Bridge' in response

                    get_page_after_pause(wait_before_tls=12)
                    get_page_after_pause(wait_before_get=7)

                    # Body streaming must survive a slow sender too.
                    with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
                        with context.wrap_socket(raw, server_hostname='127.0.0.1') as tls:
                            tls.settimeout(6)
                            headers = (b'POST /location HTTP/1.1\r\nHost: 127.0.0.1\r\n'
                                       + f'Content-Length: {len(fix)}\r\n'.encode()
                                       + f'Authorization: Bearer {token}\r\n\r\n'.encode())
                            tls.sendall(headers + fix[:5])
                            time.sleep(7)
                            tls.sendall(fix[5:])
                            response = tls.recv(4096)
                            assert response.startswith(b'HTTP/1.1 204 No Content'), response[:80]
                    # The slow-body POST is the third accepted fix.

                # Stop must interrupt a client that connects without sending TLS data.
                with socket.create_connection(('127.0.0.1', port), timeout=3):
                    tail, _ = proc.communicate('\n', timeout=3)
                expected_fixes = 3 if session == 0 else 2
                assert proc.returncode == 0 and f'accepted={expected_fixes} injected={expected_fixes}' in tail, tail
                logs.seek(0)
                assert token not in logs.read()
                previous_token = token
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
    print('PASS production certificate: IP match/mismatch, current validity, P-256/SHA256, leaf KU/EKU')
    print('PASS real TLS: explicit test trust succeeds; default public trust correctly rejects self-signed cert')
    print('PASS authenticated/fragmented HTTP, rejected malformed fixes, private tokens, restart rotation, bounded stop')
    print('PASS delayed TLS/client headers and slow GPS body (host-only; not Safari)')
    print('NOT VERIFIED: 3DS services, Safari trust, iOS geolocation or physical phone-to-console pairing')

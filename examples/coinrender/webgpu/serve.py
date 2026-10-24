#!/usr/bin/env python3
"""Serve only the built WebGPU artifacts over TLS, including on a LAN."""
import argparse
import json
import re
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
from urllib.parse import unquote, urlsplit


class ArtifactHandler(SimpleHTTPRequestHandler):
    required = {'index.html', 'app.js', 'coin_capture.js', 'coin_capture.wasm',
               'renderer.js', 'renderer_bg.wasm'}
    allowed = required | {'city-million.bin', 'city-40000.bin'}

    def do_GET(self):
        if not self.artifact_request():
            self.send_error(404)
            return
        super().do_GET()

    def do_HEAD(self):
        if not self.artifact_request():
            self.send_error(404)
            return
        super().do_HEAD()

    def artifact_request(self):
        path = unquote(urlsplit(self.path).path)
        if path == '/':
            path = '/index.html'
        if path.lstrip('/') not in self.allowed or path.count('/') != 1:
            return False
        target = Path(self.directory) / path[1:]
        return not target.is_symlink() and target.resolve().parent == Path(self.directory).resolve()

    def do_POST(self):
        if not getattr(self.server, 'benchmark_reports', False) or self.path != '/benchmark-report':
            self.send_error(404)
            return
        origin = self.headers.get('Origin', '')
        expected = 'http://' + self.headers.get('Host', '')
        if len(self.headers.get_all('Host', [])) != 1 or len(self.headers.get_all('Origin', [])) != 1 or origin.lower() != expected.lower():
            self.send_error(403, 'Origin must match Host')
            return
        length = self.headers.get('Content-Length', '')
        if self.headers.get('Transfer-Encoding') or len(self.headers.get_all('Content-Length', [])) != 1 or not re.fullmatch(r'[0-9]+', length):
            self.send_error(411, 'A bounded Content-Length is required')
            return
        if len(length) > 10:
            self.send_error(413)
            return
        size = int(length)
        if size < 1 or size > 65536:
            self.send_error(413)
            return
        if self.headers.get('Content-Type', '').split(';', 1)[0].strip().lower() != 'application/json':
            self.send_error(415)
            return
        def invalid_constant(value):
            raise ValueError('Nonfinite JSON number: ' + value)
        try:
            self.connection.settimeout(10)
            body = self.rfile.read(size)
            if len(body) != size:
                raise ValueError('Truncated report')
            value = json.loads(body.decode('utf-8'), parse_constant=invalid_constant)
            if not isinstance(value, dict):
                raise ValueError('Report must be an object')
            payload = json.dumps(value, separators=(',', ':'), allow_nan=False, ensure_ascii=False)
            if len(payload.encode('utf-8')) > 65536:
                raise ValueError('Expanded report exceeds limit')
        except (ValueError, OSError, RecursionError):
            self.send_error(400, 'Invalid bounded JSON object')
            return
        print('BENCHMARK_REPORT ' + payload, flush=True)
        ack = b'{"accepted":true}'
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(ack)))
        self.end_headers()
        self.wfile.write(ack)

    def list_directory(self, _path):
        self.send_error(403, 'Directory listing disabled')
        return None

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--cert', type=Path)
    parser.add_argument('--key', type=Path)
    parser.add_argument('--loopback-http', action='store_true', help='Plain HTTP only on 127.0.0.1 for adb reverse/localhost secure context')
    parser.add_argument('--bind', default='127.0.0.1', help='Use 0.0.0.0 to allow other LAN devices')
    parser.add_argument('--port', type=int, default=8443)
    parser.add_argument('--benchmark-reports', action='store_true', help='Local opt-in POST metrics to stdout; requires --loopback-http')
    args = parser.parse_args()
    if args.benchmark_reports and not args.loopback_http:
        parser.error('--benchmark-reports requires --loopback-http on 127.0.0.1')
    if args.loopback_http:
        if args.bind != '127.0.0.1':
            parser.error('--loopback-http requires --bind 127.0.0.1; public/nonloopback binding is forbidden')
        if args.cert or args.key:
            parser.error('--loopback-http does not use TLS keys; omit --cert/--key')
    elif not args.cert or not args.key:
        parser.error('HTTPS requires both --cert and --key')
    directory = args.directory.resolve()
    if not all((directory / name).is_file() and not (directory / name).is_symlink()
               for name in ArtifactHandler.required):
        parser.error('--directory must contain the built demonstration artifacts (no symlinks)')
    context = None
    if not args.loopback_http:
        if any(path.resolve().is_relative_to(directory) for path in (args.cert, args.key)):
            parser.error('TLS certificate and private key must be outside the served artifact directory')
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(args.cert, args.key)
    handler = partial(ArtifactHandler, directory=str(directory))
    with ThreadingHTTPServer((args.bind, args.port), handler) as server:
        server.benchmark_reports = args.benchmark_reports and args.loopback_http and args.bind == '127.0.0.1'
        if context is not None:
            server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = 'http' if args.loopback_http else 'https'
        print(f'CoinRender artifacts: {scheme}://{args.bind}:{args.port}/', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()

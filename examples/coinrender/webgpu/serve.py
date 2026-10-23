#!/usr/bin/env python3
"""Serve only the built WebGPU artifacts over TLS, including on a LAN."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
from urllib.parse import unquote, urlsplit


class ArtifactHandler(SimpleHTTPRequestHandler):
    allowed = {'index.html', 'app.js', 'coin_capture.js', 'coin_capture.wasm',
               'renderer.js', 'renderer_bg.wasm'}

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
    parser.add_argument('--cert', required=True, type=Path)
    parser.add_argument('--key', required=True, type=Path)
    parser.add_argument('--bind', default='127.0.0.1', help='Use 0.0.0.0 to allow other LAN devices')
    parser.add_argument('--port', type=int, default=8443)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if not all((directory / name).is_file() and not (directory / name).is_symlink()
               for name in ArtifactHandler.allowed):
        parser.error('--directory must contain the built demonstration artifacts (no symlinks)')
    if any(path.resolve().is_relative_to(directory) for path in (args.cert, args.key)):
        parser.error('TLS certificate and private key must be outside the served artifact directory')
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    handler = partial(ArtifactHandler, directory=str(directory))
    with ThreadingHTTPServer((args.bind, args.port), handler) as server:
        server.socket = context.wrap_socket(server.socket, server_side=True)
        print(f'CoinRender HTTPS artifacts: https://{args.bind}:{args.port}/', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()

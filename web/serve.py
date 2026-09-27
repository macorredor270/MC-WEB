#!/usr/bin/env python3
"""Servidor local para el build web con las cabeceras COOP/COEP que necesitan los hilos
(SharedArrayBuffer). Uso: python3 web/serve.py [carpeta] [puerto]"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else "build/web/apps/mcweb"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
    handler = functools.partial(Handler, directory=root)
    print(f"MC-WEB en http://localhost:{port}/ (sirviendo {root})")
    http.server.ThreadingHTTPServer(("", port), handler).serve_forever()

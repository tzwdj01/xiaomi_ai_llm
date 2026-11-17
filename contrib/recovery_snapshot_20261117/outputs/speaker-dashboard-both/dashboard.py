#!/usr/bin/env python3
"""Single LAN entry point for the two independently configured speaker dashboards."""
import importlib.util
import ipaddress
import json
import secrets
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = 18788
TOKEN = secrets.token_urlsafe(32)


def load(name, directory):
    spec = importlib.util.spec_from_file_location(name, ROOT / directory / 'dashboard.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


s12a = load('s12a_dashboard', 'speaker-dashboard')
s12 = load('s12_dashboard', 'speaker-dashboard-other')
LAN_IP, LAN_NET = s12a.network_details()
HOSTS = {'127.0.0.1:18788', 'localhost:18788'}
if LAN_IP:
    HOSTS.add(LAN_IP + ':18788')
PAGES = {'s12a': ROOT / 'speaker-dashboard/index.html',
         's12': ROOT / 'speaker-dashboard-other/index.html'}


class Handler(BaseHTTPRequestHandler):
    def valid(self):
        try:
            client = ipaddress.IPv4Address(self.client_address[0])
            return self.headers.get('Host') in HOSTS and (client.is_loopback or (LAN_NET is not None and client in LAN_NET))
        except ValueError:
            return False

    def output(self, value, status=200, kind='application/json; charset=utf-8'):
        body = value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if not self.valid():
            self.send_error(403)
            return
        if self.path == '/':
            self.output((ROOT / 'speaker-dashboard-both/index.html').read_bytes(), kind='text/html; charset=utf-8')
            return
        parts = self.path.strip('/').split('/')
        if len(parts) != 2 or parts[0] not in PAGES:
            self.send_error(404)
            return
        device, action = parts
        if action == 'view':
            page = PAGES[device].read_text().replace('__TOKEN__', TOKEN)
            page = page.replace("fetch('/data'", "fetch('/%s/data'" % device)
            page = page.replace("fetch('/save'", "fetch('/%s/save'" % device)
            self.output(page.encode(), kind='text/html; charset=utf-8')
        elif action == 'data':
            try:
                value = s12a.data() if device == 's12a' else s12.read_device()
            except Exception as exc:
                value = {'online': False, 'error': str(exc)}
            self.output(value)
        else:
            self.send_error(404)

    def do_POST(self):
        if not self.valid() or self.headers.get('Origin') != 'http://' + self.headers.get('Host', '') or self.headers.get('X-Token') != TOKEN:
            self.send_error(403)
            return
        parts = self.path.strip('/').split('/')
        if len(parts) != 2 or parts[0] not in PAGES or parts[1] != 'save':
            self.send_error(404)
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 4096:
                raise ValueError('请求大小不正确')
            value = json.loads(self.rfile.read(length))
            if not ipaddress.IPv4Address(self.client_address[0]).is_loopback and value.get('key', '').strip():
                raise ValueError('API Key 请在 Mac 本机看板中填写')
            (s12a.save if parts[0] == 's12a' else s12.save_settings)(value)
            self.output({'ok': True})
        except Exception as exc:
            self.output({'ok': False, 'error': str(exc)}, 400)

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    servers = []
    try:
        servers.append(ThreadingHTTPServer(('127.0.0.1', PORT), Handler))
        if LAN_IP:
            servers.append(ThreadingHTTPServer((LAN_IP, PORT), Handler))
    except OSError:
        for server in servers:
            server.server_close()
        raise
    for server in servers[1:]:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    threading.Timer(.5, lambda: webbrowser.open('http://127.0.0.1:18788/')).start()
    print('双设备看板: http://127.0.0.1:18788/  局域网: http://%s:18788/' % LAN_IP, flush=True)
    servers[0].serve_forever()

import http.server, threading, sys, urllib.parse
PORTS=[10005,10015,10030,12380,10080]
class H(http.server.BaseHTTPRequestHandler):
    def _h(self):
        n=int(self.headers.get('Content-Length') or 0)
        body=self.rfile.read(n) if n else b''
        print(f"[{self.server.server_port}] {self.command} {self.path}\n  H={dict(self.headers)}\n  B={body[:2000]!r}", flush=True)
        self.send_response(404); self.send_header('Content-Length','0'); self.end_headers()
    do_GET=do_POST=_h
    def log_message(self,*a): pass
for p in PORTS:
    s=http.server.ThreadingHTTPServer(('0.0.0.0',p),H)
    threading.Thread(target=s.serve_forever,daemon=True).start()
print("listening",PORTS,flush=True)
threading.Event().wait()

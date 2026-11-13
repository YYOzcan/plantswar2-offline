#!/usr/bin/env python3
"""Plants War 2 offline private server (work in progress)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pw2crypto
import http.server, threading, json, urllib.parse, time, os, sys
HOST_FOR_CLIENT = os.environ.get("PW2_CLIENT_HOST", "10.0.2.2")
LOG = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "traffic.log"), "a", buffering=1)

def log(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True); LOG.write(s + "\n")

class Base(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def reply(self, body, ctype="text/plain; charset=utf-8", code=200):
        if isinstance(body, str): body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers(); self.wfile.write(body)
    def handle_any(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        u = urllib.parse.urlsplit(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        log(f"[{self.server.server_port}] {self.command} {self.path} body={body[:3000]!r}")
        try:
            r = self.route(u.path, q, body)
        except Exception as e:
            import traceback; traceback.print_exc()
            r = None
        if r is None:
            log("  -> 404"); return self.reply(b"", code=404)
        log(f"  -> {r[:500] if isinstance(r,(str,bytes)) else r!r}")
        self.reply(r)
    do_GET = do_POST = handle_any

class UDW(Base):
    def route(self, path, q, body):
        p = path.lower()
        if p.endswith("getserviceinfo.aspx"):
            return json.dumps({"Code": "0", "Url": f"http://{HOST_FOR_CLIENT}:12380"})
        if p.endswith("gettermsinfo.aspx"):
            return json.dumps({"Terms": "", "Privacy": ""})
        if p.endswith("getnoticelist.aspx"):
            return json.dumps({"Event": {"Count": "0", "List": []}, "Notice": {"Count": "0", "List": []}})
        if self.server.server_port == 10080:
            # CDN: content file map with no extra downloadable content (all assets ship in the APK)
            return '<?xml version="1.0" encoding="UTF-8"?>\n<plantswar2>\n</plantswar2>\n'
        if p.endswith("getendinginfo.aspx"):
            return json.dumps({"ImageURL": "", "PageURL": "", "LinkURL": ""})
        return None

def parse_pd(body):
    form = dict(urllib.parse.parse_qsl(body.decode("latin1"), keep_blank_values=True))
    if "PD" not in form: return None
    plain, ok = pw2crypto.decode(form["PD"])
    return plain.decode("utf-8", "replace")

class Game(Base):
    def route(self, path, q, body):
        plain = parse_pd(body)
        log("  PD:", plain)
        if path.endswith("update.php"):
            params = dict(urllib.parse.parse_qsl(plain))
            act = params.get("act")
            if act == "12":   # data-table version list -> nothing to update
                xml = "<result><msg>1</msg><act><file_list></file_list></act></result>"
            elif act == "11": # CDN content map location
                xml = '<result><msg>1</msg><act v="11"><cdn ver="1" name="filemap.xml"/></act></result>'
            else:
                xml = "<result><msg>0</msg></result>"
            return pw2crypto.encode(xml)
        if path.endswith("action.php") or path.endswith("login.php"):
            return pw2crypto.encode(handle_action(plain))
        return None

def parse_actions(plain):
    """'act=2&&act=3&k=v&&&idx_id=0&...' -> ([{act:..}, ...], {globals})"""
    if "&&&" in plain:
        acts_part, glob_part = plain.split("&&&", 1)
    else:
        acts_part, glob_part = plain, ""
    acts = []
    for chunk in acts_part.split("&&"):
        if not chunk: continue
        acts.append(dict(urllib.parse.parse_qsl(chunk, keep_blank_values=True)))
    return acts, dict(urllib.parse.parse_qsl(glob_part, keep_blank_values=True))

import handlers
def handle_action(plain):
    import importlib
    importlib.reload(handlers)   # hot-reload while developing
    acts, g = parse_actions(plain)
    parts = []
    for a in acts:
        try:
            x = handlers.dispatch(a, g)
        except Exception:
            import traceback; traceback.print_exc(); x = None
        if x is None:
            log(f"  !! unhandled act {a}")
            x = ""
        parts.append(f'<act v="{a.get("act")}">{x}</act>')
    return "<result><msg>1</msg>" + "".join(parts) + "</result>"

def serve(port, cls):
    s = http.server.ThreadingHTTPServer(("0.0.0.0", port), cls)
    threading.Thread(target=s.serve_forever, daemon=True).start()

if __name__ == "__main__":
    for p in (10005, 10015, 10030, 10080): serve(p, UDW)
    serve(12380, Game)
    log("PW2 server up")
    threading.Event().wait()

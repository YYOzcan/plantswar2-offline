package pw2.offline;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Embedded Plants War 2 server (Java port of server/pw2server.py).
 *
 * GameActivity.onCreate calls {@link #start(Object)} before the native game starts; the patched
 * libGame.so talks to http://127.0.0.1:{10005,10015,10030,10080,12380}, so every device that runs
 * the APK is its own server and the game needs no network at all.
 */
public final class OfflineServer {
    private OfflineServer() {}

    static final int[] UDW_PORTS = {10005, 10015, 10030, 10080};
    static final int GAME_PORT = 12380;

    static String clientHost = "127.0.0.1";   // host the client is told to use for the game server
    static String bindHost = "127.0.0.1";
    static boolean verbose = false;
    private static boolean started;
    private static final Object LOCK = new Object();

    /** Called from GameActivity.onCreate with the Activity (a Context). Never throws. */
    public static synchronized void start(Object context) {
        if (started) return;
        started = true;
        try {
            File dir = filesDir(context);
            dir.mkdirs();
            verbose = debugProp();
            Store.init(new File(dir, "save.json"));
            Store.db();
            listenAll();
            Log.i("offline server up, save=" + Store.file);
        } catch (Throwable t) {
            Log.e("offline server failed to start", t);
        }
    }

    /** Save folder: app-private internal storage (always available, survives updates, removed on uninstall). */
    private static File filesDir(Object ctx) throws Exception {
        return (File) ctx.getClass().getMethod("getFilesDir").invoke(ctx);
    }

    /** adb shell setprop debug.pw2 1  -> log every request/response to logcat (tag PW2Server). */
    private static boolean debugProp() {
        try {
            Object v = Class.forName("android.os.SystemProperties").getMethod("get", String.class).invoke(null, "debug.pw2");
            return "1".equals(v);
        } catch (Throwable t) { return false; }
    }

    private static void listenAll() throws InterruptedException {
        List<Thread> ts = new ArrayList<Thread>();
        for (int p : UDW_PORTS) ts.add(listen(p));
        ts.add(listen(GAME_PORT));
        for (Thread t : ts) t.join(3000);   // sockets are bound before the game sends its first request
    }

    /** Binds on a worker thread (no network work on the UI thread), then accepts forever. */
    private static Thread listen(int port) {
        Thread t = new Thread(new Binder(port), "pw2-bind-" + port);
        t.setDaemon(true);
        t.start();
        return t;
    }

    private static final class Binder implements Runnable {
        final int port;
        Binder(int port) { this.port = port; }
        public void run() {
            ServerSocket ss;
            try {
                ss = new ServerSocket();
                ss.setReuseAddress(true);
                ss.bind(new InetSocketAddress(InetAddress.getByName(bindHost), port), 50);
            } catch (Throwable t) {
                Log.e("cannot listen on " + port, t);
                return;
            }
            Thread a = new Thread(new Acceptor(ss, port), "pw2-accept-" + port);
            a.setDaemon(true);
            a.start();
        }
    }

    private static final class Acceptor implements Runnable {
        final ServerSocket ss; final int port;
        Acceptor(ServerSocket ss, int port) { this.ss = ss; this.port = port; }
        public void run() {
            while (true) {
                try {
                    Thread w = new Thread(new Conn(ss.accept(), port), "pw2-conn-" + port);
                    w.setDaemon(true);
                    w.start();
                } catch (Throwable t) {
                    Log.e("accept " + port, t);
                    try { Thread.sleep(200); } catch (InterruptedException e) { return; }
                }
            }
        }
    }

    private static final class Conn implements Runnable {
        final Socket s; final int port;
        Conn(Socket s, int port) { this.s = s; this.port = port; }
        public void run() { serveConn(s, port); }
    }

    // ------------------------------------------------------------------ HTTP/1.1
    private static String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        int c;
        while ((c = in.read()) >= 0) {
            if (c == '\n') break;
            if (c != '\r') b.write(c);
            if (b.size() > 65536) throw new IOException("line too long");
        }
        if (c < 0 && b.size() == 0) return null;
        return new String(b.toByteArray(), "ISO-8859-1");
    }

    private static void serveConn(Socket s, int port) {
        try {
            s.setSoTimeout(60000);
            s.setTcpNoDelay(true);
            InputStream in = new BufferedInputStream(s.getInputStream());
            OutputStream out = s.getOutputStream();
            while (true) {
                String line = readLine(in);
                if (line == null) break;
                if (line.length() == 0) continue;
                String[] rl = line.split(" ");
                if (rl.length < 2) break;
                String path = rl[1];
                boolean http10 = rl.length > 2 && rl[2].equals("HTTP/1.0");
                int len = 0;
                boolean close = http10, expect = false;
                String h;
                while ((h = readLine(in)) != null && h.length() > 0) {
                    int c = h.indexOf(':');
                    if (c < 0) continue;
                    String k = h.substring(0, c).trim().toLowerCase(java.util.Locale.ROOT), v = h.substring(c + 1).trim();
                    if (k.equals("content-length")) len = Integer.parseInt(v);
                    else if (k.equals("connection")) close = v.equalsIgnoreCase("close") || (http10 && !v.equalsIgnoreCase("keep-alive"));
                    else if (k.equals("expect")) expect = v.equalsIgnoreCase("100-continue");
                }
                if (expect) { out.write("HTTP/1.1 100 Continue\r\n\r\n".getBytes("ISO-8859-1")); out.flush(); }
                byte[] body = new byte[len];
                int got = 0;
                while (got < len) {
                    int n = in.read(body, got, len - got);
                    if (n < 0) throw new IOException("eof in body");
                    got += n;
                }
                String resp;
                try {
                    synchronized (LOCK) { resp = route(port, path, new String(body, "ISO-8859-1")); }
                } catch (Throwable t) {
                    Log.e("route " + path, t);
                    resp = null;
                }
                byte[] rb = resp == null ? new byte[0] : resp.getBytes("UTF-8");
                String head = (resp == null ? "HTTP/1.1 404 Not Found" : "HTTP/1.1 200 OK")
                        + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + rb.length
                        + (close ? "\r\nConnection: close" : "") + "\r\n\r\n";
                out.write(head.getBytes("ISO-8859-1"));
                out.write(rb);
                out.flush();
                if (close) break;
            }
        } catch (Throwable t) {
            if (verbose) Log.e("conn " + port, t);
        } finally {
            try { s.close(); } catch (IOException ignored) { }
        }
    }

    // ------------------------------------------------------------------ routing
    static String route(int port, String fullPath, String body) {
        int q = fullPath.indexOf('?');
        String path = q >= 0 ? fullPath.substring(0, q) : fullPath;
        if (verbose) Log.i("[" + port + "] " + fullPath + " body=" + body);
        String r = port == GAME_PORT && path.startsWith("/pw2/admin/") ? admin(path, body)
                : port == GAME_PORT ? game(path, body) : udw(port, path.toLowerCase(java.util.Locale.ROOT));
        if (verbose) Log.i("  -> " + r);
        return r;
    }

    /**
     * Save backup/restore (loopback only; from a PC: adb forward tcp:12380 tcp:12380, see scripts/save.sh).
     *   GET  /pw2/admin/save  -> save.json
     *   POST /pw2/admin/save  -> replace save.json with the request body (old one kept as save.json.bak)
     */
    static String admin(String path, String body) {
        if (!path.equals("/pw2/admin/save")) return null;
        if (body.length() == 0) return Json.write(Store.db());
        try {
            String json = new String(body.getBytes("ISO-8859-1"), "UTF-8");
            Object d = Json.parse(json);
            if (!(d instanceof Map) || !(((Map<?, ?>) d).get("users") instanceof Map)) return "ERROR: not a PW2 save";
            Store.replace(json);
            return "OK " + ((Map<?, ?>) ((Map<?, ?>) d).get("users")).size() + " account(s) restored";
        } catch (Throwable t) {
            Log.e("restore", t);
            return "ERROR: " + t;
        }
    }

    static String udw(int port, String p) {
        if (p.endsWith("getserviceinfo.aspx")) return "{\"Code\": \"0\", \"Url\": \"http://" + clientHost + ":" + GAME_PORT + "\"}";
        if (p.endsWith("gettermsinfo.aspx")) return "{\"Terms\": \"\", \"Privacy\": \"\"}";
        if (p.endsWith("getnoticelist.aspx"))
            return "{\"Event\": {\"Count\": \"0\", \"List\": []}, \"Notice\": {\"Count\": \"0\", \"List\": []}}";
        if (port == 10080)   // CDN: content map with nothing extra to download (all assets ship in the APK)
            return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plantswar2>\n</plantswar2>\n";
        if (p.endsWith("getendinginfo.aspx")) return "{\"ImageURL\": \"\", \"PageURL\": \"\", \"LinkURL\": \"\"}";
        return null;
    }

    static String game(String path, String body) {
        String pd = parseQs(body, true).get("PD");
        if (pd == null) return null;
        String plain = Crypto.decode(pd);
        if (verbose) Log.i("  PD: " + plain);
        String xml;
        if (path.endsWith("update.php")) {
            String act = parseQs(plain, false).get("act");
            if ("12".equals(act)) xml = "<result><msg>1</msg><act><file_list></file_list></act></result>";
            else if ("11".equals(act)) xml = "<result><msg>1</msg><act v=\"11\"><cdn ver=\"1\" name=\"filemap.xml\"/></act></result>";
            else xml = "<result><msg>0</msg></result>";
        } else if (path.endsWith("action.php") || path.endsWith("login.php")) {
            xml = handleAction(plain);
        } else {
            return null;
        }
        if (verbose) Log.i("  XML: " + xml);
        return Crypto.encode(xml);
    }

    /** 'act=2&&act=3&k=v&&&idx_id=0&...' -> acts + request-global params */
    static String handleAction(String plain) {
        String actsPart = plain, globPart = "";
        int i = plain.indexOf("&&&");
        if (i >= 0) { actsPart = plain.substring(0, i); globPart = plain.substring(i + 3); }
        Map<String, String> g = parseQs(globPart, true);
        StringBuilder out = new StringBuilder("<result><msg>1</msg>");
        for (String chunk : actsPart.split("&&")) {
            if (chunk.length() == 0) continue;
            Map<String, String> a = parseQs(chunk, true);
            String x;
            try {
                x = Handlers.dispatch(a, g);
            } catch (Throwable t) {
                Log.e("act " + a, t);
                x = null;
            }
            if (x == null) { Log.i("unhandled act " + a); x = ""; }
            out.append("<act v=\"").append(Handlers.s(a.get("act"))).append("\">").append(x).append("</act>");
        }
        return out.append("</result>").toString();
    }

    /** urllib.parse.parse_qsl -> dict (later keys win, '+' = space, lenient %XX, UTF-8). */
    static Map<String, String> parseQs(String s, boolean keepBlank) {
        Map<String, String> m = new LinkedHashMap<String, String>();
        for (String part : s.split("&")) {
            if (part.length() == 0) continue;
            int e = part.indexOf('=');
            String k, v;
            if (e < 0) { if (!keepBlank) continue; k = part; v = ""; }
            else { k = part.substring(0, e); v = part.substring(e + 1); }
            if (v.length() == 0 && !keepBlank) continue;
            m.put(unquote(k), unquote(v));
        }
        return m;
    }

    static String unquote(String s) {
        try {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if (c == '+') { b.write(' '); continue; }
                if (c == '%' && i + 2 < s.length() && hex(s.charAt(i + 1)) >= 0 && hex(s.charAt(i + 2)) >= 0) {
                    b.write(hex(s.charAt(i + 1)) * 16 + hex(s.charAt(i + 2)));
                    i += 2;
                    continue;
                }
                b.write(String.valueOf(c).getBytes("UTF-8"));
            }
            return new String(b.toByteArray(), "UTF-8");
        } catch (IOException e) { throw new RuntimeException(e); }
    }

    private static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    /** PC use: java -cp classes pw2.offline.OfflineServer <save dir> [client host] [bind host] */
    public static void main(String[] args) throws Exception {
        File dir = new File(args.length > 0 ? args[0] : ".");
        if (args.length > 1) clientHost = args[1];
        if (args.length > 2) bindHost = args[2];
        dir.mkdirs();
        verbose = true;
        Store.init(new File(dir, "save.json"));
        listenAll();
        Log.i("offline server up on " + bindHost + ", save=" + Store.file);
        Thread.sleep(Long.MAX_VALUE);
    }
}

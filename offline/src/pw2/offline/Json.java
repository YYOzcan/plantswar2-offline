package pw2.offline;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Minimal JSON reader/writer (no Android/JDK-9+ APIs), compatible with the Python server's save.json. */
final class Json {
    private Json() {}

    static Object parse(String s) {
        P p = new P(s);
        Object v = p.val();
        return v;
    }

    private static final class P {
        final String s; int i;
        P(String s) { this.s = s; }
        void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++; }
        RuntimeException err() { return new IllegalArgumentException("bad json at " + i); }
        void expect(char c) { ws(); if (i >= s.length() || s.charAt(i) != c) throw err(); i++; }

        Object val() {
            ws();
            if (i >= s.length()) throw err();
            char c = s.charAt(i);
            if (c == '{') {
                i++;
                Map<String, Object> m = new LinkedHashMap<String, Object>();
                ws();
                if (s.charAt(i) == '}') { i++; return m; }
                while (true) {
                    ws();
                    String k = str();
                    expect(':');
                    m.put(k, val());
                    ws();
                    char d = s.charAt(i++);
                    if (d == '}') return m;
                    if (d != ',') throw err();
                }
            }
            if (c == '[') {
                i++;
                List<Object> l = new ArrayList<Object>();
                ws();
                if (s.charAt(i) == ']') { i++; return l; }
                while (true) {
                    l.add(val());
                    ws();
                    char d = s.charAt(i++);
                    if (d == ']') return l;
                    if (d != ',') throw err();
                }
            }
            if (c == '"') return str();
            if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
            if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
            if (s.startsWith("null", i)) { i += 4; return null; }
            int st = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
            String n = s.substring(st, i);
            if (n.length() == 0) throw err();
            if (n.indexOf('.') >= 0 || n.indexOf('e') >= 0 || n.indexOf('E') >= 0) return Double.valueOf(n);
            return Long.valueOf(n);
        }

        String str() {
            ws();
            if (s.charAt(i) != '"') throw err();
            i++;
            StringBuilder b = new StringBuilder();
            while (true) {
                char c = s.charAt(i++);
                if (c == '"') return b.toString();
                if (c != '\\') { b.append(c); continue; }
                char e = s.charAt(i++);
                switch (e) {
                    case 'b': b.append('\b'); break;
                    case 'f': b.append('\f'); break;
                    case 'n': b.append('\n'); break;
                    case 'r': b.append('\r'); break;
                    case 't': b.append('\t'); break;
                    case 'u': b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; break;
                    default: b.append(e);
                }
            }
        }
    }

    static String write(Object v) {
        StringBuilder b = new StringBuilder();
        w(b, v, 0);
        return b.toString();
    }

    private static void nl(StringBuilder b, int ind) {
        b.append('\n');
        for (int k = 0; k < ind; k++) b.append(' ');
    }

    @SuppressWarnings("unchecked")
    private static void w(StringBuilder b, Object v, int ind) {
        if (v == null) { b.append("null"); return; }
        if (v instanceof String) { quote(b, (String) v); return; }
        if (v instanceof Boolean || v instanceof Number) { b.append(String.valueOf(v)); return; }
        if (v instanceof Map) {
            Map<String, Object> m = (Map<String, Object>) v;
            if (m.isEmpty()) { b.append("{}"); return; }
            b.append('{');
            Iterator<Map.Entry<String, Object>> it = m.entrySet().iterator();
            while (it.hasNext()) {
                Map.Entry<String, Object> e = it.next();
                nl(b, ind + 1);
                quote(b, e.getKey());
                b.append(": ");
                w(b, e.getValue(), ind + 1);
                if (it.hasNext()) b.append(',');
            }
            nl(b, ind);
            b.append('}');
            return;
        }
        if (v instanceof List) {
            List<Object> l = (List<Object>) v;
            if (l.isEmpty()) { b.append("[]"); return; }
            b.append('[');
            for (int k = 0; k < l.size(); k++) {
                nl(b, ind + 1);
                w(b, l.get(k), ind + 1);
                if (k + 1 < l.size()) b.append(',');
            }
            nl(b, ind);
            b.append(']');
            return;
        }
        quote(b, String.valueOf(v));
    }

    private static void quote(StringBuilder b, String s) {
        b.append('"');
        for (int k = 0; k < s.length(); k++) {
            char c = s.charAt(k);
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20) b.append(String.format(java.util.Locale.ROOT, "\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        b.append('"');
    }
}

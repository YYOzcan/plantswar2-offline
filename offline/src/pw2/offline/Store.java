package pw2.offline;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.LinkedHashMap;
import java.util.Map;

/** Persistent player state (same JSON layout as server/state.py, so saves move between PC and phone). */
@SuppressWarnings("unchecked")
final class Store {
    private Store() {}

    static File file;
    private static Map<String, Object> db;

    static synchronized void init(File f) { file = f; db = null; }

    static synchronized Map<String, Object> db() {
        if (db == null) {
            try {
                if (file != null && file.exists()) db = (Map<String, Object>) Json.parse(read(file));
            } catch (Throwable t) {
                Log.e("save.json unreadable, keeping a copy and starting fresh", t);
                file.renameTo(new File(file.getPath() + ".broken-" + System.currentTimeMillis()));
            }
            if (db == null) {
                db = new LinkedHashMap<String, Object>();
                db.put("next_idx", 1001L);
                db.put("users", new LinkedHashMap<String, Object>());
            }
        }
        return db;
    }

    static synchronized void save() {
        try {
            File tmp = new File(file.getPath() + ".tmp");
            FileOutputStream o = new FileOutputStream(tmp);
            try {
                o.write(Json.write(db()).getBytes("UTF-8"));
                o.getFD().sync();
            } finally { o.close(); }
            if (!tmp.renameTo(file)) { file.delete(); tmp.renameTo(file); }
        } catch (Throwable t) { Log.e("save failed", t); }
    }

    /** Install a restored save (the current file is kept as save.json.bak). */
    @SuppressWarnings("unchecked")
    static synchronized void replace(String json) throws java.io.IOException {
        Map<String, Object> d = (Map<String, Object>) Json.parse(json);
        if (file.exists()) {
            File bak = new File(file.getPath() + ".bak");
            bak.delete();
            file.renameTo(bak);
        }
        db = d;
        save();
    }

    static Map<String, Object> users() { return (Map<String, Object>) db().get("users"); }

    static Map<String, Object> user(Object idx) {
        if (idx == null) return null;
        return (Map<String, Object>) users().get(String.valueOf(idx));
    }

    static synchronized Map<String, Object> newUser(String nick, String snsType, String snsId) {
        Map<String, Object> d = db();
        long idx = ((Number) d.get("next_idx")).longValue();
        d.put("next_idx", idx + 1);
        long now = System.currentTimeMillis() / 1000L;
        Map<String, Object> u = new LinkedHashMap<String, Object>();
        u.put("idx_id", idx);
        u.put("idx_time", now);
        u.put("nick", nick);
        u.put("sns_type", snsType);
        u.put("sns_id", snsId);
        u.put("skey", "offline" + idx);
        u.put("created", now);
        users().put(String.valueOf(idx), u);
        save();
        return u;
    }

    static String read(File f) throws java.io.IOException {
        InputStream in = new FileInputStream(f);
        try {
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] b = new byte[8192];
            int n;
            while ((n = in.read(b)) > 0) o.write(b, 0, n);
            return new String(o.toByteArray(), "UTF-8");
        } finally { in.close(); }
    }
}

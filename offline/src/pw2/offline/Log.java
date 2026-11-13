package pw2.offline;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.Method;

/** Logs to logcat (tag PW2Server) on Android, stdout elsewhere. No compile-time Android dependency. */
final class Log {
    private Log() {}

    private static Method logI;
    static {
        try { logI = Class.forName("android.util.Log").getMethod("i", String.class, String.class); }
        catch (Throwable t) { logI = null; }
    }

    static void i(String s) {
        if (s.length() > 1000) s = s.substring(0, 1000) + "...";
        if (logI != null) {
            try { logI.invoke(null, "PW2Server", s); return; } catch (Throwable ignored) { }
        }
        System.out.println(s);
    }

    static void e(String msg, Throwable t) {
        StringWriter w = new StringWriter();
        t.printStackTrace(new PrintWriter(w));
        i(msg + ": " + w);
    }
}

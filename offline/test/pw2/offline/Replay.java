package pw2.offline;

import java.io.*;

/** Test: java pw2.offline.Replay <requests.txt> <save dir>  -> one response XML per line */
public class Replay {
    public static void main(String[] a) throws Exception {
        Store.init(new File(a[1], "save.json"));
        BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(a[0]), "UTF-8"));
        PrintStream out = new PrintStream(System.out, true, "UTF-8");
        String l;
        while ((l = r.readLine()) != null) out.println(OfflineServer.handleAction(l));
    }
}

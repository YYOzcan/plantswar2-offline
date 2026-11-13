import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
public class DumpPar extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    List<Function> fs = new ArrayList<>();
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
    while (it.hasNext()) {
      Function f = it.next();
      if (f.isThunk() || f.isExternal()) continue;
      String n = f.getName(true);
      if (n.startsWith("std::") || n.startsWith("Curl") || n.startsWith("curl") || n.startsWith("FUN_") || n.startsWith("png") || n.startsWith("inflate") || n.startsWith("deflate") || n.startsWith("FT_") || n.startsWith("ft_") || n.startsWith("TT_") || n.startsWith("_") || n.startsWith("ov_") || n.startsWith("vorbis") || n.startsWith("ogg")) continue;
      fs.add(f);
    }
    println("functions: " + fs.size());
    int T = 12;
    String[] results = new String[fs.size()];
    ExecutorService ex = Executors.newFixedThreadPool(T);
    ThreadLocal<DecompInterface> tl = ThreadLocal.withInitial(() -> { DecompInterface d = new DecompInterface(); d.openProgram(currentProgram); return d; });
    List<Future<?>> futs = new ArrayList<>();
    for (int i = 0; i < fs.size(); i++) {
      final int k = i;
      futs.add(ex.submit(() -> {
        Function f = fs.get(k);
        DecompileResults r = tl.get().decompileFunction(f, 90, monitor);
        String c = (r != null && r.getDecompiledFunction() != null) ? r.getDecompiledFunction().getC() : "";
        results[k] = "// ==== " + f.getName(true) + " @ " + f.getEntryPoint() + "\n" + c;
      }));
    }
    for (Future<?> fu : futs) fu.get();
    ex.shutdown();
    PrintWriter pw = new PrintWriter(new FileWriter(out));
    for (String s : results) pw.println(s);
    pw.close();
    println("dumped " + fs.size());
  }
}

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpAll extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    PrintWriter pw = new PrintWriter(new FileWriter(out));
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
    int n=0;
    while (it.hasNext()) {
      Function f = it.next();
      if (f.isThunk() || f.isExternal()) continue;
      DecompileResults r = d.decompileFunction(f, 60, monitor);
      pw.println("// ==== " + f.getName(true) + " @ " + f.getEntryPoint());
      if (r != null && r.getDecompiledFunction()!=null) pw.println(r.getDecompiledFunction().getC());
      n++;
    }
    pw.close();
    println("dumped " + n);
  }
}

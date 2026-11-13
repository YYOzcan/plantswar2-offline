import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.pcode.*;
import java.io.*;
import java.util.*;
public class FixAndDump extends GhidraScript {
  Function fn(String sym) {
    for (Symbol s : currentProgram.getSymbolTable().getSymbols(sym)) {
      Function f = getFunctionAt(s.getAddress()); if (f != null) return f;
    }
    // try by demangled name
    SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
    return null;
  }
  void setSig(String cls, String name, String[] pnames, DataType[] ptypes) throws Exception {
    Program p = currentProgram;
    for (Symbol s : p.getSymbolTable().getSymbols(name)) {
      if (!s.getParentNamespace().getName().equals(cls)) continue;
      Function f = getFunctionAt(s.getAddress());
      if (f == null) continue;
      Register[] regs = { p.getRegister("r0"), p.getRegister("r1"), p.getRegister("r2"), p.getRegister("r3") };
      List<ParameterImpl> params = new ArrayList<>();
      for (int i = 0; i < pnames.length; i++)
        params.add(new ParameterImpl(pnames[i], ptypes[i], regs[i], p));
      f.setCallingConvention("__stdcall");
      f.updateFunction(null, new ReturnParameterImpl(VoidDataType.dataType, p),
        params, Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
      println("fixed " + cls + "::" + name + " @ " + f.getEntryPoint());
    }
  }
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DataType vp = new PointerDataType(VoidDataType.dataType);
    DataType cp = new PointerDataType(CharDataType.dataType);
    DataType in = IntegerDataType.dataType;
    setSig("XMLParser", "x_GetAttrib", new String[]{"ret","xml","pos","attr"}, new DataType[]{vp,vp,in,cp});
    setSig("XMLParser", "x_GetData", new String[]{"ret","xml","pos"}, new DataType[]{vp,vp,in});
    setSig("XMLParser", "GetAttribName", new String[]{"ret","xml","n"}, new DataType[]{vp,vp,in});
    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    PrintWriter pw = new PrintWriter(new FileWriter(out));
    String[] want = getScriptArgs()[1].split(",");
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
    int n = 0;
    while (it.hasNext()) {
      Function f = it.next();
      String full = f.getName(true);
      boolean ok = false;
      for (String w : want) if (full.startsWith(w)) ok = true;
      if (!ok) continue;
      DecompileResults r = d.decompileFunction(f, 120, monitor);
      pw.println("// ==== " + full + " @ " + f.getEntryPoint());
      if (r != null && r.getDecompiledFunction() != null) pw.println(r.getDecompiledFunction().getC());
      n++;
    }
    pw.close();
    println("dumped " + n);
  }
}

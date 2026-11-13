import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.data.*;
import ghidra.program.model.address.*;
public class ConstPools extends GhidraScript {
  public void run() throws Exception {
    int n = 0, c = 0;
    Listing L = currentProgram.getListing();
    for (ghidra.program.model.symbol.Symbol sy : currentProgram.getSymbolTable().getAllSymbols(true)) {
      if (!sy.getName().startsWith("DAT_")) continue;
      Address a = sy.getAddress();
      MemoryBlock mb = currentProgram.getMemory().getBlock(a);
      if (mb == null || !mb.isExecute()) continue;
      Data d = L.getDataAt(a);
      if (d == null || !d.isDefined()) {
        try { L.clearCodeUnits(a, a.add(3), false); L.createData(a, DWordDataType.dataType); c++; } catch (Exception e) {}
      }
    }
    println("created " + c);
    for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
      if (!b.isExecute()) continue;
      DataIterator it = currentProgram.getListing().getDefinedData(new AddressSet(b.getStart(), b.getEnd()), true);
      while (it.hasNext()) {
        Data d = it.next();
        if (d.getLength() != 4) continue;
        MutabilitySettingsDefinition.DEF.setChoice(d, MutabilitySettingsDefinition.CONSTANT);
        n++;
      }
    }
    println("constified " + n);
  }
}

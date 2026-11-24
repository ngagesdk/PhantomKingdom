import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;
import java.util.*;

public class PKDumpOnly extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new FileWriter(a[0]));
        PrintWriter px = new PrintWriter(new FileWriter(a[0] + ".xrefs"));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (it.hasNext()) {
            Function f = it.next();
            if (f.getName().matches("^[A-Z]+_\\d+$")) continue;
            StringBuilder sb = new StringBuilder();
            for (Reference r : getReferencesTo(f.getEntryPoint())) sb.append(" ").append(r.getFromAddress());
            px.println(f.getEntryPoint() + " " + f.getName() + " <-" + sb);
            DecompileResults r = di.decompileFunction(f, 90, monitor);
            pw.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
            if (r != null && r.getDecompiledFunction() != null) pw.println(r.getDecompiledFunction().getC());
            else pw.println("// decompile failed");
            n++;
            if (n % 200 == 0) { pw.flush(); println("decompiled " + n); }
        }
        pw.close(); px.close();
        println("done " + n);
    }
}

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.listing.*;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.address.*;
import java.io.*;

public class DecompAt extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter pw = new PrintWriter(new FileWriter(a[0]));
        DecompInterface di = new DecompInterface();
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        int fixed = 0;
        while (fit.hasNext()) { Function fn = fit.next(); if (fn.hasNoReturn()) { fn.setNoReturn(false); fixed++; } }
        println("cleared noreturn on " + fixed);
        di.openProgram(currentProgram);
        for (int i = 1; i < a.length; i++) {
            Address ad = toAddr(Long.parseLong(a[i], 16));
            Function f = getFunctionAt(ad);
            if (f == null) {
                Function c = getFunctionContaining(ad);
                if (c != null) {
                    c.setBody(new AddressSet(c.getEntryPoint(), ad.subtract(1)));
                }
                disassemble(ad);
                new CreateFunctionCmd(ad).applyTo(currentProgram, monitor);
                f = getFunctionAt(ad);
            }
            pw.println("// ===== " + a[i]);
            if (f == null) { pw.println("// no function"); continue; }
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            pw.println(r.getDecompiledFunction() != null ? r.getDecompiledFunction().getC() : "// failed");
        }
        pw.close();
    }
}

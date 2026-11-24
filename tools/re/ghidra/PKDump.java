import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;
import java.util.*;

public class PKDump extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        BufferedReader br = new BufferedReader(new FileReader(a[0]));
        String line; List<Address> ents = new ArrayList<>();
        while ((line = br.readLine()) != null) {
            String[] p = line.trim().split(" ");
            Address ad = toAddr(Long.parseLong(p[0], 16));
            createLabel(ad, p[1], true, SourceType.USER_DEFINED);
            ents.add(ad);
        }
        for (Address ad : ents) {
            new ArmDisassembleCommand(ad, null, false).applyTo(currentProgram, monitor);
            new CreateFunctionCmd(ad).applyTo(currentProgram, monitor);
        }
        analyzeAll(currentProgram);
        for (Address ad : ents) if (getFunctionAt(ad) == null) new CreateFunctionCmd(ad).applyTo(currentProgram, monitor);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new FileWriter(a[1]));
        PrintWriter px = new PrintWriter(new FileWriter(a[1] + ".xrefs"));
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

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.app.cmd.function.CreateFunctionCmd;

public class PKFix extends GhidraScript {
    public void run() throws Exception {
        int pass, total = 0;
        for (pass = 0; pass < 4; pass++) {
            int n = 0;
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            java.util.List<Address> sites = new java.util.ArrayList<>();
            while (it.hasNext()) {
                Function f = it.next();
                if (f.hasNoReturn()) { f.setNoReturn(false); n++; }
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    if (r.getReferenceType().isCall()) sites.add(r.getFromAddress().add(4));
                }
            }
            int d = 0;
            for (Address a : sites) {
                if (getInstructionAt(a) == null && getDataAt(a) == null) { if (disassemble(a)) d++; }
            }
            println("pass " + pass + ": noreturn cleared " + n + ", disassembled " + d);
            total += n + d;
            if (n + d == 0) break;
            analyzeChanges(currentProgram);
        }
        // functions whose bodies changed get re-created
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
        }
    }
}

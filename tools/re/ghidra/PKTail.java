import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.app.cmd.function.CreateFunctionCmd;

/* Re-joins code that follows calls (wrongly split off as separate functions
 * because a callee was considered non-returning). */
public class PKTail extends GhidraScript {
    public void run() throws Exception {
        Listing L = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        int ov = 0, rm = 0, dis = 0;
        InstructionIterator ii = L.getInstructions(true);
        java.util.List<Address> after = new java.util.ArrayList<>();
        while (ii.hasNext()) {
            Instruction in = ii.next();
            if (!in.getFlowType().isCall()) continue;
            if (in.getFlowOverride() != FlowOverride.NONE) { in.setFlowOverride(FlowOverride.NONE); ov++; }
            after.add(in.getAddress().add(4));
        }
        for (Address a : after) {
            if (getInstructionAt(a) == null && getDataAt(a) == null) { if (disassemble(a)) dis++; }
            Function f = fm.getFunctionAt(a);
            if (f == null) continue;
            boolean called = false;
            for (Reference r : getReferencesTo(a)) {
                RefType t = r.getReferenceType();
                if (t.isCall() || t.isData() || t.isIndirect()) { called = true; break; }
            }
            if (!called) { fm.removeFunction(a); rm++; }
        }
        println("overrides " + ov + " disassembled " + dis + " removed " + rm);
        FunctionIterator it = fm.getFunctions(true);
        while (it.hasNext()) CreateFunctionCmd.fixupFunctionBody(currentProgram, it.next(), monitor);
    }
}

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
public class ListAt extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter pw = new PrintWriter(new FileWriter(a[0]));
        Address s = toAddr(Long.parseLong(a[1], 16)), e = toAddr(Long.parseLong(a[2], 16));
        InstructionIterator it = currentProgram.getListing().getInstructions(s, true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (i.getAddress().compareTo(e) > 0) break;
            pw.println(i.getAddress() + "  " + i);
        }
        pw.close();
    }
}

# Reverse engineering tools

Scripts used to study `6RAH.APP`. They work on your own copy and write
everything they derive into a work directory outside the repository.

| File | Purpose |
|---|---|
| `run_ghidra.sh` | full headless Ghidra analysis and C listing |
| `mk_ghidra_input.py` | E32 image to raw code + labels (imports as `DLL_ordinal`) |
| `e32.py` | E32 header, import and relocation parser |
| `annotate.py` | resolves literal-pool loads (`DAT_xxx`) to values and strings |
| `armtail.py` | decodes the few code tails Ghidra leaves undisassembled |
| `ghidra/PKDump.java` | labels, analysis, decompiles every function |
| `ghidra/PKFix.java` | clears wrong no-return flags and re-disassembles call sites |
| `ghidra/DecompAt.java`, `ListAt.java` | decompile / list single addresses |

Ghidra 12.x needs JDK 25. Import ordinals are not resolved to names (the
SDK's `.lib` files are needed for that); they are named by usage in
`docs/RE_NOTES.md`.

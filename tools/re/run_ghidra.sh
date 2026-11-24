#!/bin/sh
# Full analysis of your own 6RAH.APP with Ghidra (12.x needs JDK 25).
#   GHIDRA_HOME=/path/to/ghidra JAVA_HOME=/path/to/jdk25 \
#   tools/re/run_ghidra.sh /path/to/6rah.app /path/to/workdir
# The work directory receives derived files (binary, listings): keep it
# out of the repository.
set -e
APP=$1; WORK=$2
HERE=$(cd "$(dirname "$0")" && pwd)
: "${GHIDRA_HOME:?set GHIDRA_HOME}"
mkdir -p "$WORK/proj"
python3 "$HERE/mk_ghidra_input.py" "$APP" "$WORK"
"$GHIDRA_HOME/support/analyzeHeadless" "$WORK/proj" pk -import "$WORK/6rah.bin" \
    -loader BinaryLoader -loader-baseAddr 0x10000000 -processor ARM:LE:32:v5t \
    -scriptPath "$HERE/ghidra" -postScript PKDump.java "$WORK/6rah.labels" "$WORK/6rah.c"
"$GHIDRA_HOME/support/analyzeHeadless" "$WORK/proj" pk -process 6rah.bin -noanalysis \
    -scriptPath "$HERE/ghidra" -postScript PKFix.java -postScript PKTail.java -postScript PKDumpOnly.java "$WORK/6rah.c"
python3 "$HERE/annotate.py" "$WORK/6rah.c" "$WORK/6rah_annotated.c" "$WORK/6rah.bin"
echo "done: $WORK/6rah_annotated.c"

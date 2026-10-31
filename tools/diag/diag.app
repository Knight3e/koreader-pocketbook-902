#!/bin/sh
# diag.app - finds out what makes KOReader die with "Illegal instruction" on the PocketBook 902.
# Needs: /mnt/ext1/applications/koreader (the KOReader folder) and /mnt/ext1/applications/sigprobe.
# Writes the report to /mnt/ext1/diag.txt . Changes nothing else.
KO=/mnt/ext1/applications/koreader
SP=/mnt/ext1/applications/sigprobe
OUT=/mnt/ext1/diag.txt
{
echo "diag started: $(date)"
cd "$KO" || { echo "ERROR: $KO not found"; exit 1; }
[ -x "$SP" ] || chmod 755 "$SP" 2>/dev/null
echo "LD_LIBRARY_PATH at start: [$LD_LIBRARY_PATH]"

echo; echo "== A: luajit does floating point math (soft-float check)"
./luajit -e 'print(1.5+2, math.sqrt(2), 10/3, string.format("%.3f", 3.14159), math.floor(2.7), 2^0.5)'
echo "rc=$?"

echo; echo "== B: luajit loads InkView through ffi, device libraries only"
( unset LD_LIBRARY_PATH; ./luajit -e 'local ffi=require("ffi"); local l=ffi.load("inkview"); print("inkview loaded OK")' )
echo "rc=$?"

echo; echo "== C: the same, with LD_LIBRARY_PATH=koreader/libs (what koreader.app does)"
( LD_LIBRARY_PATH="$KO/libs" ./luajit -e 'local ffi=require("ffi"); local l=ffi.load("inkview"); print("inkview loaded OK")' )
echo "rc=$?"

echo; echo "== D: sigprobe: dlopen(libinkview.so), device libraries only"
( unset LD_LIBRARY_PATH; "$SP" libinkview.so )
echo "rc=$?"

echo; echo "== E: sigprobe: dlopen(libinkview.so) with LD_LIBRARY_PATH=koreader/libs"
( LD_LIBRARY_PATH="$KO/libs" "$SP" libinkview.so )
echo "rc=$?"

echo; echo "== F: which libraries get loaded in case E (LD_DEBUG, last 70 lines)"
( LD_DEBUG=libs LD_LIBRARY_PATH="$KO/libs" "$SP" libinkview.so 2>&1 | tail -70 )

echo; echo "== G: files in koreader/libs"
ls -la "$KO/libs" | head -150
echo; echo "diag finished: $(date)"
} > "$OUT" 2>&1

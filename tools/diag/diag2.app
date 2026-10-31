#!/bin/sh
# diag2.app - why does KOReader say "No reader engine for this file or invalid file" for EPUB/HTML?
# Needs /mnt/ext1/applications/koreader and /mnt/ext1/applications/sigprobe. Writes /mnt/ext1/diag2.txt. Changes nothing else.
KO=/mnt/ext1/applications/koreader
SP=/mnt/ext1/applications/sigprobe
OUT=/mnt/ext1/diag2.txt
{
echo "diag2 started: $(date)"
cd "$KO" || { echo "ERROR: $KO not found"; exit 1; }

echo; echo "== A: space and memory"
df -h /mnt/ext1 2>&1 | tail -2
du -sh "$KO" 2>&1
grep -E "^(MemTotal|MemFree|Cached)" /proc/meminfo

echo; echo "== B: can every KOReader library be loaded? (dlopen with RTLD_NOW; failures are shown in full)"
for f in "$KO"/libs/*.so*; do
    case "$f" in *.dbg) continue ;; esac
    r=$( LD_LIBRARY_PATH="$KO/libs" "$SP" "$f" 2>&1 )
    if echo "$r" | grep -q -- "-> OK" && echo "$r" | grep -q "finished without a crash"; then
        echo "ok      $(basename "$f")"
    else
        echo "FAILED  $(basename "$f")"; echo "$r" | sed 's/^/        /'
    fi
done

echo; echo "== C: what does KOReader's own Lua say when it requires the book engine (crengine)?"
LD_LIBRARY_PATH="$KO/libs" ./luajit -e 'require("setupkoenv"); local ok, e = pcall(require, "libs/libkoreader-cre"); print("require cre:", ok, e)' 2>&1 | tail -20
echo "rc=$?"

echo; echo "== D: end of koreader/crash.log (the last 120 lines)"
tail -n 120 "$KO/crash.log" 2>&1

echo; echo "== E: fonts and data folders"
ls "$KO/fonts" 2>&1 | head -20
ls "$KO/data" 2>&1 | head -20
echo; echo "diag2 finished: $(date)"
} > "$OUT" 2>&1

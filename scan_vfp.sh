#!/bin/bash
# Scans ELF files for instructions that the PocketBook 902's CPU cannot run (VFP, NEON, ARMv7).
# Usage: scan_vfp.sh DIR_OR_FILE...      (uses the ARM objdump of the legacy toolchain)
OBJDUMP="${OBJDUMP:-arm-kindle-linux-gnueabi-objdump}"
PAT='[[:space:]](v(add|sub|mul|div|mla|mls|neg|abs|sqrt|cmp|cvt[a-z]*|mov|ldr|str|ldm|stm|push|pop|mrs|msr|fma|nmul|rint[a-z]*|ld[1-4]|st[1-4]|and|orr|eor|shl|shr|dup|ext|tbl|zip|uzp|trn|rev|qadd|max|min|sel|bic|bsl|cnt|mvn|ceq|cgt|clt|cge)[a-z0-9.]*|movw|movt|ubfx|sbfx|bfi|bfc|rbit|cbz|cbnz|dmb|dsb|isb|sdiv|udiv)[[:space:]]'
found=0; total=0
while IFS= read -r f; do
    head -c4 "$f" 2>/dev/null | grep -q "ELF" || continue
    "$OBJDUMP" -f "$f" 2>/dev/null | grep -q "elf32-littlearm" || continue
    total=$((total+1))
    out=$("$OBJDUMP" -d "$f" 2>/dev/null | awk -v pat="$PAT" '/^[0-9a-f]+ <.*>:/{fn=$2; next} $0 ~ pat {c[fn]++; n++} END{for(f in c) printf "%d %s\n", c[f], f; }' | sort -rn)
    if [ -n "$out" ]; then
        found=$((found+1))
        echo "!! $f : $(echo "$out" | wc -l) function(s) with VFP/NEON/ARMv7-looking instructions"
        echo "$out" | head -5 | sed 's/^/     /'
    fi
done < <(find -L "$@" -type f \( -perm -u+x -o -name "*.so*" \) 2>/dev/null)
echo "scanned $total ARM ELF files, $found with suspicious instructions"
[ "$found" -eq 0 ] && echo "RESULT: clean (no VFP/NEON/ARMv7 instructions found)" || echo "RESULT: needs a look (note: objdump can mistake data for code, so single hits may be false alarms)"

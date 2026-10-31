#!/bin/bash
# Runs INSIDE the container (as a normal user: crosstool-NG refuses to run as root).
# Builds a SOFT-FLOAT version of the legacy "kindle" toolchain with the official koxtoolchain recipe.
set -euo pipefail
export HOME=/work/home
mkdir -p "$HOME"
TC="$HOME/x-tools/arm-kindle-linux-gnueabi"

is_soft() {   # does this compiler default to pure soft-float? (the prebuilt one says "softfp")
    [ -x "$1" ] && "$1" -Q --help=target 2>/dev/null | grep -E 'mfloat-abi=' | head -1 | grep -qE 'soft[[:space:]]*$'
}
scan_libstdcxx() {
    echo "== scanning the toolchain's libstdc++ for VFP/NEON/ARMv7 instructions =="
    OBJDUMP="$TC/bin/arm-kindle-linux-gnueabi-objdump" \
        bash /kit/scan_vfp.sh "$(readlink -f "$TC/arm-kindle-linux-gnueabi/lib/libstdc++.so.6")" || true
}

if is_soft "$TC/bin/arm-kindle-linux-gnueabi-gcc"; then
    echo "== soft-float toolchain already built: $TC =="
    scan_libstdcxx
    exit 0
fi

git config --global --add safe.directory '*'
cd /work
rm -rf koxtoolchain
git clone --depth 1 https://github.com/koreader/koxtoolchain.git
cd koxtoolchain
# hook: run soften-config.sh right after the sample config was loaded
python3 - << 'PY'
p = "gen-tc.sh"
s = open(p).read()
needle = '\t\t\t"${ct_ng[@]}" "${tc_target}"\n'
assert s.count(needle) == 1, "gen-tc.sh layout changed, cannot insert the hook"
s = s.replace(needle, needle + '\t\t\tbash /kit/soften-config.sh .config\n')
open(p, "w").write(s)
PY
echo "== building the soft-float toolchain (this takes a while: crosstool-NG, gcc, glibc ...) =="
if ! env -u CHOST -u CFLAGS -u CXXFLAGS -u LDFLAGS PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin ./gen-tc.sh kindle; then
    echo "== toolchain build FAILED. Saving crosstool-NG's detailed log to out/ct-ng-build.log =="
    LOGF="$(find /work/koxtoolchain -name build.log -path '*kindle*' 2>/dev/null | head -1)"
    if [ -n "$LOGF" ]; then cp "$LOGF" /out/ct-ng-build.log; echo "---- last 60 lines of $LOGF:"; tail -n 60 "$LOGF"; fi
    exit 1
fi
echo "== checking the result =="
GCC="$TC/bin/arm-kindle-linux-gnueabi-gcc"
if [ ! -x "$GCC" ]; then
    echo "ERROR: compiler not found in $TC/bin. Found instead:"
    find /work/home /work/koxtoolchain -maxdepth 6 -name 'arm-kindle-linux-gnueabi-gcc' 2>/dev/null
    exit 1
fi
echo "float ABI reported by the compiler: $("$GCC" -Q --help=target | grep -E 'mfloat-abi=' | head -1 | tr -s ' \t' ' ')"
is_soft "$GCC" || { echo "ERROR: toolchain is not pure soft-float (expected 'soft', not 'softfp')"; exit 1; }
scan_libstdcxx
echo "== soft-float toolchain ready: $TC =="

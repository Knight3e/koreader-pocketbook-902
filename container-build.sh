#!/bin/bash
# Runs INSIDE the container. Safe to run again: it resets the sources, re-applies the patches and builds incrementally.
set -euo pipefail
export HOME=/tmp
git config --global --add safe.directory '*'
export CHOST=arm-kindle-linux-gnueabi
SOFT_TC=/work/home/x-tools/arm-kindle-linux-gnueabi
if [ -x "$SOFT_TC/bin/arm-kindle-linux-gnueabi-gcc" ] && "$SOFT_TC/bin/arm-kindle-linux-gnueabi-gcc" -Q --help=target 2>/dev/null | grep -E 'mfloat-abi=' | head -1 | grep -qE 'soft[[:space:]]*$'; then
    export PATH="$SOFT_TC/bin:$PATH"; TCNAME=soft
else
    echo "WARNING: soft-float toolchain not found, using the prebuilt VFP one (KOReader will crash on the 902)"; TCNAME=prebuilt
fi
echo "== using $(command -v arm-kindle-linux-gnueabi-gcc) ($TCNAME) =="
KO_COMMIT="$(cat /kit/KOREADER_COMMIT)"
cd /work
if [ ! -d koreader/.git ]; then
    echo "== cloning KOReader =="
    git clone https://github.com/koreader/koreader.git
fi
cd koreader
echo "== checking out pinned commit $KO_COMMIT and its submodules =="
git fetch origin
git checkout -f "$KO_COMMIT"
git submodule update --init --recursive --force
echo "== applying the PocketBook 902 patches =="
find_patch() {   # works with ./patches/NAME and with the files lying flat next to build.sh
    for d in /kit/patches /kit; do [ -f "$d/$1" ] && { echo "$d/$1"; return; }; done
    echo "ERROR: patch $1 not found in /kit/patches or /kit" >&2; ls -la /kit >&2; exit 1
}
git -C base apply --whitespace=nowarn "$(find_patch 0001-base-pb902-armv6-softfloat-and-version.patch)"
git apply --whitespace=nowarn "$(find_patch 0002-frontend-pb902-device.patch)"
git apply --whitespace=nowarn "$(find_patch 0003-platform-pb902-launcher.patch)"
git -C base diff --stat; git diff --stat
if [ "$(cat /work/built-with 2>/dev/null || true)" != "$TCNAME" ]; then
    echo "== toolchain changed ($TCNAME): cleaning old build output =="
    rm -rf build koreader-pocketbook-*
fi
echo "== fetching third-party sources =="
make TARGET=pocketbook fetchthirdparty
echo "== building (this is the long step) =="
make TARGET=pocketbook all -j"$(nproc)"
INSTALL_DIR="$(ls -d koreader-pocketbook-* | head -1)"
echo "== install dir: $INSTALL_DIR =="
file --dereference "$INSTALL_DIR/koreader/luajit" || true
echo "== creating the ready-to-copy device folder (symlinks resolved, FAT-friendly) =="
rm -rf /out/device && mkdir -p /out/device/applications
cp -rL "$INSTALL_DIR/koreader" /out/device/applications/koreader
cp -L platform/pocketbook/koreader.app /out/device/applications/koreader.app
chmod 755 /out/device/applications/koreader.app
# the firmware looks for associated viewer apps ("Open with", last-page start) in system/bin: tiny forwarding launcher
mkdir -p /out/device/system/bin
cp -L platform/pocketbook/system_koreader.app /out/device/system/bin/koreader.app
chmod 755 /out/device/system/bin/koreader.app
echo "== removing debug symbol files (*.dbg): not needed on the device, they only waste space =="
du -sh /out/device/applications/koreader || true
find /out/device -name '*.dbg' -delete
du -sh /out/device/applications/koreader || true
echo "== scanning the result for instructions the 902 cannot run (VFP / NEON / ARMv7) =="
/kit/scan_vfp.sh /out/device/applications/koreader | tee /out/vfp_scan.txt
echo "$TCNAME" > /work/built-with
echo "== done =="

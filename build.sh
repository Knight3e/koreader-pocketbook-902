#!/bin/bash
# One command: ./build.sh   (needs Docker, ~25 GB free disk, internet)
set -euo pipefail
cd "$(dirname "$0")"
for f in Dockerfile container-build.sh build-toolchain.sh soften-config.sh makeinfo-wrapper.sh scan_vfp.sh KOREADER_COMMIT; do
    [ -f "$f" ] || { echo "ERROR: $f is missing in $(pwd). Extract the archive: tar xzf koreader-902-kit.tar.gz"; exit 1; }
done
for p in 0001-base-pb902-armv6-softfloat-and-version.patch 0002-frontend-pb902-device.patch 0003-platform-pb902-launcher.patch; do
    [ -f "patches/$p" ] || [ -f "$p" ] || { echo "ERROR: patch $p not found (neither in ./patches nor next to build.sh)"; exit 1; }
done
mkdir -p work out
echo "== 1/3: building the Docker image (build tools + ARMv6 toolchain) =="
docker build -t koreader-pb902-build .
# crosstool-NG refuses to run as root: use a normal user id inside the container
if [ "$(id -u)" = 0 ]; then RUN_AS="1000:1000"; chown -R 1000:1000 work out; else RUN_AS="$(id -u):$(id -g)"; fi
RUN=(docker run --rm --user "$RUN_AS" -v "$PWD/work:/work" -v "$PWD/out:/out" -v "$PWD:/kit:ro" koreader-pb902-build)
echo "== 2/3: building the SOFT-FLOAT toolchain (long, but only once; log: out/toolchain.log) =="
"${RUN[@]}" /kit/build-toolchain.sh 2>&1 | tee out/toolchain.log
echo "== 3/3: building KOReader (long; the full log is saved to out/build.log) =="
"${RUN[@]}" /kit/container-build.sh 2>&1 | tee out/build.log
echo
echo "Finished. Look into ./out :  device/ (copy to the e-reader), vfp_scan.txt, build.log"

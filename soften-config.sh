#!/bin/bash
# Called by the toolchain build right after crosstool-NG loaded the "kindle" sample config.
# Changes it from  ARM1136JF-S + VFP + softfp  to  ARM1176JZ-S, pure SOFT float (the 902 has no VFP):
# this makes libgcc / libstdc++ / glibc start files free of VFP instructions.
set -euo pipefail
cfg="${1:-.config}"
echo "[soften] before:"; grep -E '^(# )?CT_ARCH_(ARCH|CPU|TUNE|FPU|FLOAT)' "$cfg" || true
# drop every float/fpu/cpu line of the sample, then add ours
sed -i -E '/^(# )?CT_ARCH_FLOAT_(AUTO|HW|SW|SOFTFP)([= ]|$)/d; /^(# )?CT_ARCH_FPU([= ]|$)/d; /^CT_ARCH_CPU=/d; /^# CT_ARCH_CPU is not set/d' "$cfg"
cat >> "$cfg" << 'EOT'
CT_ARCH_CPU="arm1176jz-s"
CT_ARCH_FPU=""
# CT_ARCH_FLOAT_AUTO is not set
# CT_ARCH_FLOAT_HW is not set
CT_ARCH_FLOAT_SW=y
# CT_ARCH_FLOAT_SOFTFP is not set
EOT
echo "[soften] after:"; grep -E '^(# )?CT_ARCH_(ARCH|CPU|TUNE|FPU|FLOAT)' "$cfg" || true

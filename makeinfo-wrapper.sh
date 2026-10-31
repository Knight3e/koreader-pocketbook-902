#!/bin/bash
# Replaces /usr/bin/makeinfo inside the build container.
# texinfo 7's XS parser crashes ("parent hv not already set", core dump) on glibc 2.9's manual.
#  1) use the pure-Perl parser (TEXINFO_XS_PARSER=0), which does not have that bug
#  2) if it still fails, write an empty .info file and succeed: the toolchain does not need documentation
export TEXINFO_XS_PARSER=0
TEXI2ANY="${TEXI2ANY:-/usr/bin/texi2any}"
if (exec -a makeinfo "$TEXI2ANY" "$@"); then exit 0; fi
echo "makeinfo-wrapper: texi2any failed, writing an empty info file instead" >&2
out=""; prev=""; last=""
for a in "$@"; do
    case "$prev" in -o|--output) out="$a" ;; esac
    prev="$a"; case "$a" in -*) ;; *) last="$a" ;; esac
done
[ -n "$out" ] || out="${last%.*}.info"
: > "$out"
exit 0

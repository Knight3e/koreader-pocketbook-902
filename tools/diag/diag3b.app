#!/bin/sh
# diag3b.app - READ-ONLY: how does this firmware associate file types with applications?
# Writes /mnt/ext1/diag3b.txt line by line (so even a partial run leaves something). Prints no Wi-Fi/account files.
OUT=/mnt/ext1/diag3b.txt
echo "diag3b started" > "$OUT"
log() { echo "$@" >> "$OUT"; }
run() { log ""; log "== $*"; "$@" >> "$OUT" 2>&1; log "(rc=$?)"; }
log "script: $0"
run id
run ls -la /ebrmain
run ls -la /ebrmain/config
run sed -n 1,70p /ebrmain/config/extensions.cfg
run ls -la /ebrmain/bin
run ls -la /mnt/ext1/system
run ls -la /mnt/ext1/system/config
run ls -la /mnt/ext1/system/profiles
run ls -la /mnt/ext1/system/profiles/default
run ls -la /mnt/ext1/system/profiles/default/config
run cat /mnt/ext1/system/config/extensions.cfg
run cat /mnt/ext1/system/profiles/default/config/extensions.cfg
log ""
log "diag3b finished"

#!/bin/sh
# diag4.app - READ-ONLY: how are the home-screen themes and widgets set up on this firmware?
# Writes /mnt/ext1/diag4.txt and copies the (small) firmware theme files to /mnt/ext1/diag4/themes/ so they can be read on the PC.
# Prints no Wi-Fi/account files.
OUT=/mnt/ext1/diag4.txt
D=/mnt/ext1/diag4
echo "diag4 started" > "$OUT"
log() { echo "$@" >> "$OUT"; }
run() { log ""; log "== $*"; "$@" >> "$OUT" 2>&1; log "(rc=$?)"; }
mkdir -p "$D/themes"
run ls -la /ebrmain/themes
run du -sk /ebrmain/themes
run ls -laR /ebrmain/widgets
run ls -la /mnt/ext1/system/themes
run ls -laR /mnt/ext1/system/config/widgets
run cat /mnt/ext1/system/config/widgets.cfg
for f in /ebrmain/themes/*; do
    if [ -f "$f" ] && [ "$(wc -c < "$f")" -lt 3000000 ]; then cp "$f" "$D/themes/" 2>>"$OUT"; else log "not copied: $f"; fi
done
log ""; log "== copied theme files"; ls -la "$D/themes" >> "$OUT" 2>&1
for f in /mnt/ext1/system/config/widgets/*/*.cfg; do
    if [ -f "$f" ]; then log ""; log "-- $f"; cat "$f" >> "$OUT" 2>&1; log ""; fi
done
log ""
log "diag4 finished"

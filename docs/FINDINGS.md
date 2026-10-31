# Findings: what stops KOReader on this device, and what fixed it

Measured with small test programs (`tools/probes/`) on a PocketBook Pro 902, firmware `D902.2.1.3`, unless marked *assumed*.

## Hardware / system
- Linux 2.6.29.6 `armv6l`, glibc 2.6.1, about 250 MB RAM. `/proc/cpuinfo`: ARM1176, features `swp half thumb fastmult edsp java`
  (**no `vfp`, no `neon`**). Instruction test (`probe8`): ARMv6 instructions run; `movw/movt` (ARMv7), VFP and NEON raise SIGILL.
- Official KOReader PocketBook builds target ARMv7 (Cortex-A8, NEON) -> "Illegal instruction".
- EABI5 binaries from the koxtoolchain "kindle" (legacy) toolchain run fine, although the firmware's own programs are EABI4.
- Kernel/ld: the device exposes no `/hwevent` message queue, so KOReader's InkView loop (not its "raw input" mode) is used.

## The legacy toolchain emits VFP
The prebuilt `arm-kindle-linux-gnueabi` toolchain defaults to `-march=armv6j+fp -mfpu=vfp -mfloat-abi=softfp`.
Its `libstdc++.so.6` (bundled by KOReader) contains VFP instructions even for plain 64-bit copies.
Loading it on the 902 crashes in `std::locale::_Impl::_Impl` with `vldr d7,[pc,...]` (found with `tools/probes/sigprobe.c`).
Fix: rebuild the same toolchain as pure soft-float ARM1176 (`soften-config.sh` edits the crosstool-NG sample config).
`scan_vfp.sh` scans the build output for such instructions (it can report false positives from data inside code).

## InkView (firmware library `libinkview.so`)
- `GetSoftwareVersion()` returns `D902.2.1.3`, which KOReader reads as 201 and rejects. The 5.05 definitions are used instead.
  They declare `SetSubtaskInfo(int, int, char *, char *)` (non-const), so LuaJIT refuses Lua strings: cast explicitly.
- Events seen: 21 init, 22 exit, 23 show, 24 hide, 25/26 key press/release, 32 orientation (tilt), 151/152 foreground/background.
- Keys: 17-20 arrows, 10 OK, 24/25 page prev/next, 23 menu (book key), 27 back, 6/7 zoom, 21 volume. Home is handled by the firmware.
- Framebuffer: 828x1200, 8 bit gray, scanline 828; `GetTaskFramebuffer(GetCurrentTask())` works; direct pixel writes plus
  `FullUpdate` / `PartialUpdate` / `DynamicUpdate` work.
- KOReader's compat start-up (`OpenScreen()` in the main thread, then `InkViewMain` in a thread with a weak timer) works.
  Calling `OpenScreen()` from inside the first event handler crashed (probes 2 and 3).
- Missing exports: `WiFiPower`, `NetMgrPing`, frontlight and g-sensor calls, `PartialUpdateHQ`, `WaitForUpdateComplete`, `adjustAreaDefault`.

## Network
Switching Wi-Fi on in the settings does not bring the link up for an application; `NetConnect(NULL)` does (as in the stock browser).
KOReader's `QueryNetwork()`-based check did not match this firmware (*assumed*: different flag values), so the patch uses the routing table
and implements `turnOnWifi` / `restoreWifiAsync` with `NetConnect`.

## Files, associations, logos
- Associated viewers are looked up as `system/bin/<app>` (KOReader's package ships a forwarding `system/bin/koreader.app`);
  the user table is `system/config/extensions.cfg`, format as in `/ebrmain/config/extensions.cfg`.
- Start with "last page" uses `system/state/lastopen.txt` (path plus newline).
- Logo files: `.bmp`, 8-bit grayscale, 824x1200, in `system/logo/`.
- Build quirk: texinfo 7 crashes on glibc 2.9's manual while building the toolchain (`makeinfo-wrapper.sh`).

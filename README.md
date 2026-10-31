# KOReader for the PocketBook Pro 902 (firmware 2.x) - experimental

A build kit and a few small patches that make [KOReader](https://github.com/koreader/koreader) run on the **PocketBook Pro 902**
(2010, ARM1176 CPU, no VFP/NEON, firmware 2.x). The official KOReader builds do not start on this device.

> Status: works on **one** device (firmware `D902.2.1.3`). Experimental, use at your own risk.
> Not affiliated with KOReader, PocketBook or Obreey. Developed with the help of an AI assistant (Claude).

## What works (tested on the device)

- Reading EPUB, file browser, KOReader menus
- Hardware buttons: arrows, OK, page keys, book/menu key, back (Home belongs to the firmware and leaves KOReader)
- Wi-Fi on demand (connects through the firmware when KOReader needs the network, and can disconnect afterwards)
- OPDS catalogs (tested with a Kavita and an Audiobookshelf server on the local network)
- The [Hardcover](https://github.com/billiam/hardcoverapp.koplugin) plugin (progress sync)
- "Open with" from the library, and "Last page" at power-on opens KOReader (needs the file-association step in `docs/INSTALL.md`)

## Known issues / not tested

- File browser: after scrolling, thin black "focus" underlines of earlier selections can stay on the screen (e-ink partial refresh).
- Starting through "Open with" takes a few seconds longer than starting KOReader from the Applications menu.
- Not tested: PDF, DjVu, CBZ, long reading sessions, battery life, sleep/standby behaviour.
- Not supported by this hardware: touch, frontlight. Tilt sensor / auto-rotate is not implemented.
- Wi-Fi: connecting blocks the screen for a few seconds.

## Install

See [docs/INSTALL.md](docs/INSTALL.md). If a release zip exists on the Releases page, use that.

## Build it yourself

Needs Linux x86_64 with Docker, about 25 GB of free disk and an internet connection.

    bash build.sh

It (1) builds a **soft-float** variant of the legacy ARMv6 toolchain with the official koxtoolchain recipe (once),
(2) builds KOReader at the pinned commit (`KOREADER_COMMIT`) with the patches from `patches/`, and
(3) scans the result for VFP/NEON/ARMv7 instructions the CPU cannot run. Result: `out/device/`.

## What the patches do

| Patch | Purpose |
|---|---|
| `0001-base-...` | `pocketbook` target compiles for ARMv6, soft-float, ARM mode; defaults to the legacy toolchain; treats firmware version `2.x` as the oldest known InkView definitions (5.05) |
| `0002-frontend-...` | adds a `PocketBook 902` device; fixes `SetSubtaskInfo` (`char *` vs `const char *`); Wi-Fi handling for this firmware (route check, `NetConnect` for on-demand Wi-Fi and Wi-Fi restore); keeps the firmware's `lastopen.txt` in sync |
| `0003-platform-...` | launcher: explicit `PATH`, small launch log, safer "already running" check |

Why these were needed is written down in [docs/FINDINGS.md](docs/FINDINGS.md).
`tools/` contains the small test programs and diagnostics that were used to find out what the device can do.

## Credits

[KOReader](https://github.com/koreader/koreader) and [koxtoolchain](https://github.com/koreader/koxtoolchain) (crosstool-NG based),
and the MobileRead community for decades of PocketBook documentation.

## License

AGPL-3.0, like KOReader (see `LICENSE`). The patches are changes to KOReader. Third-party libraries bundled in a
KOReader build keep their own licenses (see the notices inside the `koreader` folder).

# Installing on the PocketBook Pro 902

Tested only on firmware `D902.2.1.3`. Back up anything important first. Everything below can be undone by deleting the files again.

## 1. Copy the files (USB)

From the release zip or from `out/device/` after a build, copy to the e-reader's main folder:

    applications/koreader/           (the whole folder, about 200-300 MB)
    applications/koreader.app
    system/bin/koreader.app          (tiny forwarding launcher; needed for "Open with" and for the last-page start-up)

Eject, then start **KOReader** from *Applications*.

## 2. Wi-Fi

In KOReader's network settings set *Action when Wi-Fi is off* to **Turn on** and *Action when done* to **Turn off** (or "Leave on").
For the Hardcover plugin enable *Enable wifi on demand*.

## 3. Optional: make KOReader the default for books ("Open with" and power-on)

The firmware reads `system/config/extensions.cfg`. Copy your existing file to a safe place first. Each line is
`extension:@TYPE_file:1:app1,app2,...:ICON_...` and the **first app is the default**. Example (add `koreader.app` first):

    epub:@EPUB_file:1:koreader.app,AdobeViewer.app,fbreader.app:ICON_EPUB
    pdf:@PDF_file:1:koreader.app,AdobeViewer.app,pdfviewer.app:ICON_PDF
    djvu:@DJVU_file:1:koreader.app,djviewer.app:ICON_DJVU

Use Unix line endings. Then set the firmware option *start with* to **last page**. KOReader writes
`system/state/lastopen.txt`, so power-on reopens the book you read last.
To undo: restore your copy of `extensions.cfg`.

## 3b. Optional: book cover as power-off logo

The firmware lists logo files from `system/logo/` that are **8-bit grayscale `.bmp`**, 824x1200 on this model.
`tools/make_logo.py picture.jpg --dither` converts any picture.

## Removing it

Delete `applications/koreader`, `applications/koreader.app`, `system/bin/koreader.app`, and restore `extensions.cfg`.

Rubik + Onest typography
=======================

Rubik (300, 400, 500, 600): interface labels, buttons, headings and clocks.
Onest (400): notes, explanatory copy, file paths and search descriptions.
Full Cyrillic and Latin glyph sets are retained, without subsetting.

Sources retrieved 2026-10-18:
- https://raw.githubusercontent.com/google/fonts/main/ofl/rubik/Rubik%5Bwght%5D.ttf
- https://raw.githubusercontent.com/google/fonts/main/ofl/onest/Onest%5Bwght%5D.ttf

The exact source binaries are retained in third_party/fonts; source and output
SHA-256 hashes are recorded in manifest.json. The static instances are produced
by scripts/prepare_fonts.py with fontTools 4.60.1 (a development dependency only).
Normal application builds use the committed static TTFs; no font download or
font processing is required. The application embeds these five TTFs as RCDATA.

The process privately registers the same bytes with GDI and a custom DirectWrite
collection. Font data is read from mapped resources and text formats are cached
by family, weight and size. No global installation or extra service is used.
The private collection outlives all windows and text formats. Guardian/index
helper paths do not construct Application and do not register these fonts.

SIL OFL 1.1 licenses and author notices are in ../Rubik-OFL.txt and ../Onest-OFL.txt;
both are included in portable releases and the installer.

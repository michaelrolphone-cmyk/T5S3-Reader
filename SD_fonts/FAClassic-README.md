# Font Awesome Classic SD font set

This directory contains SD-card `.cpfont` families generated from the official Font Awesome 7 Free Classic desktop fonts.

Generated families:

- `FAClassicSolid` — source: Font Awesome 7 Free Solid 900
- `FAClassicRegular` — source: Font Awesome 7 Free Regular 400

Each family is generated at 12, 14, 16, and 18 px using `lib/EpdFont/scripts/fontconvert_sdcard.py`, matching the existing SD font layout in this repository.

The converter is given the full BMP (`U+0000`–`U+FFFF`). It validates the source font cmap and writes only codepoints actually present in the source face, which captures both Font Awesome Private Use Area icon codepoints and any standard Unicode mappings without filling gaps.

For each family, the build also generates a `*_codepoints.csv` lookup table containing the exact Unicode codepoint and glyph name from the upstream font cmap.

## Install on SD card

Copy either generated family folder into one of the reader font roots:

- `/.fonts/FAClassicSolid/`
- `/.fonts/FAClassicRegular/`

or use the visible `/fonts/` root instead.

## Rebuilding

The workflow `.github/workflows/build-fa-classic-sd-fonts.yml` downloads the upstream fonts from the pinned Font Awesome commit and regenerates the files. It also runs automatically if the SD font converter changes.

Upstream source commit: `14c65a3747d0f3b751f15831fc719236aea8729d`

## Licensing

Font Awesome Free desktop/web fonts are licensed under the SIL Open Font License 1.1. The generated `.cpfont` files are renamed `FAClassicSolid` and `FAClassicRegular` because `Font Awesome` is a Reserved Font Name under that license. See `SD_fonts/FAClassic-LICENSE.txt` for the upstream license text and attribution.

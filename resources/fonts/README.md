# OLED font source

Noto Sans SemiBold, release **v2.015**, comes from the official
[Noto Latin/Greek/Cyrillic release](https://github.com/notofonts/latin-greek-cyrillic/releases/tag/NotoSans-v2.015).
The original `NotoSans-SemiBold.ttf` is retained unchanged with its accompanying
SIL Open Font License in [OFL.txt](OFL.txt).

SHA-256: `a7af8d4a11e8e49fea9a19786a6469210b512cd4061413e8036bdd2425722795`.

`tools/generate_oled_fonts.mjs` uses pinned `lv_font_conv@1.5.3` to produce
10, 14 and 20 pixel, uncompressed 1-bit LVGL fonts in a caller-selected build
directory. It seeds supported Latin, combining mark, Greek and Cyrillic blocks
independently of catalog contents and rejects missing additional catalog glyphs.
Combining glyph availability is not a claim of arbitrary shaping or RTL support.
Existing FontAwesome icon fonts remain separate from product text fonts.

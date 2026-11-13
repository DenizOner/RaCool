# Product translation catalogs

Each UTF-8 JSON file contains `locale`, its native `name`, and a `messages` object.
The filename must match the locale, for example `bg.json` with `"locale": "bg"`.
Copy all message keys from `en.json`; use semantic English keys and translate only
their values. English and Turkish remain required baseline catalogs.

Parameterized CLI output uses `LOC_PRINTF` with a literal English format reference.
The generator checks every such source reference against the English catalog,
then checks all other languages against that contract. The macro also retains
compiler checks of argument types; changing every catalog together cannot silently
change a C call's parameter contract. Print non-parameterized text with a `%s`
wrapper rather than treating arbitrary text as a printf format.

The generator discovers every JSON file in this directory. Adding a catalog needs
a firmware rebuild and upload; changing the selected installed language will not
need a restart. OLED and CLI product text use the catalogs; technical SDK logs,
source identifiers and versioned machine records remain English. A selected locale
is shared by OLED and human CLI responses. Final physical layout acceptance is
tracked separately and is not implied by catalog validation.

Printf messages must preserve parameter order, conversion and length modifiers.
Use `%%` for a literal percent sign. `%n`, positional parameters and dynamic
width/precision are rejected. Duplicate/missing/extra keys, invalid UTF-8, NUL and
oversized messages fail generation. File ordering is never a persisted identity;
the configuration stores the locale code.

The font generator seeds broad Latin, Greek and Cyrillic coverage independently
of catalogs, then validates additional catalog characters against the pinned font.
Unsupported glyphs fail the build; glyph coverage alone does not provide RTL or
complex shaping. Keep product messages short enough for the 128×64 OLED; bitmap
availability alone does not establish that a label fits its allocated rectangle.
Catalog text is normalized to Unicode NFC before emission, so common decomposed
Latin/Cyrillic accents use their precomposed glyphs rather than depending on shaping.

Generated C/header/font files belong in the individual build directory. Do not edit
them or commit generated resources instead of the source catalogs.

Versioned `RACOOL_CLI v=1` records are a machine contract, not translated text.
Live helpers must parse those records and distinguish queue admission, owner
readback, SDK response and physical observation. Capture one immutable catalog
snapshot per response/frame; asynchronous responses take their own snapshot.

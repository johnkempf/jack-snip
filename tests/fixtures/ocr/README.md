These unmodified screen crops were supplied as OCR bug reproductions. All five
returned no text when passed directly to Windows OCR without preparation.

`tests/text_capture.cpp` checks exact Unicode clipboard output through the actual
area-selection completion path, including punctuation and identifier digits:

| Fixture | Expected text |
| --- | --- |
| part-number.png | RSP-241492-01 |
| serialized.png | Serialized: |
| serial-number.png | 1301558 |
| marked-empty.png | Marked Empty |
| parcels-area.png | 683: Empty Parcels Area |
| hyphenated-part.png | APF6-037-01-04-RA |
| clipped-date-row.png | 3/31/2025 2:38 PM (clipped first row omitted) |
| quantity-and-zero.png | CurrentQuantity (isolated zero is outside the accuracy requirement) |

`quoted-unit.png` is retained as a known Windows OCR quality limitation. Its
target is `Each’ (‘EA’)`; the test reports the actual result without asserting
that the built-in engine can reproduce the exact punctuation. The bundled OCR
branch retains the nine exact-output assertions, including the zero and quotes.

Run on Windows with an English OCR recognition language installed. Tests use a
private desktop and clipboard, preserving the user's clipboard.

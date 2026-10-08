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

Run on Windows with an English OCR recognition language installed. Tests use a
private desktop and clipboard, preserving the user's clipboard.

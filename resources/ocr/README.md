The embedded English model is `eng.traineddata` from the official Tesseract
`tessdata_fast` repository:

https://github.com/tesseract-ocr/tessdata_fast
https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/eng.traineddata

SHA-256: `7D4322BD2A7749724879683FC3912CB542F19906C83BCC1A52132556427170B2`

License: Apache 2.0, reproduced in `../licenses/Tesseract.txt`.
The checked-in bytes, rather than the moving upstream branch, are used in builds.
Model initialization reads RCDATA resource 204 directly from the executable;
no model files are extracted to the user's disk.

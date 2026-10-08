# Bundled OCR components

Review date: October 8, 2026. This is an implementation inventory and a limited
advisory review, not an IT approval or a complete security audit.

| Component | Version | Role | License |
| --- | --- | --- | --- |
| Tesseract | 5.5.3 | Recognizes compact English text rows | Apache 2.0 |
| Leptonica | 1.87.0 | Image processing used by Tesseract | BSD |
| tessdata_fast English model | Fixed checked-in bytes | Recognition weights and alphabet | Apache 2.0 |

The model SHA-256 is
`7D4322BD2A7749724879683FC3912CB542F19906C83BCC1A52132556427170B2`.
Its source and license are recorded in `resources/ocr/README.md`.

## Runtime behavior

The two libraries are statically linked into Tiger Snip. The model is embedded
in executable resource 204 and initialized directly from those bytes. There is
no external model selection, download, separate service, or OCR installation.
The recognition API receives RGB pixels from the selected screenshot. Text is
copied to the ordinary Windows clipboard; operating-system clipboard history
and synchronization remain subject to the user's Windows settings.

The OCR build disables networking support, training tools, the legacy OCR
engine, and external image codecs. Larger passages and other Windows UI
languages still use Windows OCR. Recognition runs in the app process with the
user's permissions, so a native-library defect could still affect that process.
Offline operation limits data exposure but does not eliminate software defects.

## Known advisories and scope

Tesseract's official advisory
[GHSA-q44c-23p6-5mw6 / CVE-2026-88048](https://github.com/tesseract-ocr/tesseract/security/advisories/GHSA-q44c-23p6-5mw6)
lists versions through 5.5.3 as affected, with no patched version listed at this
review. It describes memory corruption triggered by a malicious recognition
model and lists trusted models as a workaround. Tiger Snip uses the fixed
official model above and exposes no path for loading user-supplied models.
This restricts that attack path; it is not a claim that the library is free of
vulnerabilities. A component scanner can still flag the bundled version.

[GHSA-5j2p-r5vc-q7f3](https://github.com/tesseract-ocr/tesseract/security/advisories/GHSA-5j2p-r5vc-q7f3)
describes a model-loader overflow in the legacy engine. That engine is excluded
from our build. Other advisories and future defects need separate assessment.

Official advisory pages:
- https://github.com/tesseract-ocr/tesseract/security/advisories
- https://github.com/DanBloomberg/leptonica/security/advisories

## Updates and deployment

These embedded components are not updated by Windows Update or by an independent
Tesseract installation. A maintainer must review upstream advisories/releases,
change the pinned versions and archive hashes in `cmake/OcrDependencies.cmake`,
rebuild, run the OCR/regression tests, and distribute a new EXE/MSI. Model changes
also require updating the checked-in bytes, provenance/hash, and accuracy tests.
There is no automatic updater or scheduled advisory monitor configured.

Review advisories before each release and periodically during deployment;
prioritize a security fix when its vulnerable path is reachable in this app.
Security updates should not wait for visible OCR accuracy problems.

Source archives use fixed SHA-256 values. `Tiger Snip Build.json` records source,
model, library, compiler, and executable hashes; the release record includes
installer/payload hashes. Notices ship in the installer. These provide provenance
and integrity evidence, not a security certification or a publisher signature.
The current EXE/MSI are unsigned. IT should review the component inventory,
advisory applicability, maintenance ownership, signing, and deployment policy
before company-wide distribution. No IT approval is implied by local tests.

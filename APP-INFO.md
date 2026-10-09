# Tiger Snip application information

Tiger Snip is a Windows screenshot tool for capturing, annotating, copying, and saving images. It runs locally and supports multiple monitors, text, drawing tools, shapes, cropping, undo/redo, and ten recent captures.

## Installation

Open **Tiger Snip Setup.msi** and click **Install**. Setup confirms successful installation and offers **Launch Tiger Snip**, checked by default. Click **Finish** to open the app, or clear the checkbox to launch it later from the Start menu. Silent/basic-UI installations, repairs, and removals do not launch the app. The installer can be distributed through a shared folder; each user installs a local copy in `%LOCALAPPDATA%\Programs\Tiger Snip`.

Settings are saved in `%LOCALAPPDATA%\Tiger Snip\TigerSnip.ini`. Startup at sign-in is optional. Remove the app through Windows Installed apps. To replace the current 1.0.2 package, close the app, uninstall the previous copy, and install the replacement. Settings remain.

## Data storage

Screenshots and edits stay in memory while Tiger Snip is running. Closing the window keeps it in the tray; File > Exit clears the recent captures. Copy sends an image to the Windows clipboard, and Save writes a PNG or JPG to the chosen location. Choose the format in Settings > Export; JPG disables Professional Border, blur, and rounded corners. PNG restores the previous border choices. Tiger Snip has no networking or telemetry functionality.

Save As also offers PNG and JPG. Its format choice applies to that file without
changing Settings; JPG omits border effects and PNG uses the remembered choices.

Copy text recognizes selected pixels locally and sends Unicode text to the
Windows clipboard. Recognition uses the built-in Windows OCR engine and installed
recognition languages. No captures are uploaded or saved by text recognition. A brief popup previews
the copied text.

## Support

Jack Kempf — jack.kempf@samtec.com.

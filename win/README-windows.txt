tinydaw for Windows
===================

Unzip the folder anywhere and run tinydaw.exe. Nothing is installed; keep the DLLs and
DejaVuSansMono.ttf next to the program.

Where things go
  Projects and exports   your Music folder (Music\tinydaw for projects)
  Pad bindings           %APPDATA%\tinydaw\pads.conf
  Autosave               %LOCALAPPDATA%\tinydaw\autosave.tdaw

MIDI devices are found when they're plugged in (tinydaw checks every two seconds). Windows
lets only one program use a MIDI device at a time: close other music software first if a
keyboard doesn't show up in the Pads pane.

The Singer (Sounds > Singer) uses eSpeak NG, a free speech program that comes in the
espeak-ng folder. Keep that folder next to tinydaw.exe; nothing else needs installing.

To open .tdaw files by double-clicking, choose "Open with" > tinydaw.exe once and tick
"Always use this app".

From a command prompt the Linux options work the same, for example:
  tinydaw.exe song.tdaw --export song.flac
  tinydaw.exe --selftest

Licences for the bundled libraries and font are in the licenses folder.

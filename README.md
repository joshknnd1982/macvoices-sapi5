# macvoices-sapi5

The classic macOS text-to-speech voices — **Alex**, Vicki, Fred, Zarvox and the rest of
the Mac OS X 10.5 Leopard voice set, **24 voices in all** — as native Windows SAPI 5
voices, for NVDA, Narrator, and any other program that speaks through SAPI 5.

These are Apple's original MacinTalk voices, running on Windows through a host process
that loads Apple's own i386 engine directly — not a recreation, not a port of the
synthesis: the genuine voices, byte for byte.

## There is no voice data in this repository

This repository is **source code only**. Apple's speech engine and voice files are not
here and never will be.

**To get the voices, download the setup installer from the
[Releases](../../releases) page and run it.** The installer sets up everything: all 24
voices (Alex's full 670 MB voice bank included), the 32-bit and 64-bit SAPI 5
interfaces, and the settings utility, with a desktop shortcut. After installing, pick a
voice ending in "(Classic Mac)" in your screen reader or in Windows speech settings.

## The voices

All 24 speak US English:

* **Alex** and **Vicki** — the natural concatenative voices
* **Agnes, Bruce, Victoria** — MacinTalk Pro
* **Albert, Bad News, Bahh, Bells, Boing, Bubbles, Cellos, Deranged, Fred, Good News,
  Hysterical, Junior, Kathy, Pipe Organ, Princess, Ralph, Trinoids, Whisper, Zarvox** —
  MacinTalk 3, including the singing voices

## Settings

The **Classic Mac Voices settings** utility (desktop shortcut) adjusts voice, rate,
rate boost, pitch, volume, inflection, embedded speech commands, the gap between
announcement parts, engine phrase breaks, breathing between sentences, number reading,
abbreviation expansion, and the fix for words the engine stresses wrongly. Every change
takes effect immediately in whatever is speaking, and settings persist in
`%APPDATA%\ClassicMacVoices\settings.ini` — a plain file, not the registry. Everything
is screen-reader accessible: labelled standard controls, all in the tab order.

## Building from source

Requirements: Visual Studio 2022 Build Tools (C++ x86 and x64), CMake 3.20+, Inno
Setup 6 — plus the engine data this repo does not carry: put `panthera_host.exe` (from
the panthera NVDA add-on family) in `bin\_panthera\`, and an extracted Leopard speech
tree (from your own Mac OS X 10.5 install disc) in `bin\leopard\`. Then:

    powershell -ExecutionPolicy Bypass -File tools\build_all.ps1

builds both architectures, runs from `output\`, and compiles the installer into
`dist\`. `cmv_directtest.exe` exercises every voice through the DLL with no
registration and no administrator rights; `cmv_textproc_test.exe` checks the text
pipeline against the behaviour measured in the NVDA driver this project derives from.

## Logs

Every component writes to `%LOCALAPPDATA%\ClassicMacVoices\Logs`, and the installer
saves its own log as `install.log` beside the program files. `CLASSICMAC_LOG_LEVEL`
(off/error/warn/info/debug/trace) and `CLASSICMAC_LOG_DIR` override the defaults.

## Credits

The engine host and the measured voice behaviour come from the panthera NVDA add-on
family, which first brought these voices to Windows for NVDA. The voices themselves
are Apple's, from Mac OS X 10.5 Leopard.

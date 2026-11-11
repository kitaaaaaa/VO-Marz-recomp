# VO-Marz-recomp
Recomp project for Cybertroopers Virtual-On MARZ MSBS Ver. 8.5

## Playing

Windows x64 only. You need a disc image (.iso) of your own copy of the Japanese release, Dennou Senki Virtual-On
Marz (SLPM-65303). This repository holds no game material: the game is recompiled from your disc on your PC.

1. Download or clone this repository to a drive with about 12 GB free.
2. Run `setup.bat`, then pick your .iso (or drop the .iso on `setup.bat`).
3. When it finishes, the game starts. From then on, start it with `Marz.exe` (Japanese), or `Marz.exe --en` (English).

`setup.bat` installs its tools into `toolchain\` inside the repository (MSYS2 with clang, a JDK and Ghidra), so
nothing is installed system-wide, and deletes that folder again when it has finished. Next it unpacks your disc into `marz\iso_jp` and writes the English game folder
`marz\iso_en`. Then it analyses the game program with Ghidra and recompiles and builds it (about 20 to 30 minutes,
longer on slower PCs). It downloads about 1.1 GB and needs about 12 GB while it works; afterwards
the game takes about 7 GB. Run `setup.bat` again after updating the repository
to rebuild (it downloads the tools again).

- Saves are kept in `marz\iso_jp\mc0` and `marz\iso_en\mc0` (one memory card per language).
- Keep the .iso where it was: the game still reads from it.
- Display defaults: 3x internal resolution, 16:9, flicker filter and dither off. `Marz.exe` options:
  - `--en`: the English game folder
  - `--scale=N`: internal resolution, N times the PS2's (for example `--scale=2`)
  - `--widescreen=0`: 4:3 (`--widescreen=1`: 16:9)
  - `--ot-handling=1`: handling closer to Virtual-On Oratorio Tangram (MSBS 5.66) for the human players' robots
    (`--ot-handling=0`, the default: Marz's own)

  For example, `Marz.exe --en --scale=4 --widescreen=0`. The flicker filter and dither can be changed with the
  environment variables `PS2X_GS_FLICKER_FILTER` and `PS2X_GS_DITHER`.
- The game's output goes to `marz\logs\run_play.log`; the build logs are in `marz\logs\`.

## English translation

**The English translation is incomplete and has not been proofread.** It lives in `marz/translation/en/` and
covers the Dramatic Mode story text, the database and the text strings in the game program. The chapter titles,
the HUD, the menu help lines and other text stored as images or in packed files are still Japanese. Names of VRs,
characters and places in particular still need checking.

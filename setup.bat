@echo off
rem One-click setup: installs the build tools into toolchain\, builds the game from your own Virtual-On Marz
rem (Japan, SLPM-65303) disc image, writes Marz.exe and starts it. Drop the .iso on this file, or pick it when asked.
rem Run it again after updating the repository to rebuild. Afterwards start the game with Marz.exe (Marz.exe --en: English).
setlocal
cd /d "%~dp0"
set "TC=%~dp0toolchain"
set "BASH=%TC%\msys64\usr\bin\bash.exe"
set "MSYS2_URL=https://github.com/msys2/msys2-installer/releases/download/2026-09-27/msys2-base-x86_64-20260927.sfx.exe"
set "MSYS2_SHA=ad336cccfda47758b5e15cda993fbba421115cb0b126697daef1ee4dfe37209f"

set "ISO=%~1"
if not defined ISO for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = 'Your Virtual-On Marz disc image'; $d.Filter = 'Disc image (*.iso)|*.iso'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }"`) do set "ISO=%%i"
if not defined ISO (echo No disc image chosen. & goto fail)
rem Not in a ( ) block: a ")" in the path, as in "(Japan)", would end the block.
if not exist "%ISO%" goto noiso

if not exist "%BASH%" (
    echo Downloading MSYS2...
    if not exist "%TC%\downloads" mkdir "%TC%\downloads"
    curl -L --fail -o "%TC%\downloads\msys2-base.sfx.exe" "%MSYS2_URL%" || goto fail
    powershell -NoProfile -Command "if ((Get-FileHash '%TC%\downloads\msys2-base.sfx.exe' -Algorithm SHA256).Hash -ne '%MSYS2_SHA%') { exit 1 }" || (echo MSYS2 download checksum mismatch & goto fail)
    "%TC%\downloads\msys2-base.sfx.exe" -y -o"%TC%" || goto fail
    rem The first login creates the user's home and the pacman keyring.
    "%BASH%" -lc "exit 0" || goto fail
)
set MSYSTEM=CLANG64
set CHERE_INVOKING=1
echo Installing build packages...
rem Twice: the first update may replace pacman and the MSYS2 runtime themselves.
"%BASH%" -lc "pacman -Syu --noconfirm" || goto fail
"%BASH%" -lc "pacman -Syu --noconfirm" || goto fail
"%BASH%" -lc "pacman -S --needed --noconfirm git rsync unzip mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-lld mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-ffmpeg" || goto fail
"%BASH%" -l marz/tools/setup.sh "%ISO%" || goto fail
rem The game no longer needs the tools: marz\bin has the runner and its DLLs. A later setup.bat downloads them again.
echo Deleting the build tools (toolchain\)...
rmdir /s /q "%TC%"
echo.
echo Done. Start the game with Marz.exe (Japanese) or "Marz.exe --en" (English).
start "" "%~dp0Marz.exe"
exit /b 0

:noiso
echo Not found: "%ISO%"
:fail
echo.
echo Setup stopped. See the messages above (and marz\logs\ for the build logs).
pause
exit /b 1

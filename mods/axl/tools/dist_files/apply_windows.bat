@echo off
rem Axl mod (Mega Man X Legacy Collection 2): patch the game's 5 archives, keep <name>.orig, copy dinput8.dll.
rem
rem   Put xdelta3.exe (https://github.com/jmacd/xdelta-gpl/releases) next to this file, then
rem   double-click this file          the game folder found through Steam (registry, library folders)
rem   apply_windows.bat "<game folder>"  that folder (steamapps\common\Mega Man X Legacy Collection 2)
rem
rem Each archive is checked against SHA1SUMS.txt before (the shipped file) and after (the patched one); an archive
rem already patched is left alone, so running it again is safe. Paths are only used inside quotes (spaces and the
rem parentheses of "Program Files (x86)" are fine).
setlocal EnableExtensions DisableDelayedExpansion
set "HERE=%~dp0"
set "GAME=%~1"
set "SUB=steamapps\common\Mega Man X Legacy Collection 2"
set "RC=1"

if defined GAME goto have_game
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find /i "SteamPath"') do set "STEAM=%%b"
if not defined STEAM goto no_game
set "STEAM=%STEAM:/=\%"
if exist "%STEAM%\%SUB%\RXC2.exe" set "GAME=%STEAM%\%SUB%"
if defined GAME goto have_game
if not exist "%STEAM%\steamapps\libraryfolders.vdf" goto no_game
for /f usebackq^ tokens^=4^ delims^=^" %%p in (`findstr /c:"\"path\"" "%STEAM%\steamapps\libraryfolders.vdf"`) do call :try_lib "%%p"
if defined GAME goto have_game
goto no_game

:have_game
echo game folder: "%GAME%"
if not exist "%GAME%\RXC2.exe" goto bad_game
if not exist "%HERE%xdelta3.exe" goto no_xdelta
set "ROM=%GAME%\nativeDX10\X6\romPC"
for /f "usebackq tokens=1,2,3" %%a in ("%HERE%SHA1SUMS.txt") do call :one "%%a" "%%b" "%%c" || goto failed
copy /y "%HERE%dinput8.dll" "%GAME%\dinput8.dll" >nul || goto failed
echo done.
set "RC=0"
goto end

:no_game
echo The game folder was not found. Run: apply_windows.bat "game folder"
goto end
:bad_game
echo Not the game folder (no RXC2.exe):
echo "%GAME%"
goto end
:no_xdelta
echo xdelta3.exe not found next to this file. Or use Delta Patcher (README.md).
goto end
:failed
echo Stopped. Steam: Verify integrity of game files, then run this again.
goto end

:end
if "%~1"=="" pause
exit /b %RC%

rem ---- one library folder of libraryfolders.vdf (its \\ doubled backslashes are fine for Windows) ----
:try_lib
if defined GAME exit /b 0
if exist "%~1\%SUB%\RXC2.exe" set "GAME=%~1\%SUB%"
exit /b 0

rem ---- one archive: %1 shipped SHA-1, %2 patched SHA-1, %3 name ----
:one
set "OLD=%~1"
set "NEW=%~2"
set "NAME=%~3"
set "F=%ROM%\%NAME%"
if not exist "%F%" goto one_missing
call :sha1 "%F%"
if /i "%SHA%"=="%NEW%" goto one_done_before
if /i not "%SHA%"=="%OLD%" goto one_unexpected
copy /y "%F%" "%F%.orig" >nul || exit /b 1
"%HERE%xdelta3.exe" -d -f -s "%F%.orig" "%HERE%patches\%NAME%.xdelta" "%F%.new" || exit /b 1
call :sha1 "%F%.new"
if /i not "%SHA%"=="%NEW%" goto one_wrong
move /y "%F%.new" "%F%" >nul || exit /b 1
echo %NAME%: patched
exit /b 0
:one_done_before
echo %NAME%: already patched
exit /b 0
:one_missing
echo %NAME%: missing in the game folder
exit /b 1
:one_unexpected
echo %NAME%: not the expected file
exit /b 1
:one_wrong
del "%F%.new" >nul 2>&1
echo %NAME%: the patched file is wrong
exit /b 1

rem ---- SHA = the SHA-1 of %1 (certutil; older Windows prints it with spaces between the bytes) ----
:sha1
set "SHA="
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%~1" SHA1') do if not defined SHA set "SHA=%%h"
if defined SHA set "SHA=%SHA: =%"
exit /b 0

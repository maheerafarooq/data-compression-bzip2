@echo off
set "MSYS2=C:\msys64"
if not exist "%MSYS2%\usr\bin\bash.exe" (
  echo MSYS2 not found at %MSYS2%. Install from https://www.msys2.org/ then in UCRT64:
  echo   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
  exit /b 1
)
cd /d "%~dp0"
"%MSYS2%\usr\bin\bash.exe" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; cd \"$(cygpath -u \"$1\")\" && mingw32-make" _ "%CD%"
exit /b %ERRORLEVEL%

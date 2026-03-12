@echo off

if "%1"=="" goto usage
if "%2"=="" goto usage
if "%3"=="386" goto do386

echo PyDOS Compiler (8086): %1 -> %2.EXE
bin\PYDOS.EXE %1 -o %2.asm %4 %5
if errorlevel 1 goto fail
wasm -0 -ml -d0 %2.asm
if errorlevel 1 goto fail
echo system dos >%2.lnk
echo option stack=32768 >>%2.lnk
REM echo option map >>%2.lnk
echo option dosseg >>%2.lnk
echo option eliminate >>%2.lnk
echo name %2.EXE >>%2.lnk
echo file %2.obj >>%2.lnk
echo library lib\PYDOSRT.LIB >>%2.lnk
echo library clibl >>%2.lnk
echo library emu87 >>%2.lnk
wlink @%2.lnk > %2.ERR
if errorlevel 1 goto fail
echo Success: %2.EXE created.
if exist %2.ERR del %2.ERR
goto end

:do386
echo PyDOS Compiler (386): %1 -> %2.EXE
bin\PYDOS.EXE %1 -o %2.asm -t 386 %4 %5
if errorlevel 1 goto fail
wasm -3 -mf -d0 %2.asm
if errorlevel 1 goto fail
REM echo system dos4g >%2.lnk
echo system causeway >%2.lnk
echo option stack=65536 >>%2.lnk
REM echo option map >>%2.lnk
echo option dosseg >>%2.lnk
echo option eliminate >>%2.lnk
echo name %2.EXE >>%2.lnk
echo file %2.obj >>%2.lnk
echo library lib\PDOS32RT.LIB >>%2.lnk
echo library clib3s >>%2.lnk
wlink @%2.lnk > %2.ERR
if errorlevel 1 goto fail
echo Success: %2.EXE created (DOS/4GW).
if exist %2.ERR del %2.ERR
goto end

:usage
echo Usage: pydc.bat input.py outputname [386]
goto end

:fail
echo Compilation FAILED!

:end
if exist %2.lnk del %2.lnk

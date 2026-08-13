@echo off

if "%1"=="" goto usage
if exist _TESTOK del _TESTOK

rem tests\NAME.386 marks a 386-only test (native code exceeds the 8086
rem 640 KB budget); skip it but keep the suite green on 8086.
if exist tests\%1.nodos goto skipnodos
if not "%2"=="386" if exist tests\%1.386 goto skip386

call pydc.bat tests\%1.py %1 %2 %3 %4
if errorlevel 1 goto cfail
%1.EXE > tests\%1.out
fc /l tests\%1.exp tests\%1.out > nul
if errorlevel 1 goto mismatch

echo PASS %1
echo ok > _TESTOK
if exist %1.asm del %1.asm
if exist %1.obj del %1.obj
if exist %1.EXE del %1.EXE
if exist tests\%1.out del tests\%1.out
goto end

:cfail
echo FAIL %1 (compile error)
REM if exist %1.asm del %1.asm
if exist %1.obj del %1.obj
goto end

:mismatch
echo FAIL %1 (output mismatch)
fc /l tests\%1.exp tests\%1.out
REM if exist %1.asm del %1.asm
if exist %1.obj del %1.obj
if exist %1.EXE del %1.EXE
goto end

:usage
echo Usage: runone.bat testname [386]
goto end

:skip386
echo SKIP %1 (386-only)
echo ok > _TESTOK
goto end

:skipnodos
echo SKIP %1 (CPython-only)
echo ok > _TESTOK
goto end

:end

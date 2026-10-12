@echo off
rem BUILDS THE C++ VERSION ON WINDOWS
rem NEEDS MINGW-W64 G++ WITH C++17 IN PATH (MINGW FROM CODE::BLOCKS 20.03+, MSYS2 OR W64DEVKIT)
rem "BUILD.BAT NOPAUSE" DOES NOT WAIT FOR A KEY AT THE END
setlocal EnableDelayedExpansion
cd /d "%~dp0"
call :load
call :say BUILDING "bin\evaluator-cpp.exe"
if not exist bin mkdir bin
g++ -std=c++17 -O2 -Wall -o bin\evaluator-cpp.exe src\server.cpp src\engine.cpp src\sys.cpp -lws2_32 -lshell32 -static -static-libgcc -static-libstdc++
if errorlevel 1 (
  call :say BUILD_FAILED_GPP
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
call :say BUILD_DONE "bin\evaluator-cpp.exe"
if /i not "%~1"=="nopause" pause
exit /b 0

:load
rem READS THE SENTENCES (KEY=TEXT) FROM TRANSLATIONS\MESSAGES.TXT
set "MSGFILE=translations\messages.txt"
if not exist "%MSGFILE%" set "MSGFILE=..\..\master-translations\messages.txt"
if exist "%MSGFILE%" for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%MSGFILE%") do set "MSG_%%a=%%b"
goto :eof

:say
rem PRINTS SENTENCE %1 WITH {0} REPLACED BY %2
set "SAY_TEXT=!MSG_%~1!"
if not defined SAY_TEXT set "SAY_TEXT=%~1"
if not "%~2"=="" for /f "delims=" %%x in ("%~2") do set "SAY_TEXT=!SAY_TEXT:{0}=%%x!"
echo !SAY_TEXT!
goto :eof

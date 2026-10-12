@echo off
rem STARTS THE WEB VERSION (OFFLINE APP; NEEDS NODE.JS 16 OR NEWER, NO NPM PACKAGES)
setlocal EnableDelayedExpansion
cd /d "%~dp0"
call :load
node --version >nul 2>nul
if errorlevel 1 (
  call :say NEED_NODE
  pause
  exit /b 1
)
node server.js %*
pause
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

@echo off
rem COPIES THE TRANSLATIONS FROM THIS FOLDER INTO EVERY VERSION (WINDOWS)
rem DOUBLE-CLICK IT AFTER EDITING A LANGUAGE FILE OR ADDING A NEW ONE HERE
rem LANGUAGE FILES THAT NO LONGER EXIST HERE ARE REMOVED FROM THE VERSIONS
setlocal EnableDelayedExpansion
cd /d "%~dp0"
call :load
call :say INSTALL_TITLE "%CD%"
for %%v in (c c++ python web) do call :copyto %%v
call :say INSTALL_DONE
pause
exit /b 0

:copyto
rem COPIES ALL LANGUAGE FILES AND THE SENTENCES INTO VERSIONS\%1\TRANSLATIONS
if not exist "..\versions\%~1\" (
  call :say INSTALL_MISSING "versions\%~1"
  goto :eof
)
if not exist "..\versions\%~1\translations\" mkdir "..\versions\%~1\translations"
for %%f in ("..\versions\%~1\translations\*.json") do if not exist "%%~nxf" del "%%f"
copy /y *.json "..\versions\%~1\translations\" >nul
copy /y messages.txt "..\versions\%~1\translations\" >nul
call :say INSTALL_COPY "versions\%~1\translations"
goto :eof

:load
rem READS THE SENTENCES (KEY=TEXT) FROM MESSAGES.TXT
if exist messages.txt for /f "usebackq eol=# tokens=1,* delims==" %%a in ("messages.txt") do set "MSG_%%a=%%b"
goto :eof

:say
rem PRINTS SENTENCE %1 WITH {0} REPLACED BY %2
set "SAY_TEXT=!MSG_%~1!"
if not defined SAY_TEXT set "SAY_TEXT=%~1"
if not "%~2"=="" for /f "delims=" %%x in ("%~2") do set "SAY_TEXT=!SAY_TEXT:{0}=%%x!"
echo !SAY_TEXT!
goto :eof

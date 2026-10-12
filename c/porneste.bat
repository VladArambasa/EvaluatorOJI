@echo off
rem Porneste varianta C a Evaluatorului Olimpiada (Windows).
cd /d "%~dp0"
if not exist bin\evaluator-c.exe call build.bat
bin\evaluator-c.exe %*
if errorlevel 1 pause

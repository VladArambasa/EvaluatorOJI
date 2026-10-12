@echo off
rem Porneste varianta C++ a Evaluatorului Olimpiada (Windows).
cd /d "%~dp0"
if not exist bin\evaluator-cpp.exe call build.bat
bin\evaluator-cpp.exe %*
if errorlevel 1 pause

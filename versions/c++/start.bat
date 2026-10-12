@echo off
rem STARTS THE C++ VERSION ON WINDOWS (BUILDS IT FIRST WHEN NEEDED)
cd /d "%~dp0"
if not exist bin\evaluator-cpp.exe call build.bat nopause
if not exist bin\evaluator-cpp.exe (
  pause
  exit /b 1
)
bin\evaluator-cpp.exe %*
if errorlevel 1 pause

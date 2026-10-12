@echo off
rem Porneste varianta Web (aplicatie offline) a Evaluatorului Olimpiada (necesita Node.js 16 sau mai nou).
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js nu este instalat. Descarca-l de la https://nodejs.org/ ^(versiunea LTS^).
  pause
  exit /b 1
)
node server.js %*
pause

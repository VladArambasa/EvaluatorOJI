@echo off
rem Porneste varianta Python a Evaluatorului Olimpiada (necesita Python 3.7 sau mai nou).
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
  py -3 evaluator.py %*
  goto :final
)
where python >nul 2>nul
if not errorlevel 1 (
  python evaluator.py %*
  goto :final
)
echo Python nu este instalat. Descarca-l de la https://www.python.org/downloads/
echo La instalare bifeaza optiunea "Add python.exe to PATH".
:final
pause

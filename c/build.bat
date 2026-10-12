@echo off
rem Compileaza varianta C a Evaluatorului Olimpiada pe Windows.
rem Necesita MinGW-w64 (gcc) in PATH: de ex. MinGW din Code::Blocks 20.03+, MSYS2 sau w64devkit.
cd /d "%~dp0"
if not exist bin mkdir bin
gcc -std=c11 -O2 -Wall -o bin\evaluator-c.exe src\server.c src\engine.c src\json.c src\util.c src\runner.c -lws2_32 -lshell32 -static
if errorlevel 1 (
  echo Compilarea a esuat. Verifica daca gcc ^(MinGW-w64^) este in PATH.
  pause
  exit /b 1
)
echo Gata: bin\evaluator-c.exe
pause

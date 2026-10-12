@echo off
rem Compileaza varianta C++ a Evaluatorului Olimpiada pe Windows.
rem Necesita MinGW-w64 (g++ cu C++17) in PATH: de ex. MinGW din Code::Blocks 20.03+, MSYS2 sau w64devkit.
cd /d "%~dp0"
if not exist bin mkdir bin
g++ -std=c++17 -O2 -Wall -o bin\evaluator-cpp.exe src\server.cpp src\engine.cpp src\sys.cpp -lws2_32 -lshell32 -static -static-libgcc -static-libstdc++
if errorlevel 1 (
  echo Compilarea a esuat. Verifica daca g++ ^(MinGW-w64, C++17^) este in PATH.
  pause
  exit /b 1
)
echo Gata: bin\evaluator-cpp.exe
pause

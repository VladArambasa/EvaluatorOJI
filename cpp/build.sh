#!/bin/sh
# Compilează varianta C++ a Evaluatorului Olimpiadă.
#   ./build.sh            -> bin/evaluator-cpp      (Linux / macOS; necesită g++ sau clang++, C++17)
#   ./build.sh static     -> la fel, dar legat static (rulează pe orice distribuție Linux x64)
#   ./build.sh windows    -> bin/evaluator-cpp.exe  (de pe Linux, cu MinGW-w64: x86_64-w64-mingw32-g++)
# Dacă ai modificat gui/index.html sau common/, rulează întâi: python3 ../tools/embed.py
set -e
cd "$(dirname "$0")"
mkdir -p bin
SRC="src/server.cpp src/engine.cpp src/sys.cpp"
if [ "$1" = "windows" ]; then
    ${MINGW_CXX:-x86_64-w64-mingw32-g++} -std=c++17 -O2 -Wall -o bin/evaluator-cpp.exe $SRC -lws2_32 -lshell32 -static -static-libgcc -static-libstdc++
    echo "Gata: bin/evaluator-cpp.exe"
elif [ "$1" = "static" ]; then
    ${CXX:-c++} -std=c++17 -O2 -Wall -static -o bin/evaluator-cpp $SRC -lpthread
    echo "Gata: bin/evaluator-cpp (static)"
else
    ${CXX:-c++} -std=c++17 -O2 -Wall -o bin/evaluator-cpp $SRC -lpthread
    echo "Gata: bin/evaluator-cpp   (pornire: ./bin/evaluator-cpp)"
fi

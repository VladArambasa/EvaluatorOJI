#!/bin/sh
# Compilează varianta C a Evaluatorului Olimpiadă.
#   ./build.sh            -> bin/evaluator-c      (Linux / macOS; necesită gcc sau clang)
#   ./build.sh static     -> la fel, dar legat static (rulează pe orice distribuție Linux x64)
#   ./build.sh windows    -> bin/evaluator-c.exe  (de pe Linux, cu MinGW-w64: x86_64-w64-mingw32-gcc)
# Dacă ai modificat gui/index.html sau common/, rulează întâi: python3 ../tools/embed.py
set -e
cd "$(dirname "$0")"
mkdir -p bin
SRC="src/server.c src/engine.c src/json.c src/util.c src/runner.c"
if [ "$1" = "windows" ]; then
    ${MINGW_CC:-x86_64-w64-mingw32-gcc} -std=c11 -O2 -Wall -o bin/evaluator-c.exe $SRC -lws2_32 -lshell32 -static
    echo "Gata: bin/evaluator-c.exe"
elif [ "$1" = "static" ]; then
    ${CC:-cc} -std=c11 -O2 -Wall -static -o bin/evaluator-c $SRC -lpthread -lm
    echo "Gata: bin/evaluator-c (static)"
else
    ${CC:-cc} -std=c11 -O2 -Wall -o bin/evaluator-c $SRC -lpthread -lm
    echo "Gata: bin/evaluator-c   (pornire: ./bin/evaluator-c)"
fi

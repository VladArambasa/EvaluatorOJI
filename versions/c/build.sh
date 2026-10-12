#!/bin/sh
# BUILDS THE C VERSION
#   ./BUILD.SH            NATIVE BUILD (LINUX / MACOS, NEEDS GCC OR CLANG)
#   ./BUILD.SH STATIC     STATICALLY LINKED (RUNS ON ANY LINUX X64)
#   ./BUILD.SH WINDOWS    WINDOWS EXECUTABLE BUILT ON LINUX (NEEDS MINGW-W64)
# AFTER CHANGING THE SHARED FILES IN COMMON, RUN THE SYNC SCRIPT THERE FIRST
cd "$(dirname "$0")" || exit 1
MSGS=translations/messages.txt
[ -f "$MSGS" ] || MSGS=../../master-translations/messages.txt
msg() {
    # PRINTS SENTENCE $1 FROM THE MESSAGES FILE; {0} BECOMES $2
    m=$(grep -m 1 "^$1=" "$MSGS" 2>/dev/null | cut -d= -f2- | tr -d '\r')
    [ -n "$m" ] || m=$1
    case $m in *"{0}"*) m="${m%%"{0}"*}$2${m#*"{0}"}" ;; esac
    printf '%s\n' "$m"
}
mkdir -p bin
SRC="src/server.c src/engine.c src/json.c src/util.c src/runner.c"
if [ "$1" = "windows" ]; then
    msg BUILDING bin/evaluator-c.exe
    if ! ${MINGW_CC:-x86_64-w64-mingw32-gcc} -std=c11 -O2 -Wall -o bin/evaluator-c.exe $SRC -lws2_32 -lshell32 -static; then msg BUILD_FAILED; exit 1; fi
    msg BUILD_DONE bin/evaluator-c.exe
elif [ "$1" = "static" ]; then
    msg BUILDING bin/evaluator-c
    if ! ${CC:-cc} -std=c11 -O2 -Wall -static -o bin/evaluator-c $SRC -lpthread -lm; then msg BUILD_FAILED; exit 1; fi
    msg BUILD_DONE_STATIC bin/evaluator-c
else
    msg BUILDING bin/evaluator-c
    if ! ${CC:-cc} -std=c11 -O2 -Wall -o bin/evaluator-c $SRC -lpthread -lm; then msg BUILD_FAILED; exit 1; fi
    msg BUILD_DONE bin/evaluator-c
    msg START_WITH ./start.sh
fi

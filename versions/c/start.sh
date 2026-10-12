#!/bin/sh
# STARTS THE C VERSION (LINUX / MACOS)
# BUILDS THE EXECUTABLE WHEN IT IS MISSING OR DOES NOT RUN ON THIS SYSTEM
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
if [ ! -x bin/evaluator-c ]; then ./build.sh || exit 1; fi
./bin/evaluator-c "$@"
st=$?
if [ $st -eq 126 ] || [ $st -eq 127 ]; then msg REBUILDING; ./build.sh && exec ./bin/evaluator-c "$@"; fi
exit $st

#!/bin/sh
# Pornește varianta C (Linux/macOS). Dacă executabilul lipsește sau nu rulează pe acest sistem, îl compilează.
cd "$(dirname "$0")"
if [ ! -x bin/evaluator-c ]; then ./build.sh || exit 1; fi
./bin/evaluator-c "$@"
st=$?
if [ $st -eq 126 ] || [ $st -eq 127 ]; then ./build.sh && exec ./bin/evaluator-c "$@"; fi
exit $st

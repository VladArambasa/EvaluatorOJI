#!/bin/sh
# STARTS THE PYTHON VERSION (NEEDS PYTHON 3.7 OR NEWER, STANDARD LIBRARY ONLY)
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
if command -v python3 >/dev/null 2>&1; then exec python3 evaluator.py "$@"; fi
if command -v python >/dev/null 2>&1; then exec python evaluator.py "$@"; fi
msg NEED_PYTHON
exit 1

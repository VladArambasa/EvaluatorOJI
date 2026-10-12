#!/bin/sh
# STARTS THE WEB VERSION (OFFLINE APP; NEEDS NODE.JS 16 OR NEWER, NO NPM PACKAGES)
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
if ! command -v node >/dev/null 2>&1; then msg NEED_NODE; exit 1; fi
exec node server.js "$@"

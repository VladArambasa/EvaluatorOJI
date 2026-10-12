#!/bin/sh
# COPIES THE TRANSLATIONS FROM THIS FOLDER INTO EVERY VERSION (MACOS)
# DOUBLE-CLICK IT IN FINDER, OR RUN IT FROM A TERMINAL, AFTER EDITING A LANGUAGE FILE OR ADDING A NEW ONE HERE
# LANGUAGE FILES THAT NO LONGER EXIST HERE ARE REMOVED FROM THE VERSIONS
cd "$(dirname "$0")" || exit 1
MSGS=messages.txt
msg() {
    # PRINTS SENTENCE $1 FROM THE MESSAGES FILE; {0} BECOMES $2
    m=$(grep -m 1 "^$1=" "$MSGS" 2>/dev/null | cut -d= -f2- | tr -d '\r')
    [ -n "$m" ] || m=$1
    case $m in *"{0}"*) m="${m%%"{0}"*}$2${m#*"{0}"}" ;; esac
    printf '%s\n' "$m"
}
msg INSTALL_TITLE "$(pwd)"
for v in c c++ python web; do
    if [ ! -d "../versions/$v" ]; then msg INSTALL_MISSING "versions/$v"; continue; fi
    d="../versions/$v/translations"
    mkdir -p "$d" || exit 1
    for f in "$d"/*.json; do
        [ -f "$f" ] && [ ! -f "./$(basename "$f")" ] && rm -f "$f"
    done
    cp -f ./*.json ./messages.txt "$d"/ || exit 1
    msg INSTALL_COPY "versions/$v/translations"
done
msg INSTALL_DONE

#!/bin/sh
# Pornește varianta Web (aplicație offline; necesită Node.js 16 sau mai nou, fără pachete npm).
cd "$(dirname "$0")"
if ! command -v node >/dev/null 2>&1; then echo "Node.js nu este instalat: https://nodejs.org/ (versiunea LTS)"; exit 1; fi
exec node server.js "$@"

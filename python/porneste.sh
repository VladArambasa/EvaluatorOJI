#!/bin/sh
# Pornește varianta Python (necesită Python 3.7 sau mai nou; doar biblioteca standard).
cd "$(dirname "$0")"
if command -v python3 >/dev/null 2>&1; then exec python3 evaluator.py "$@"; fi
exec python evaluator.py "$@"

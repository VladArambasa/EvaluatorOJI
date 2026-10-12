# Translations

The master copies of every translation. Each version reads its own copy from its `translations/`
folder (`versions/c/translations`, `versions/c++/translations`, `versions/python/translations`,
`versions/web/translations`), so after any change here run the script for your system:

| system  | script |
|---------|--------|
| Windows | double-click `install-windows.bat` |
| Linux   | `sh install-linux.sh` |
| macOS   | double-click `install-macos.command` (or run it in Terminal) |

The script copies every `*.json` and `messages.txt` into the four versions and removes language
files that no longer exist here. Restart the evaluator afterwards.

## Files

| file | contents |
|------|----------|
| `en.json` | English, the base language: the keys are already English, so it is empty |
| `ro.json` | Romanian |
| `messages.txt` | the sentences printed by the build, start and install scripts (`.bat`, `.sh`), always English: `KEY=Text`, `{0}` is replaced by a value; do not use `!`, `^` or `%` in a text |

## Language file

```json
{
  "language": "ro",
  "name": "Română",
  "decimal": ",",
  "ui": {
    "Evaluate problem": "Evaluează problema",
    "Maximum score: %{score} points": "Punctaj maxim: %{score} puncte"
  },
  "messages": {
    "Runtime error: %{status}": "Eroare la execuție: %{status}",
    "Line %{n}: expected \"%{raw_expected}\", got \"%{raw_got}\"": "Linia %{n}: așteptat «%{raw_expected}», primit «%{raw_got}»"
  }
}
```

- `language` must match the file name (`ro.json` → `ro`); `name` is shown in the language selector.
- `decimal` is the decimal mark used for numbers (with `,` the CSV export uses `;` between columns).
- `ui`: every text of the interface. The key is the English text exactly as the interface has it;
  keep the HTML tags (`<code>`, `<b>`…) and every `%{name}` placeholder in the translation.
- `messages`: the English messages of the engines (errors, verdict texts, notes, log lines). The
  engines always answer in English; the interface translates each line with these templates.
  `%{name}` stands for any text, which is translated again when it is itself a known message; names
  that start with `raw` (student output, for example) are copied unchanged. A message without a
  template is shown in English.

## Adding a language

```
python3 tests/check_translations.py --skeleton de > master-translations/de.json
```

writes a file with every key, each mapped to its English text. Translate the values, set `name`
(e.g. `Deutsch`) and `decimal`, run the install script and restart the evaluator: the new language
appears in the title bar. The interface picks the saved choice, else the browser language, else English.

## Checking

```
python3 tests/check_translations.py
```

lists missing, unused and broken keys (placeholders that do not match) in every language file and
checks that the copies in the versions match the files here. `--results FILE...` checks real result
files (`_results/<problem>/latest.json`) for messages that no template covers.

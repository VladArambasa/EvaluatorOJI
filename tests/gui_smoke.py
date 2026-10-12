#!/usr/bin/env python3
# VISUAL SMOKE TEST OF THE GUI WITH PLAYWRIGHT
#   PYTHON3 GUI_SMOKE.PY URL OUTDIR [--LANG RO] [--THEME CLASSIC] [--NO-EVAL]
# OPENS THE EXAMPLE, EVALUATES THE FIRST PROBLEM, VISITS EVERY TAB AND SAVES SCREENSHOTS.
# IN A NON-ENGLISH LANGUAGE IT ALSO LISTS VISIBLE TEXTS THAT ARE STILL ENGLISH UI KEYS.
# EXIT CODE 1 ON CONSOLE ERRORS OR UNTRANSLATED TEXT
import json
import os
import sys
import time

from playwright.sync_api import sync_playwright

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
args = sys.argv[1:]
url, out = args[0], args[1]
lang = args[args.index("--lang") + 1] if "--lang" in args else "en"
theme = args[args.index("--theme") + 1] if "--theme" in args else "xp"
evaluate = "--no-eval" not in args
os.makedirs(out, exist_ok=True)
errors, untranslated = [], set()
ui = {}
if lang != "en":
    with open(os.path.join(ROOT, "master-translations", lang + ".json"), encoding="utf-8") as f:
        ui = json.load(f).get("ui", {})
# PLAIN KEYS ONLY (NO MARKUP, NO PLACEHOLDERS) WHOSE TRANSLATION DIFFERS
plain = {k for k, v in ui.items() if k != v and "<" not in k and "%{" not in k and len(k) > 3}


def shot(page, name):
    page.screenshot(path=os.path.join(out, "%s-%s-%s.png" % (lang, theme, name)))
    if plain:
        for line in page.evaluate("document.body.innerText").split("\n"):
            s = line.strip()
            if s in plain:
                untranslated.add(s)


def wait_idle(page, limit=180):
    t0 = time.time()
    while time.time() - t0 < limit:
        page.wait_for_timeout(700)
        if page.locator("button[data-act=stop][disabled]").count():
            break
    page.wait_for_timeout(600)


with sync_playwright() as p:
    b = p.chromium.launch()
    page = b.new_page(viewport={"width": 1280, "height": 800})
    page.on("console", lambda m: errors.append("console %s: %s" % (m.type, m.text)) if m.type in ("error", "warning") else None)
    page.on("pageerror", lambda e: errors.append("pageerror: %s" % e))
    page.goto(url)
    page.wait_for_timeout(1200)
    # LANGUAGE AND THEME FIRST, SO EVERY SCREENSHOT USES THEM
    if page.locator("#langsel").count():
        page.select_option("#langsel", lang)
        page.wait_for_timeout(400)
    want_classic = theme == "classic"
    if (page.evaluate("document.documentElement.getAttribute('data-theme')") == "classic") != want_classic:
        page.click("button[data-act=theme]")
        page.wait_for_timeout(300)
    shot(page, "00-start")
    ex = page.locator("button.choice[data-act=ws-recent]")
    if ex.count():
        ex.first.click()
        page.wait_for_timeout(1500)
    page.click("button.tab[data-v=eval]")
    page.wait_for_timeout(400)
    if evaluate:
        page.click("button[data-act=eval-problem]")
        page.wait_for_timeout(1500)
        shot(page, "01-running")
        wait_idle(page)
    shot(page, "02-eval")
    cell = page.locator("button.cell.v-tle, button.cell.v-wa")
    if cell.count():
        cell.first.click()
        page.wait_for_timeout(1000)
        shot(page, "03-test-details")
        page.keyboard.press("Escape")
        page.wait_for_timeout(300)
    for tab, name in [("tests", "04-tests"), ("problem", "05-problem"), ("workspace", "06-workspace"),
                      ("ranking", "07-ranking"), ("settings", "08-settings"), ("help", "09-help")]:
        page.click("button.tab[data-v=%s]" % tab)
        page.wait_for_timeout(450)
        shot(page, name)
    page.click("button.tab[data-v=eval]")
    page.wait_for_timeout(300)
    page.click("button[data-act=quit]")
    page.wait_for_timeout(400)
    shot(page, "10-confirm")
    page.keyboard.press("Escape")
    page.wait_for_timeout(300)
    page.set_viewport_size({"width": 390, "height": 844})
    page.wait_for_timeout(400)
    shot(page, "11-phone")
    b.close()
for e in errors:
    print(e)
for s in sorted(untranslated):
    print("untranslated:", s)
print("OK" if not errors and not untranslated else "%d console error(s), %d untranslated text(s)" % (len(errors), len(untranslated)))
sys.exit(1 if errors or untranslated else 0)

#!/usr/bin/env python3
"""Test vizual al interfeței cu Playwright: pornește o variantă, deschide exemplul,
evaluează și face capturi de ecran.  Utilizare:
   python3 gui_smoke.py URL OUTDIR [--theme dark]"""
import sys
import time
from playwright.sync_api import sync_playwright

url = sys.argv[1]
out = sys.argv[2]
theme = "dark" if "--theme" in sys.argv and sys.argv[sys.argv.index("--theme") + 1] == "dark" else "light"
errors = []


def shot(page, name):
    page.screenshot(path="%s/%s.png" % (out, name), full_page=False)


with sync_playwright() as p:
    b = p.chromium.launch()
    page = b.new_page(viewport={"width": 1440, "height": 900}, color_scheme=theme)
    page.on("console", lambda m: errors.append("console %s: %s" % (m.type, m.text)) if m.type in ("error", "warning") else None)
    page.on("pageerror", lambda e: errors.append("pageerror: %s" % e))
    page.goto(url)
    page.wait_for_timeout(1200)
    shot(page, "01-start")
    if page.locator("text=Deschide exemplul").count():
        page.click("text=Deschide exemplul")
        page.wait_for_timeout(1500)
    shot(page, "02-evaluare-gol")
    page.click("button[data-act=eval-problem]")
    t0 = time.time()
    while time.time() - t0 < 120:
        page.wait_for_timeout(1000)
        if page.locator("button[data-act=stop][disabled]").count():
            break
    page.wait_for_timeout(800)
    shot(page, "03-evaluare-rezultate")
    cells = page.locator("button.cell.v-wa")
    if cells.count():
        cells.first.click()
        page.wait_for_timeout(1200)
        shot(page, "04-detalii-test")
        page.keyboard.press("Escape")
    for tab, name in [("teste", "05-teste"), ("problema", "06-problema"), ("spatiu", "07-spatiu"),
                      ("clasament", "08-clasament"), ("setari", "09-setari"), ("ajutor", "10-ajutor")]:
        page.click("button.tab[data-v=%s]" % tab)
        page.wait_for_timeout(500)
        shot(page, name)
    page.click("button.tab[data-v=evaluare]")
    page.select_option("#probsel", "maxim")
    page.wait_for_timeout(500)
    page.click("button[data-act=eval-problem]")
    t0 = time.time()
    while time.time() - t0 < 60:
        page.wait_for_timeout(800)
        if page.locator("button[data-act=stop][disabled]").count():
            break
    page.wait_for_timeout(600)
    shot(page, "11-maxim")
    page.set_viewport_size({"width": 390, "height": 844})
    page.wait_for_timeout(400)
    shot(page, "12-mobil")
    b.close()
print("\n".join(errors) if errors else "fără erori în consolă")

#!/usr/bin/env python3
"""
ff_to_sierra.py
---------------
Letölti a ForexFactory heti gazdasági naptárát (JSON feed), és átalakítja
a Sierra Chart "Economic Calendar (CSV)" study formátumára.

Kimenet (EconCalendar.csv):
    Date,Time,Currency,Impact,Event
    2026-10-07,19:00,USD,High,FOMC Meeting Minutes

- Az időpontokat London időre váltja (a study "CSV Time Zone" = London).
- A fájlt atomikusan írja (előbb .tmp, aztán csere), így a Sierra soha
  nem olvas félkész fájlt.
- Ha a letöltés nem sikerül, a régi CSV érintetlen marad.

Futtatás:
    python ff_to_sierra.py
    python ff_to_sierra.py --out "D:\\SierraChart\\Data\\EconCalendar.csv"
    python ff_to_sierra.py --currencies USD,EUR,GBP --with-values
    python ff_to_sierra.py --no-holidays          (bankszünnapok nélkül)

Bankszünnapok: alapból bekerülnek, Medium fontosságként (pl. "CNY Bank Holiday").

Követelmény: Python 3.9+, Windows-on egyszer:  pip install tzdata
"""

import argparse
import csv
import io
import json
import logging
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime
from zoneinfo import ZoneInfo

# --------------------------------------------------------------------------
#  Beállítások (parancssorból felülírhatók)
# --------------------------------------------------------------------------
FEED_URL        = "https://nfs.faireconomy.media/ff_calendar_thisweek.json"
DEFAULT_OUT     = r"C:\SierraChart\Data\EconCalendar.csv"
DEFAULT_TZ      = "Europe/London"     # ennek kell egyeznie a study "CSV Time Zone" inputjával
USER_AGENT      = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ff_to_sierra/1.0"
TIMEOUT_SECONDS = 30

LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ff_to_sierra.log")


def setup_logging() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s  %(levelname)-7s %(message)s",
        handlers=[
            logging.FileHandler(LOG_FILE, encoding="utf-8"),
            logging.StreamHandler(sys.stdout),
        ],
    )


def download_feed(url: str) -> list:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=TIMEOUT_SECONDS) as resp:
        raw = resp.read().decode("utf-8")
    data = json.loads(raw)
    if not isinstance(data, list):
        raise ValueError("Váratlan feed-formátum: a gyökérelem nem lista")
    return data


def clean_text(s: str) -> str:
    return " ".join(str(s or "").replace("\r", " ").replace("\n", " ").split())


def convert(events: list, tz_name: str, currencies: set,
            include_holidays: bool, with_values: bool) -> list:
    tz = ZoneInfo(tz_name)
    rows = []

    for ev in events:
        try:
            impact = clean_text(ev.get("impact"))
            if impact == "Holiday" and not include_holidays:
                continue
            if impact not in ("High", "Medium", "Low", "Holiday"):
                impact = "Low"
            if impact == "Holiday":
                impact = "Medium"        # bankszünnap Medium-ként, hogy a Low-ok nélkül is látszódjon

            currency = clean_text(ev.get("country")).upper()   # pl. USD, EUR, ALL
            if currencies and currency not in currencies:
                continue

            # ISO idő eltolással, pl. 2026-10-07T14:00:00-04:00
            dt_local = datetime.fromisoformat(ev["date"]).astimezone(tz)

            title = clean_text(ev.get("title"))
            if with_values:
                parts = []
                if clean_text(ev.get("forecast")):
                    parts.append("F: " + clean_text(ev.get("forecast")))
                if clean_text(ev.get("previous")):
                    parts.append("P: " + clean_text(ev.get("previous")))
                if parts:
                    title += " (" + " | ".join(parts) + ")"

            rows.append((dt_local, currency, impact, title))
        except Exception as exc:  # egy hibás tétel ne állítsa le az egészet
            logging.warning("Kihagyott tétel (%s): %r", exc, ev)

    rows.sort(key=lambda r: r[0])
    return rows


def write_csv_atomic(rows: list, out_path: str, tz_name: str) -> None:
    buf = io.StringIO()
    buf.write("# Forrás: ForexFactory heti naptár - generálta: ff_to_sierra.py\n")
    buf.write(f"# Frissítve: {datetime.now(ZoneInfo(tz_name)):%Y-%m-%d %H:%M} ({tz_name})\n")
    w = csv.writer(buf, lineterminator="\n", quoting=csv.QUOTE_MINIMAL)
    w.writerow(["Date", "Time", "Currency", "Impact", "Event"])
    for dt_local, currency, impact, title in rows:
        # A study az 5. mezőtől mindent névnek vesz, ezért a címet nem idézőjelezzük,
        # a benne lévő vessző nem okoz gondot.
        buf.write(f"{dt_local:%Y-%m-%d},{dt_local:%H:%M},{currency},{impact},{title}\n")

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    tmp_path = out_path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8", newline="") as f:
        f.write(buf.getvalue())
    os.replace(tmp_path, out_path)


def main() -> int:
    p = argparse.ArgumentParser(description="ForexFactory -> Sierra Chart CSV")
    p.add_argument("--out", default=DEFAULT_OUT, help="Kimeneti CSV útvonala")
    p.add_argument("--tz", default=DEFAULT_TZ, help="Kimeneti időzóna (pl. Europe/London, America/New_York)")
    p.add_argument("--currencies", default="",
                   help="Csak ezek a devizák, pl. USD,EUR,GBP (üres = mind; a szűrést a study is tudja)")
    p.add_argument("--no-holidays", dest="include_holidays", action="store_false",
                   help="Bankszünnapok kihagyása (alapból bekerülnek, Medium-ként)")
    p.add_argument("--with-values", action="store_true", help="Forecast / Previous érték a címke végén")
    args = p.parse_args()

    setup_logging()
    currencies = {c.strip().upper() for c in args.currencies.split(",") if c.strip()}

    try:
        events = download_feed(FEED_URL)
    except urllib.error.HTTPError as exc:
        logging.error("HTTP hiba %s - túl gyakori letöltés esetén a ForexFactory ideiglenesen tilthat. "
                      "A régi CSV megmaradt.", exc.code)
        return 1
    except Exception as exc:
        logging.error("Letöltési hiba: %s - a régi CSV megmaradt.", exc)
        return 1

    rows = convert(events, args.tz, currencies, args.include_holidays, args.with_values)
    if not rows:
        logging.error("A feed üres vagy minden tétel ki lett szűrve - a régi CSV megmaradt.")
        return 1

    try:
        write_csv_atomic(rows, args.out, args.tz)
    except Exception as exc:
        logging.error("Írási hiba (%s): %s", args.out, exc)
        return 1

    logging.info("%d esemény kiírva: %s", len(rows), args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

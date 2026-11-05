# Sierra Chart – Gazdasági naptár (Economic Calendar)

Gazdasági események (CPI, NFP, FOMC, kamatdöntések, bankszünnapok stb.) megjelenítése **Sierra Chart** chartokon függőleges vonalakkal és címkékkel, automatikus napi frissítéssel a ForexFactory heti naptárából.

```
ForexFactory heti naptár (JSON)
        │   ff_to_sierra.py  (Windows Task Scheduler futtatja)
        ▼
Sierra Chart Data\EconCalendar.csv
        │   EconCalendar.cpp  (ACSIL study, percenként újraolvassa)
        ▼
Vonalak + címkék a charton, riasztás az esemény előtt
```

## Fájlok

| Fájl | Mi ez |
|---|---|
| `EconCalendar.cpp` | Sierra Chart ACSIL custom study (C++) – beolvassa a CSV-t és kirajzolja az eseményeket |
| `ff_to_sierra.py` | Python script – letölti a ForexFactory heti naptárát és legyártja a CSV-t |

## A study tudása (v5)

- **Csak a mai nap eseményei** (alapértelmezés) – a tegnapiak törlődnek, a holnapiak nem jelennek meg; napváltáskor magától frissül. Átállítható *Aktuális hét* módra is.
- **Állítható napváltás** – pl. 22:00 esetén a másnapi események már előző este látszanak.
- **Fontosság szerinti színezés**: High = piros folytonos, Medium = narancs szaggatott, Low = sárga szaggatott. Mindhárom külön kapcsolható – megjelenítéshez és riasztáshoz is.
- **Deviza-szűrő** (pl. `USD,EUR,GBP`).
- **Címke a vonaltól balra**, a felirat vége ér a vonalhoz. Formátum: `13:30 (Th) USD CPI m/m`.
- **Ütközésmentes címke-elrendezés** minden idősíkon (1D, 4h, 1h, 30m, 15m…) – a study pixelben számolja a címkék szélességét, zoomoláskor újrarendez.
- **Gyertyához igazítás** – a 15:30-as adat 1h charton a 15:00-ás gyertyán jelenik meg, a címkében a pontos idővel.
- **Riasztás** hanggal, beállítható perccel az esemény előtt.
- **Időzóna-átváltás** – a CSV-időket a megadott időzónából (alapból London) a chart időzónájára váltja.

## Telepítés

### 1. A study lefordítása

1. Másold az `EconCalendar.cpp`-t a Sierra Chart `ACS_Source` mappájába  
   (pl. `C:\SierraChartKit_2807_251212\ACS_Source\`).
2. Sierra Chartban: **Analysis → Build Custom Studies DLL** → válaszd ki a fájlt → **Remote Build**.
3. Chart → **Analysis → Studies → Add Custom Study** → *Krisz Economic Calendar* → **Economic Calendar (CSV)**.

Frissítéskor csak írd felül a fájlt és fordítsd újra – ha csak új input került a lista végére, a studyt nem kell újra hozzáadni.

### 2. Python és a script

1. Python 3.9+ telepítése a [python.org](https://www.python.org/downloads/) oldalról – a telepítőben pipáld be: **Add python.exe to PATH**.
2. Parancssorban egyszer:
   ```
   pip install tzdata
   ```
3. Tedd a scriptet ide: `C:\Scripts\ff_to_sierra.py`
4. Próbafuttatás:
   ```
   python C:\Scripts\ff_to_sierra.py --with-values --out "C:\SierraChartKit_2807_251212\Data\EconCalendar.csv"
   ```
   Siker esetén: `… esemény kiírva: …EconCalendar.csv`

#### Script kapcsolók

| Kapcsoló | Hatás |
|---|---|
| `--out "útvonal"` | A kimeneti CSV helye (alap: `C:\SierraChart\Data\EconCalendar.csv`) |
| `--with-values` | Forecast / Previous érték a címke végén, pl. `(F: 200K \| P: 197K)` |
| `--currencies USD,EUR,GBP` | Már letöltéskor szűr devizára (a study is tud szűrni) |
| `--no-holidays` | Bankszünnapok kihagyása (alapból Medium-ként bekerülnek) |
| `--tz Europe/London` | A CSV időzónája – egyezzen a study *CSV Time Zone* inputjával |

### 3. Automatikus futtatás (Task Scheduler)

A parancssorba (egy-egy sor):

```
schtasks /Create /TN "FF Sierra naptar\Vasarnap" /SC WEEKLY /D SUN /ST 21:00 /TR "\"C:\Users\User\AppData\Local\Python\pythoncore-3.14-64\pythonw.exe\" \"C:\Scripts\ff_to_sierra.py\" --with-values --out \"C:\SierraChartKit_2807_251212\Data\EconCalendar.csv\"" /F
schtasks /Create /TN "FF Sierra naptar\Reggel" /SC DAILY /ST 06:00 /TR "\"C:\Users\User\AppData\Local\Python\pythoncore-3.14-64\pythonw.exe\" \"C:\Scripts\ff_to_sierra.py\" --with-values --out \"C:\SierraChartKit_2807_251212\Data\EconCalendar.csv\"" /F
schtasks /Create /TN "FF Sierra naptar\Del" /SC DAILY /ST 12:00 /TR "\"C:\Users\User\AppData\Local\Python\pythoncore-3.14-64\pythonw.exe\" \"C:\Scripts\ff_to_sierra.py\" --with-values --out \"C:\SierraChartKit_2807_251212\Data\EconCalendar.csv\"" /F
```

Kimaradt futás pótlása (ha a gép ki volt kapcsolva):

```
powershell -Command "Get-ScheduledTask -TaskPath '\FF Sierra naptar\' | ForEach-Object { $s = $_.Settings; $s.StartWhenAvailable = $true; Set-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath -Settings $s }"
```

Az útvonalakat (Python, script, Sierra Data mappa) igazítsd a saját gépedhez. A `pythonw.exe` helyét a `where pythonw` paranccsal kérdezheted le.

> Napi néhány letöltés elég – a ForexFactory feed nem hivatalos API, a túl gyakori lekérést ideiglenesen tilthatja.

## CSV formátum

```
Date,Time,Currency,Impact,Event
2026-10-15,13:30,USD,High,CPI m/m
```

- `Date`: `YYYY-MM-DD` · `Time`: `HH:MM` (24 órás) · `Impact`: `High` / `Medium` / `Low`
- `#`-tel kezdődő sor komment; az esemény neve tartalmazhat vesszőt.
- Kézzel is szerkeszthető – a study percenként újraolvassa.

## Fontosabb study-beállítások

| Input | Alap | Megjegyzés |
|---|---|---|
| CSV fájl | `EconCalendar.csv` | Relatív útvonal = a Sierra `Data` mappája |
| CSV Time Zone | London | Egyezzen a script `--tz` értékével |
| Devizák | `USD,EUR,GBP` | Üres = mind |
| Megjelenítés – High / Medium / Low | Igen / Igen / Nem | |
| Riasztás ennyi perccel előtte | 5 | 0 = kikapcsolva |
| Riasztás – High / Medium / Low | Igen / Nem / Nem | |
| Minimális sorköz (%) | 2 | A study ennél nagyobbat is választhat |
| Címke méret-korrekció (%) | 125 | Növeld, ha a címkék mégis összeérnek (Windows 125–150% nagyítás) |
| Megjelenítési időszak | Csak a mai nap | vagy *Aktuális hét* |
| Napváltás ideje | 00:00 | Csak „mai nap” módban |
| Új hét már vasárnap este | Igen | Csak „Aktuális hét” módban |
| Vasárnapi váltás ideje | 22:00 | Csak „Aktuális hét” módban |

## Hibakeresés

- **Nincsenek vonalak** → Sierra Chart: **Window → Message Log** – hány eseményt töltött be a study, megtalálta-e a fájlt.
- **Nem frissül a CSV** → `C:\Scripts\ff_to_sierra.log` – sikerült-e a legutóbbi letöltés.
- **Hiányzik egy deviza eseménye / bankszünnapja** → vedd fel a devizát a study *Devizák* inputjába.
- **Jövőbeli esemény nem látszik** → növeld a chart jobb oldali üres helyét (*Chart Settings*).
- **Tick / range / renko chart** → nincs fix gyertyahossz, a jövőbeli események az utolsó gyertyára kerülnek; időalapú chartokon pontos.

## Változások

- **v5** – Csak a mai nap eseményei (kapcsolható), állítható napváltás.
- **v4** – Ütközésmentes címke-elrendezés intraday chartokon, méret-korrekció input.
- **v3** – Csak az aktuális hét, címke a vonaltól balra, napnév-rövidítés, vasárnap esti hétváltás.
- **v2** – Gyertyához igazítás, azonos időpontú címkék egymás alatt, High/Medium/Low külön kapcsolók.
- **v1** – Első változat: CSV beolvasás, vonalak, címkék, riasztás.

## Megjegyzés

Az adatok forrása a ForexFactory nyilvános heti naptár-feedje (`nfs.faireconomy.media`), amely nem hivatalos API – formátuma vagy elérhetősége bármikor változhat. Az időpontokat fontos esemény előtt érdemes egy másik naptárban is ellenőrizni.

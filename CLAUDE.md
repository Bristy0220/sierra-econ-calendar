# Sierra Chart gazdasági naptár – projekt-jegyzet Claude-nak

Krisz projektje: gazdasági események megjelenítése Sierra Chart chartokon. A felhasználóval **magyarul** kommunikálj.

## Fájlok
- `EconCalendar.cpp` – Sierra Chart ACSIL study (C++), jelenleg **v5**. Itt nem fordítható (nincs `sierrachart.h`); a felhasználó Sierra Chartban fordítja: *Analysis → Build Custom Studies DLL → Remote Build*. Fordítási hibát a felhasználó másolja be.
- `ff_to_sierra.py` – letölti a ForexFactory heti naptárát (`https://nfs.faireconomy.media/ff_calendar_thisweek.json`) és CSV-t ír a Sierra `Data` mappájába.
- `README.md` – teljes magyar leírás (telepítés, beállítások, hibakeresés, verziótörténet). Módosításkor frissítsd a *Változások* részt is.

## A felhasználó gépén (Windows)
- Sierra Chart: `C:\SierraChartKit_2807_251212\` (study forrás: `ACS_Source\`, CSV: `Data\EconCalendar.csv`)
- Script: `C:\Scripts\ff_to_sierra.py`, log: `C:\Scripts\ff_to_sierra.log`
- Python: `C:\Users\User\AppData\Local\Python\pythoncore-3.14-64\` (`pythonw.exe`)
- Task Scheduler: `FF Sierra naptar` mappa – `Vasarnap` (vas 21:00), `Reggel` (06:00), `Del` (12:00), kimaradt futás pótlása bekapcsolva
- Windows angol nyelvű → menüneveket angolul add meg.

## Szabályok a study módosításához
- Új inputot **mindig a lista végére** tegyél (`sc.Input[N]` következő szabad index), különben a felhasználónak törölnie kell és újra hozzáadnia a studyt. Jelenleg a legmagasabb: `sc.Input[26]`; az `sc.Input[13]` nem használt (régi).
- CSV formátum: `Date,Time,Currency,Impact,Event` – `YYYY-MM-DD,HH:MM,USD,High,Esemény neve`, London időben.
- Bankszünnapok a scriptben Medium-ként kerülnek a CSV-be (`--no-holidays` kapcsolóval kihagyhatók).

## GitHub
`Bristy0220/sierra-econ-calendar`, ág: `main`.

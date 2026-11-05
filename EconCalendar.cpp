// =====================================================================
//  EconCalendar.cpp  -  Gazdasági naptár (CSV) megjelenítése Sierra Charton
//  v5
//
//  v5 változások:
//   - Új input: "Megjelenítési időszak" = Csak a mai nap (alap) / Aktuális hét
//   - "Mai nap" módban csak a mai események látszanak; a tegnapiak és a
//     korábbiak törlődnek, a holnapiak/későbbiek nem jelennek meg.
//     Napváltáskor (alapból 00:00) egy percen belül magától frissül.
//   - Új input: "Napváltás ideje" - ha pl. 22:00, a következő nap eseményei
//     már előző este 22:00-tól látszanak (ázsiai nyitás előtt).
//
//  v4 változások:
//   - Ütközésmentes címke-elrendezés intraday chartokon is: a study
//     pixelben becsüli minden címke szélességét és a gyertyák távolságát,
//     és minden címkét az első olyan "sorba" teszi, ahol nem takar ki
//     egy korábbit. Zoomoláskor / átméretezéskor automatikusan újrarendez.
//   - A sorköz a betűméretből és a chart-régió magasságából számolódik,
//     így alacsony charton sem csúsznak egymásra a sorok.
//   - Új input: "Címke méret-korrekció (%)" - ha a Windows nagyítás
//     (125%, 150%) miatt mégis összeérnek, ezt kell növelni.
//
//  v3:
//   - Csak az aktuális hét eseményei; vasárnap esti hétváltás kapcsolóval
//   - A címke a vonaltól balra áll, a vége ér a vonalhoz
//   - Az idő után a nap rövidítése: 13:45 (Mo), 16:00 (We) ...
//  v2:
//   - Gyertyához igazítás (15:30-as adat 1h / 30m / 15m charton is látszik)
//   - High / Medium / Low külön kapcsolókkal (megjelenítés és riasztás)
//
//  CSV formátum (vessző elválasztó, első sor fejléc lehet, # = komment):
//     Date,Time,Currency,Impact,Event
//     2026-10-15,13:30,USD,High,CPI m/m
// =====================================================================

#include "sierrachart.h"

#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>

SCDLLName("Krisz Economic Calendar")

// ---------------------------------------------------------------------
//  Adatszerkezetek
// ---------------------------------------------------------------------
struct EconEvent
{
    SCDateTime  Time;        // pontos idő, a chart időzónájában
    std::string Currency;
    int         Impact;      // 0 = Low, 1 = Medium, 2 = High
    std::string Title;
    bool        AlertFired;
};

struct CalendarData
{
    std::vector<EconEvent> Events;
    int        DrawingCount = 0;
    SCDateTime LastLoad;
    bool       Loaded = false;

    // az utolsó rajzoláskori chart-geometria (változáskor újrarendezés)
    double     LastPxPerBar  = -1.0;
    int        LastRegionH   = -1;
    int        LastArraySize = -1;
};

struct BarGroup
{
    SCDateTime BarTime;
    int        BarIndex;
    std::vector<const EconEvent*> Items;
};

static const int LINE_NUMBER_BASE = 87300000;

// ---------------------------------------------------------------------
//  Segédfüggvények
// ---------------------------------------------------------------------
static std::string Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n\"");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n\"");
    return s.substr(b, e - b + 1);
}

static std::string ToUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

static int ParseImpact(const std::string& raw)
{
    std::string s = ToUpper(Trim(raw));
    if (s == "HIGH" || s == "H" || s == "3") return 2;
    if (s == "MEDIUM" || s == "MED" || s == "M" || s == "2") return 1;
    return 0;
}

static bool ImpactEnabled(int impact, int high, int medium, int low)
{
    switch (impact)
    {
        case 2:  return high   != 0;
        case 1:  return medium != 0;
        default: return low    != 0;
    }
}

static std::string NormalizeCurrencyList(const std::string& raw)
{
    std::string s = ToUpper(raw);
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return std::isspace(c); }), s.end());
    if (s.empty() || s == "ALL") return "";
    return "," + s + ",";
}

static bool CurrencyAllowed(const std::string& normalizedList, const std::string& currency)
{
    if (normalizedList.empty()) return true;
    return normalizedList.find("," + ToUpper(currency) + ",") != std::string::npos;
}

static std::string ResolvePath(SCStudyInterfaceRef sc, const std::string& file)
{
    if (file.size() > 1 && (file[1] == ':' || (file[0] == '\\' && file[1] == '\\')))
        return file;

    std::string folder = sc.DataFilesFolder().GetChars();
    if (!folder.empty() && folder.back() != '\\' && folder.back() != '/')
        folder += "\\";
    return folder + file;
}

// Az esemény idejét a tartalmazó gyertya kezdetéhez igazítja.
// Jövőbeli eseménynél (az utolsó gyertya után) a gyertyahosszal extrapolál.
static bool SnapToBar(SCStudyInterfaceRef sc, const SCDateTime& t,
                      SCDateTime& outTime, int& outIndex)
{
    const int last = sc.ArraySize - 1;
    if (last < 0) return false;

    const SCDateTime firstTime = sc.BaseDateTimeIn[0];
    const SCDateTime lastTime  = sc.BaseDateTimeIn[last];

    if (t < firstTime) return false;

    if (t >= lastTime)
    {
        const int secs = sc.SecondsPerBar;
        if (secs <= 0)
        {
            outIndex = last;
            outTime  = lastTime;
            return true;
        }
        const double diffSec   = (t - lastTime).GetAsDouble() * 86400.0;
        const int    barsAhead = (int)std::floor(diffSec / secs + 1e-6);
        outIndex = last + barsAhead;
        outTime  = lastTime + SCDateTime((double)barsAhead * secs / 86400.0);
        return true;
    }

    const int idx = sc.GetContainingIndexForSCDateTime(sc.ChartNumber, t);
    if (idx < 0 || idx > last) return false;
    outIndex = idx;
    outTime  = sc.BaseDateTimeIn[idx];
    return true;
}

// Az aktuális hét határai (chart időzóna).
static void CurrentWeekBounds(const SCDateTime& now, int sundayRollover, int rolloverSec,
                              SCDateTime& weekStart, SCDateTime& weekEnd)
{
    const int dow             = now.GetDayOfWeek();     // 0 = vasárnap ... 6 = szombat
    const int daysSinceMonday = (dow + 6) % 7;
    int monday = now.GetDate() - daysSinceMonday;

    if (!sundayRollover)
    {
        weekStart = SCDateTime((double)monday);
        weekEnd   = SCDateTime((double)(monday + 7));
        return;
    }

    const double rollFrac = rolloverSec / 86400.0;
    const SCDateTime thisSundayRoll = SCDateTime((double)(monday + 6) + rollFrac);
    if (now >= thisSundayRoll)
        monday += 7;

    weekStart = SCDateTime((double)(monday - 1) + rollFrac);
    weekEnd   = SCDateTime((double)(monday + 6) + rollFrac);
}

// Az aktuális nap határai (chart időzóna): [napváltás ideje] -> másnap ugyanekkor.
// Alapból 00:00 - 24:00. Pl. 22:00 esetén a "mai nap" előző este 22:00-kor kezdődik.
static void CurrentDayBounds(const SCDateTime& now, int dayChangeSec,
                             SCDateTime& dayStart, SCDateTime& dayEnd)
{
    double frac = dayChangeSec / 86400.0;
    if (frac >= 0.5) frac -= 1.0;          // késő esti váltás = a következő nap korábban kezdődik
    const SCDateTime shifted = now - SCDateTime(frac);
    const int d = shifted.GetDate();
    dayStart = SCDateTime((double)d + frac);
    dayEnd   = SCDateTime((double)(d + 1) + frac);
}

static const char* DayAbbrev(const SCDateTime& t)
{
    static const char* names[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    const int dow = t.GetDayOfWeek();
    return (dow >= 0 && dow < 7) ? names[dow] : "??";
}

static bool LoadCsv(SCStudyInterfaceRef sc,
                    const std::string& path,
                    int sourceTimeZone,
                    const std::vector<EconEvent>& previous,
                    std::vector<EconEvent>& out)
{
    std::ifstream f(path.c_str());
    if (!f.is_open())
    {
        SCString msg;
        msg.Format("Economic Calendar: nem nyitható meg a fájl: %s", path.c_str());
        sc.AddMessageToLog(msg, 1);
        return false;
    }

    out.clear();
    std::string line;
    int lineNo = 0;
    const SCDateTime now = sc.CurrentSystemDateTime;

    while (std::getline(f, line))
    {
        ++lineNo;
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (ToUpper(t.substr(0, 4)) == "DATE") continue;

        std::string fields[5];
        size_t pos = 0;
        int i = 0;
        for (; i < 4; ++i)
        {
            size_t c = t.find(',', pos);
            if (c == std::string::npos) break;
            fields[i] = Trim(t.substr(pos, c - pos));
            pos = c + 1;
        }
        if (i < 4)
        {
            SCString msg;
            msg.Format("Economic Calendar: hibás sor (%d): %s", lineNo, t.c_str());
            sc.AddMessageToLog(msg, 0);
            continue;
        }
        fields[4] = Trim(t.substr(pos));

        int y = 0, mo = 0, d = 0, h = 0, mi = 0;
        if (sscanf(fields[0].c_str(), "%d-%d-%d", &y, &mo, &d) != 3)
        {
            SCString msg;
            msg.Format("Economic Calendar: hibás dátum (%d. sor): %s", lineNo, fields[0].c_str());
            sc.AddMessageToLog(msg, 0);
            continue;
        }
        if (sscanf(fields[1].c_str(), "%d:%d", &h, &mi) != 2)
        {
            h = 0;
            mi = 0;
        }

        SCDateTime dt;
        dt.SetDateTimeYMDHMS(y, mo, d, h, mi, 0);

        EconEvent ev;
        ev.Time       = sc.ConvertDateTimeToChartTimeZone(dt, (TimeZonesEnum)sourceTimeZone);
        ev.Currency   = ToUpper(fields[2]);
        ev.Impact     = ParseImpact(fields[3]);
        ev.Title      = fields[4];
        ev.AlertFired = (ev.Time <= now);

        for (const EconEvent& p : previous)
        {
            if (p.Time == ev.Time && p.Title == ev.Title)
            {
                ev.AlertFired = ev.AlertFired || p.AlertFired;
                break;
            }
        }

        out.push_back(ev);
    }

    std::sort(out.begin(), out.end(),
              [](const EconEvent& a, const EconEvent& b)
              {
                  if (a.Time != b.Time) return a.Time < b.Time;
                  return a.Impact > b.Impact;
              });

    SCString msg;
    msg.Format("Economic Calendar: %d esemény betöltve (%s)", (int)out.size(), path.c_str());
    sc.AddMessageToLog(msg, 0);
    return true;
}

// A [from, to) tartományba eső rajzszámok törlése
static void DeleteDrawingRange(SCStudyInterfaceRef sc, int from, int to)
{
    for (int k = from; k < to; ++k)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, LINE_NUMBER_BASE + k);
}

// ---------------------------------------------------------------------
//  A study
// ---------------------------------------------------------------------
SCSFExport scsf_EconomicCalendarCSV(SCStudyInterfaceRef sc)
{
    SCInputRef In_File          = sc.Input[0];
    SCInputRef In_SourceTZ      = sc.Input[1];
    SCInputRef In_Currencies    = sc.Input[2];
    SCInputRef In_ShowHigh      = sc.Input[3];
    SCInputRef In_ShowMedium    = sc.Input[4];
    SCInputRef In_ShowLow       = sc.Input[5];
    SCInputRef In_ColorHigh     = sc.Input[6];
    SCInputRef In_ColorMedium   = sc.Input[7];
    SCInputRef In_ColorLow      = sc.Input[8];
    SCInputRef In_LineWidth     = sc.Input[9];
    SCInputRef In_ShowLabels    = sc.Input[10];
    SCInputRef In_LabelTop      = sc.Input[11];
    SCInputRef In_LabelStep     = sc.Input[12];
    SCInputRef In_Unused13      = sc.Input[13];   // v3-ban: lépcsőzés gyertyaszámmal (már nem kell)
    SCInputRef In_FontSize      = sc.Input[14];
    SCInputRef In_ShowTime      = sc.Input[15];
    SCInputRef In_AlertMinutes  = sc.Input[16];
    SCInputRef In_AlertHigh     = sc.Input[17];
    SCInputRef In_AlertMedium   = sc.Input[18];
    SCInputRef In_AlertLow      = sc.Input[19];
    SCInputRef In_AlertSound    = sc.Input[20];
    SCInputRef In_ReloadSeconds = sc.Input[21];
    SCInputRef In_SundayRollover= sc.Input[22];
    SCInputRef In_RolloverTime  = sc.Input[23];
    SCInputRef In_SizeCorrection= sc.Input[24];
    SCInputRef In_PeriodMode    = sc.Input[25];
    SCInputRef In_DayChangeTime = sc.Input[26];

    if (sc.SetDefaults)
    {
        sc.GraphName        = "Economic Calendar (CSV)";
        sc.StudyDescription = "Gazdasági események CSV-ből: függőleges vonal, címke, riasztás. v5";
        sc.GraphRegion      = 0;
        sc.AutoLoop         = 0;
        sc.UpdateAlways     = 1;

        In_File.Name = "CSV fájl (relatív = Data mappa)";
        In_File.SetString("EconCalendar.csv");

        In_SourceTZ.Name = "CSV Time Zone";
        In_SourceTZ.SetTimeZone(TIMEZONE_LONDON);

        In_Currencies.Name = "Devizák (pl. USD,EUR,GBP | üres = mind)";
        In_Currencies.SetString("USD,EUR,GBP");

        In_ShowHigh.Name = "Megjelenítés - High";
        In_ShowHigh.SetYesNo(1);
        In_ShowMedium.Name = "Megjelenítés - Medium";
        In_ShowMedium.SetYesNo(1);
        In_ShowLow.Name = "Megjelenítés - Low";
        In_ShowLow.SetYesNo(0);

        In_ColorHigh.Name = "Szín - High";
        In_ColorHigh.SetColor(RGB(230, 40, 40));
        In_ColorMedium.Name = "Szín - Medium";
        In_ColorMedium.SetColor(RGB(255, 150, 0));
        In_ColorLow.Name = "Szín - Low";
        In_ColorLow.SetColor(RGB(220, 200, 0));

        In_LineWidth.Name = "Vonalvastagság";
        In_LineWidth.SetInt(1);
        In_LineWidth.SetIntLimits(1, 10);

        In_ShowLabels.Name = "Címkék megjelenítése";
        In_ShowLabels.SetYesNo(1);

        In_LabelTop.Name = "Első címke helye (% a régió aljától)";
        In_LabelTop.SetInt(97);
        In_LabelTop.SetIntLimits(10, 100);

        In_LabelStep.Name = "Minimális sorköz (%) - a study ennél nagyobbat is választhat";
        In_LabelStep.SetInt(2);
        In_LabelStep.SetIntLimits(1, 20);

        In_Unused13.Name = "(nem használt)";
        In_Unused13.SetInt(0);

        In_FontSize.Name = "Betűméret";
        In_FontSize.SetInt(8);
        In_FontSize.SetIntLimits(6, 20);

        In_ShowTime.Name = "Pontos idő a címkében";
        In_ShowTime.SetYesNo(1);

        In_AlertMinutes.Name = "Riasztás ennyi perccel előtte (0 = ki)";
        In_AlertMinutes.SetInt(5);
        In_AlertMinutes.SetIntLimits(0, 240);

        In_AlertHigh.Name = "Riasztás - High";
        In_AlertHigh.SetYesNo(1);
        In_AlertMedium.Name = "Riasztás - Medium";
        In_AlertMedium.SetYesNo(0);
        In_AlertLow.Name = "Riasztás - Low";
        In_AlertLow.SetYesNo(0);

        In_AlertSound.Name = "Riasztási hang";
        In_AlertSound.SetAlertSoundNumber(2);

        In_ReloadSeconds.Name = "CSV újraolvasása (másodperc)";
        In_ReloadSeconds.SetInt(60);
        In_ReloadSeconds.SetIntLimits(10, 86400);

        In_SundayRollover.Name = "Új hét már vasárnap este (nyitáskor)";
        In_SundayRollover.SetYesNo(1);

        In_RolloverTime.Name = "Vasárnapi váltás ideje (chart időzóna)";
        In_RolloverTime.SetTime(HMS_TIME(22, 0, 0));

        In_SizeCorrection.Name = "Címke méret-korrekció (%) - növeld, ha mégis összeérnek";
        In_SizeCorrection.SetInt(125);
        In_SizeCorrection.SetIntLimits(50, 300);

        In_PeriodMode.Name = "Megjelenítési időszak";
        In_PeriodMode.SetCustomInputStrings("Csak a mai nap;Aktuális hét");
        In_PeriodMode.SetCustomInputIndex(0);

        In_DayChangeTime.Name = "Napváltás ideje (chart időzóna) - csak 'mai nap' módban";
        In_DayChangeTime.SetTime(HMS_TIME(0, 0, 0));

        return;
    }

    CalendarData* data = (CalendarData*)sc.GetPersistentPointer(1);

    if (sc.LastCallToFunction)
    {
        if (data != nullptr)
        {
            DeleteDrawingRange(sc, 0, data->DrawingCount);
            delete data;
            sc.SetPersistentPointer(1, nullptr);
        }
        return;
    }

    if (data == nullptr)
    {
        data = new CalendarData();
        sc.SetPersistentPointer(1, data);
    }

    if (sc.ArraySize < 1)
        return;

    const SCDateTime now = sc.CurrentSystemDateTime;
    const double secondsSinceLoad = (now - data->LastLoad).GetAsDouble() * 86400.0;

    const bool needReload = !data->Loaded
                         || sc.UpdateStartIndex == 0
                         || secondsSinceLoad >= In_ReloadSeconds.GetInt();

    const std::string currencyList = NormalizeCurrencyList(In_Currencies.GetString());

    // --- CSV beolvasás ---
    if (needReload)
    {
        std::vector<EconEvent> loaded;
        const std::string path = ResolvePath(sc, In_File.GetString());

        data->LastLoad = now;
        data->Loaded   = true;

        if (LoadCsv(sc, path, In_SourceTZ.GetTimeZone(), data->Events, loaded))
            data->Events.swap(loaded);
    }

    // --- Chart-geometria (pixel / gyertya, régió magassága) ---
    const int last = sc.ArraySize - 1;
    double xLast     = 0.0;
    double pxPerBar  = 0.0;
    if (last >= 1)
    {
        xLast    = (double)sc.BarIndexToXPixelCoordinate(last);
        pxPerBar = xLast - (double)sc.BarIndexToXPixelCoordinate(last - 1);
    }
    const int regionH = sc.StudyRegionBottomCoordinate - sc.StudyRegionTopCoordinate;

    const bool geometryChanged = std::fabs(pxPerBar - data->LastPxPerBar) > 0.01
                              || regionH != data->LastRegionH
                              || sc.ArraySize != data->LastArraySize;

    // --- Rajzolás: újraolvasáskor vagy ha a chart nézete változott ---
    if (needReload || geometryChanged)
    {
        data->LastPxPerBar  = pxPerBar;
        data->LastRegionH   = regionH;
        data->LastArraySize = sc.ArraySize;

        const int showH = In_ShowHigh.GetYesNo();
        const int showM = In_ShowMedium.GetYesNo();
        const int showL = In_ShowLow.GetYesNo();

        // 1. Csoportosítás gyertyánként (csak a megjelenítési időszak eseményei)
        SCDateTime weekStart, weekEnd;
        if (In_PeriodMode.GetIndex() == 0)
            CurrentDayBounds(now, In_DayChangeTime.GetTime(), weekStart, weekEnd);
        else
            CurrentWeekBounds(now, In_SundayRollover.GetYesNo(), In_RolloverTime.GetTime(),
                              weekStart, weekEnd);

        std::vector<BarGroup> groups;
        for (const EconEvent& ev : data->Events)
        {
            if (ev.Time < weekStart || ev.Time >= weekEnd) continue;
            if (!ImpactEnabled(ev.Impact, showH, showM, showL)) continue;
            if (!CurrencyAllowed(currencyList, ev.Currency)) continue;

            SCDateTime barTime;
            int barIndex = 0;
            if (!SnapToBar(sc, ev.Time, barTime, barIndex)) continue;

            if (groups.empty() || groups.back().BarIndex != barIndex)
            {
                BarGroup g;
                g.BarTime  = barTime;
                g.BarIndex = barIndex;
                groups.push_back(g);
            }
            groups.back().Items.push_back(&ev);
        }

        // 2. Méretek pixelben (becslés a betűméretből)
        const double corr     = In_SizeCorrection.GetInt() / 100.0;
        const double fontPx   = In_FontSize.GetInt() * 96.0 / 72.0 * corr;
        const double charPx   = fontPx * 0.62;          // átlagos karakterszélesség
        const double lineHPx  = fontPx * 1.45;          // sormagasság
        const double gapPx    = charPx * 1.5;           // vízszintes biztonsági rés
        const double barPx    = (pxPerBar > 0.5) ? pxPerBar : 6.0;

        double step = (double)In_LabelStep.GetInt();
        if (regionH > 20)
        {
            const double autoStep = lineHPx / regionH * 100.0;
            if (autoStep > step) step = autoStep;
        }

        const double top    = (double)In_LabelTop.GetInt();
        const double minPos = 2.0;
        int maxRows = (int)std::floor((top - minPos) / step) + 1;
        if (maxRows < 1) maxRows = 1;

        // Minden sorhoz: a legjobb oldali foglalt X pixel (a címkék jobb széle = vonal)
        std::vector<double> rowRight(maxRows, -1e18);

        int n = 0;   // rajzszám-számláló

        for (const BarGroup& g : groups)
        {
            int maxImpact = 0;
            for (const EconEvent* e : g.Items)
                if (e->Impact > maxImpact) maxImpact = e->Impact;

            COLORREF lineColor = (maxImpact == 2) ? In_ColorHigh.GetColor()
                               : (maxImpact == 1) ? In_ColorMedium.GetColor()
                                                  : In_ColorLow.GetColor();

            s_UseTool line;
            line.Clear();
            line.ChartNumber   = sc.ChartNumber;
            line.Region        = sc.GraphRegion;
            line.DrawingType   = DRAWING_VERTICALLINE;
            line.LineNumber    = LINE_NUMBER_BASE + n++;
            line.BeginDateTime = g.BarTime;
            line.Color         = lineColor;
            line.LineWidth     = In_LineWidth.GetInt();
            line.LineStyle     = (maxImpact == 2) ? LINESTYLE_SOLID : LINESTYLE_DASH;
            line.AddMethod     = UTAM_ADD_OR_ADJUST;
            sc.UseTool(line);

            if (!In_ShowLabels.GetYesNo())
                continue;

            // A vonal X pozíciója (múltbeli és jövőbeli gyertyára is lineárisan)
            const double lineX = xLast + (double)(g.BarIndex - last) * barPx;

            for (const EconEvent* e : g.Items)
            {
                COLORREF c = (e->Impact == 2) ? In_ColorHigh.GetColor()
                           : (e->Impact == 1) ? In_ColorMedium.GetColor()
                                              : In_ColorLow.GetColor();

                SCString label;
                if (In_ShowTime.GetYesNo())
                    label.Format("%02d:%02d (%s) %s %s ",
                                 e->Time.GetHour(), e->Time.GetMinute(), DayAbbrev(e->Time),
                                 e->Currency.c_str(), e->Title.c_str());
                else
                    label.Format("(%s) %s %s ", DayAbbrev(e->Time),
                                 e->Currency.c_str(), e->Title.c_str());

                // A címke balra nyúlik a vonaltól: [leftX, lineX]
                const double leftX = lineX - label.GetLength() * charPx;

                // Első szabad sor, ahol a korábbi címke jobb széle a mostani bal széle előtt véget ér
                int row = -1;
                for (int r = 0; r < maxRows; ++r)
                {
                    if (rowRight[r] + gapPx <= leftX)
                    {
                        row = r;
                        break;
                    }
                }
                // Ha nincs szabad sor: a legkorábban felszabaduló (legkisebb átfedés)
                if (row < 0)
                {
                    row = 0;
                    for (int r = 1; r < maxRows; ++r)
                        if (rowRight[r] < rowRight[row]) row = r;
                }
                rowRight[row] = lineX;

                double pos = top - row * step;
                if (pos < minPos) pos = minPos;

                s_UseTool txt;
                txt.Clear();
                txt.ChartNumber   = sc.ChartNumber;
                txt.Region        = sc.GraphRegion;
                txt.DrawingType   = DRAWING_TEXT;
                txt.LineNumber    = LINE_NUMBER_BASE + n++;
                txt.BeginDateTime = g.BarTime;
                txt.UseRelativeVerticalValues = 1;
                txt.BeginValue    = (float)pos;
                txt.Color         = c;
                txt.FontSize      = In_FontSize.GetInt();
                txt.TextAlignment = DT_RIGHT | DT_VCENTER;
                txt.Text          = label;
                txt.AddMethod     = UTAM_ADD_OR_ADJUST;
                sc.UseTool(txt);
            }
        }

        // A korábbi rajzolásból megmaradt, már felesleges elemek törlése
        if (n < data->DrawingCount)
            DeleteDrawingRange(sc, n, data->DrawingCount);
        data->DrawingCount = n;
    }

    // --- Riasztás (pontos esemény-időre) ---
    const int alertMinutes = In_AlertMinutes.GetInt();
    if (alertMinutes > 0 && !sc.IsReplayRunning() && sc.UpdateStartIndex != 0)
    {
        const int alH = In_AlertHigh.GetYesNo();
        const int alM = In_AlertMedium.GetYesNo();
        const int alL = In_AlertLow.GetYesNo();

        for (EconEvent& ev : data->Events)
        {
            if (ev.AlertFired) continue;

            const double minutesTo = (ev.Time - now).GetAsDouble() * 1440.0;

            if (minutesTo <= 0.0)
            {
                ev.AlertFired = true;
                continue;
            }

            if (!ImpactEnabled(ev.Impact, alH, alM, alL)) continue;
            if (!CurrencyAllowed(currencyList, ev.Currency)) continue;

            if (minutesTo <= alertMinutes)
            {
                SCString msg;
                msg.Format("HÍR %d perc múlva (%02d:%02d): %s %s",
                           (int)(minutesTo + 0.5), ev.Time.GetHour(), ev.Time.GetMinute(),
                           ev.Currency.c_str(), ev.Title.c_str());
                sc.SetAlert(In_AlertSound.GetAlertSoundNumber() - 1, msg);
                ev.AlertFired = true;
            }
        }
    }
}

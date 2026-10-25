#define WIN32_LEAN_AND_MEAN
#include "cleanfiles.h"
#include <shlwapi.h>
#include <sstream>
#include <algorithm>
#include <set>
#include <vector>
#include <functional>
#include <cwctype>

static std::wstring gEnv(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    if (n == 0 || n >= 1024) return L"";
    return buf;
}

static std::wstring ToLower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

static std::wstring TrimTrail(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/'))
        s.pop_back();
    return s;
}

// Hard blocklist for deletion (double safety net even inside curated dirs).
static bool BlockedForDelete(const std::wstring& path) {
    static const wchar_t* exts[] = { L".dll", L".exe", L".sys", L".msi", L".msp", L".evtx" };
    size_t dot = path.rfind(L'.');
    if (dot != std::wstring::npos) {
        std::wstring e = path.substr(dot);
        for (auto x : exts) if (_wcsicmp(e.c_str(), x) == 0) return true;
    }
    std::wstring lower = ToLower(path);
    static const wchar_t* pats[] = {
        L"pagefile.sys", L"swapfile.sys", L"hiberfil.sys",
        L"ntuser.dat*", L"usrclass.dat*", L"desktop.ini", L"thumbs.db",
        L"boot*.dat", L"bcd", L"bcd.log*", L"*.vhd", L"*.vhdx"
    };
    size_t fn = lower.find_last_of(L'\\');
    std::wstring fname = fn == std::wstring::npos ? lower : lower.substr(fn + 1);
    for (auto p : pats) if (PathMatchSpecW(fname.c_str(), p)) return true;
    return false;
}

// Case-insensitive prefix check: path lives under base dir (with '\' anchor).
static bool PathUnder(const std::wstring& path, const std::wstring& baseDir) {
    std::wstring p = ToLower(path);
    std::wstring b = ToLower(baseDir);
    while (!b.empty() && b.back() == L'\\') b.pop_back();
    if (b.empty()) return false;
    if (p.size() < b.size()) return false;
    if (p.compare(0, b.size(), b) != 0) return false;
    if (p.size() == b.size()) return true;
    return p[b.size()] == L'\\';
}

// Walks a directory (files only, junction/reparse dirs skipped) and reports:
//   - for WM_COUNT:   calls onFile for every file (size accumulated)
//   - for WM_COLLECT: inserts unique file paths into out (size accumulated)
enum WalkMode { WM_COUNT, WM_COLLECT };

static void WalkDir(const std::wstring& dir, int depth, const std::wstring& filter, bool recursive,
                    WalkMode mode, std::set<std::wstring>* out, uint64_t* size,
                    int* denied, std::atomic<bool>* cancel,
                    const std::function<void(const std::wstring&, uint64_t)>& onFile = nullptr) {
    if (cancel && cancel->load()) return;
    if (depth > 8) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED && denied) (*denied)++;
        return;
    }
    do {
        if (cancel && cancel->load()) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue; // junction loop guard
            if (recursive)
                WalkDir(full, depth + 1, filter, recursive, mode, out, size, denied, cancel, onFile);
            continue;
        }
        if (!filter.empty() && !PathMatchSpecW(fd.cFileName, filter.c_str())) continue;
        ULARGE_INTEGER li;
        li.LowPart = fd.nFileSizeLow;
        li.HighPart = fd.nFileSizeHigh;
        if (mode == WM_COUNT) {
            if (size) *size += li.QuadPart;
            if (onFile) onFile(full, li.QuadPart);
        } else {
            if (out && out->insert(full).second && size) *size += li.QuadPart;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ==== wildcard expansion for intermediate path components ====
// "C:\x\Profiles\*\cache2\entries" -> list of concrete existing dirs.
static void ExpandDirPattern(const std::wstring& pattern, std::vector<std::wstring>& out) {
    std::wstring p = TrimTrail(pattern);
    std::vector<std::wstring> segs;
    size_t start = 0;
    while (true) {
        size_t pos = p.find(L'\\', start);
        if (pos == std::wstring::npos) { segs.push_back(p.substr(start)); break; }
        segs.push_back(p.substr(start, pos - start));
        start = pos + 1;
    }
    std::vector<std::wstring> bases;
    std::wstring acc;
    bool wild = false;
    size_t i = 0;
    for (; i < segs.size(); i++) {
        if (segs[i].find(L'*') != std::wstring::npos) { wild = true; break; }
        if (!acc.empty()) acc += L"\\";
        acc += segs[i];
    }
    if (!wild) { // no wildcards — pattern is a concrete path
        out.push_back(acc.empty() ? p : acc);
        return;
    }
    bases.push_back(acc);
    for (; i < segs.size(); i++) {
        std::vector<std::wstring> next;
        if (segs[i].find(L'*') != std::wstring::npos) {
            for (auto& b : bases) {
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((b + L"\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                        if (PathMatchSpecW(fd.cFileName, segs[i].c_str()))
                            next.push_back(b + L"\\" + fd.cFileName);
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        } else {
            for (auto& b : bases) next.push_back(b + L"\\" + segs[i]);
        }
        bases = std::move(next);
        if (bases.empty()) break;
    }
    for (auto& b : bases) out.push_back(b);
}

// Resolve a source into a list of concrete base directories (expanding '*').
static void ResolveDirs(const CatSource& s, std::vector<std::wstring>& dirs) {
    if (s.dir.empty()) return;
    if (s.dir.find(L'*') == std::wstring::npos) { dirs.push_back(TrimTrail(s.dir)); return; }
    ExpandDirPattern(s.dir, dirs);
}

// ================================================================ categories

std::wstring FormatBytes(uint64_t b) {
    wchar_t tmp[64];
    if (b >= ((uint64_t)1 << 30))
        swprintf_s(tmp, 64, L"%.2f ГБ", (double)b / (double)((uint64_t)1 << 30));
    else if (b >= ((uint64_t)1 << 20))
        swprintf_s(tmp, 64, L"%.1f МБ", (double)b / (double)((uint64_t)1 << 20));
    else if (b >= 1024)
        swprintf_s(tmp, 64, L"%.0f КБ", (double)b / 1024.0);
    else
        swprintf_s(tmp, 64, L"%llu Б", (unsigned long long)b);
    return tmp;
}

std::vector<CleanCategory> BuildCategories() {
    std::vector<CleanCategory> cats;
    std::wstring temp   = gEnv(L"TEMP");
    std::wstring sysR   = gEnv(L"SystemRoot");   if (sysR.empty()) sysR = L"C:\\Windows";
    std::wstring local  = gEnv(L"LOCALAPPDATA");
    std::wstring roam   = gEnv(L"APPDATA");
    std::wstring prog86 = gEnv(L"ProgramFiles(x86)");
    std::wstring pdata  = gEnv(L"ProgramData");
    std::wstring home   = gEnv(L"USERPROFILE");

    auto L = [&](const wchar_t* name, const wchar_t* desc, int grp, bool adm, bool clean,
                 std::initializer_list<CatSource> srcs, const wchar_t* kind = L"") {
        CleanCategory c;
        c.name = name;
        c.desc = desc;
        c.group = grp;
        c.requiresAdmin = adm;
        c.canClean = clean;
        c.srcs = srcs;
        c.kind = kind;
        if (kind == L"wu") c.safetyLevel = 2;
        cats.push_back(std::move(c));
    };
    auto S = [](const std::wstring& dir, const std::wstring& flt = L"", bool rec = true) {
        CatSource s; s.dir = dir; s.filter = flt; s.recursive = rec; return s;
    };

    // ---------------- Пользователь ----------------
    if (!temp.empty())
        L(L"Пользовательские Temp", L"Временные файлы текущего пользователя (%TEMP%)",
          CG_USER, false, true, { S(temp) });

    if (!local.empty()) {
        L(L"Internet Cache", L"Кэш интернета пользователя (INetCache), пересоздаётся системой",
          CG_USER, false, true, { S(local + L"\\Microsoft\\Windows\\INetCache") });
        L(L"Эскизы и значки", L"Кэш эскизов и значков Проводника (thumbcache / iconcache)",
          CG_USER, false, true, { S(local + L"\\Microsoft\\Windows\\Explorer", L"thumbcache_*.db", false),
                                  S(local + L"\\Microsoft\\Windows\\Explorer", L"iconcache_*.db", false) });
        L(L"Chrome Cache", L"Кэш браузера Chrome (Cache / Code Cache / GPUCache)",
          CG_USER, false, true, { S(local + L"\\Google\\Chrome\\User Data\\Default\\Cache"),
                                  S(local + L"\\Google\\Chrome\\User Data\\Default\\Code Cache"),
                                  S(local + L"\\Google\\Chrome\\User Data\\Default\\GPUCache") });
        L(L"Edge Cache", L"Кэш браузера Microsoft Edge",
          CG_USER, false, true, { S(local + L"\\Microsoft\\Edge\\User Data\\Default\\Cache") });
        L(L"Firefox Cache", L"Кэш записей Firefox (cache2/entries)",
          CG_USER, false, true, { S(local + L"\\Mozilla\\Firefox\\Profiles\\*\\cache2\\entries") });
        L(L"Opera Cache", L"Кэш браузера Opera",
          CG_USER, false, true, { S(local + L"\\Opera Software\\Opera Stable\\Cache") });
        L(L"Brave Cache", L"Кэш браузера Brave",
          CG_USER, false, true, { S(local + L"\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Cache") });
        L(L"NVIDIA DXCache", L"Кэш шейдеров NVIDIA DirectX",
          CG_USER, false, true, { S(local + L"\\NVIDIA\\DXCache") });
        L(L"NVIDIA GLCache", L"Кэш шейдеров NVIDIA OpenGL",
          CG_USER, false, true, { S(local + L"\\NVIDIA\\GLCache") });
        L(L"NVIDIA ComputeCache", L"Кэш компиляции Compute NVIDIA",
          CG_USER, false, true, { S(local + L"\\NVIDIA\\ComputeCache") });
        L(L"AMD ShaderCache", L"Кэш шейдеров AMD",
          CG_USER, false, true, { S(local + L"\\AMD\\DXCache") });
        L(L"Intel ShaderCache", L"Кэш шейдеров Intel",
          CG_USER, false, true, { S(local + L"\\Intel\\ShaderCache") });
        L(L"Epic Games Launcher", L"Web-кэш Epic Games Launcher",
          CG_USER, false, true, { S(local + L"\\EpicGamesLauncher\\Saved\\webcache") });
        L(L"pip Cache", L"Кэш пакетов Python (pip)",
          CG_USER, false, true, { S(local + L"\\pip\\Cache") });
        L(L"npm Cache", L"Кэш пакетов Node (npm)",
          CG_USER, false, true, { S(local + L"\\npm-cache") });
    }
    if (!roam.empty()) {
        L(L"Недавние файлы", L"Список недавних документов пользователя",
          CG_USER, false, true, { S(roam + L"\\Microsoft\\Windows\\Recent") });
        L(L"Discord Cache", L"Кэш Discord (Cache / Code Cache)",
          CG_USER, false, true, { S(roam + L"\\discord\\Cache"), S(roam + L"\\discord\\Code Cache") });
        L(L"Telegram Cache", L"Кэш Telegram Desktop",
          CG_USER, false, true, { S(roam + L"\\Telegram Desktop\\tdata\\user_data\\cache") });
        L(L"VS Code Cache", L"Кэш редактора VS Code",
          CG_USER, false, true, { S(roam + L"\\Code\\Cache") });
        L(L"Adobe Media Cache", L"Медиа-кэш Adobe",
          CG_USER, false, true, { S(roam + L"\\Adobe\\Common\\Media Cache Files") });
        L(L"Java Cache", L"Кэш Java Deployment",
          CG_USER, false, true, { S(roam + L"\\Sun\\Java\\Deployment\\cache") });
    }
    if (!prog86.empty())
        L(L"Steam appcache", L"Кэш приложений Steam",
          CG_USER, false, true, { S(prog86 + L"\\Steam\\appcache") });
    if (!pdata.empty())
        L(L"Battle.net Cache", L"Кэш Battle.net",
          CG_USER, false, true, { S(pdata + L"\\Blizzard Entertainment\\Battle.net\\Cache") });
    if (!home.empty() && prog86.empty())
        L(L"Python __pycache__", L"Скомпилированные кэши Python по профилю",
          CG_USER, false, true, { S(home + L"\\**\\__pycache__") });

    // ---------------- Система ----------------
    L(L"Системные Temp", L"Временные файлы системы (Windows\\Temp). Часть может быть занята.",
      CG_SYSTEM, true, true, { S(sysR + L"\\Temp") });
    L(L"Prefetch", L"Кэш предзагрузки приложений. Пересоздаётся Windows.",
      CG_SYSTEM, true, true, { S(sysR + L"\\Prefetch") });

    L(L"Windows Logs", L"Журналы Windows (Logs\\*.log). Активные журналы событий (.evtx) не затрагиваются.",
      CG_SYSTEM, true, true, { S(sysR + L"\\Logs", L"*.log") });

    L(L"Setup Logs", L"Журналы установки Windows (Panther, setupact/setuperr, setupapi)",
      CG_SYSTEM, true, true, { S(sysR + L"\\Panther", L"*.log", false),
                               S(sysR + L"\\inf", L"setupapi*.log", false),
                               S(sysR, L"setupact.log", false),
                               S(sysR, L"setuperr.log", false) });

    L(L"Font Cache", L"Кэш шрифтов Windows (FontCache*.dat). Пересоздаётся автоматически.",
      CG_SYSTEM, true, true, { S(sysR + L"\\ServiceProfiles\\LocalService\\AppData\\Local", L"FontCache*", false),
                               S(sysR + L"\\ServiceProfiles\\LocalService\\AppData\\Local\\FontCache"),
                               S(local + L"\\Microsoft\\Windows", L"FontCache*", false) });

    L(L"Windows Update Cache", L"Кэш загруженных обновлений (SoftwareDistribution\\Download). "
                               L"Очистка под контролем служб wuauserv/bits с возвратом в исходное состояние.",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\SoftwareDistribution\\Download"),
        S(sysR + L"\\SoftwareDistribution\\DeliveryOptimization") },
      L"wu");

    L(L"Windows Update DB Logs", L"Журналы базы Windows Update (DataStore\\Logs\\*.etl). База не удаляется.",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\SoftwareDistribution\\DataStore\\Logs", L"*.etl", false) },
      L"wu");

    if (!pdata.empty())
        L(L"Системные ошибки (WER)", L"Отчёты об ошибках Windows (ReportArchive / ReportQueue)",
          CG_SYSTEM, true, true,
          { S(pdata + L"\\Microsoft\\Windows\\WER\\ReportArchive"),
            S(pdata + L"\\Microsoft\\Windows\\WER\\ReportQueue") });

    L(L"Crash Dumps", L"Дампы сбоев (Minidump, MEMORY.DMP, CrashDumps)",
      CG_SYSTEM, true, true, { S(sysR + L"\\Minidump", L"*.dmp", false),
                               S(sysR, L"MEMORY.DMP", false),
                               S(local + L"\\CrashDumps") });

    L(L"Kernel Live Dump Files", L"Живые дампы ядра (LiveKernelReports)",
      CG_SYSTEM, true, true, { S(sysR + L"\\LiveKernelReports") });

    L(L"Installer Patch Cache", L"MSP-патчи Windows Installer. Удаление ломает Repair/Update/Uninstall — "
                                L"только просмотр, без удаления.",
      CG_SYSTEM, true, false, { S(sysR + L"\\Installer", L"*.msp", false) });

    L(L"Event Log Archives", L"Архивы журналов событий (*.evtx). Активные журналы не удаляются; "
                              L"безопасная очистка через API автоматически не выполняется.",
      CG_SYSTEM, true, false, { S(sysR + L"\\System32\\winevt\\Logs", L"*.evtx", false) });

    if (!pdata.empty())
        L(L"Defender Scan History", L"История проверок Microsoft Defender. Удаление системных данных "
                                    L"защитника не выполняется — категория диагностическая.",
          CG_SYSTEM, true, false, { S(pdata + L"\\Microsoft\\Windows Defender\\Scans\\History") });

    L(L"Windows Debug Logs", L"Отладочные журналы (Debug\\*.log): WIA, Ntfs, WFP и др.",
      CG_SYSTEM, true, true, { S(sysR + L"\\Debug", L"*.log") });

    L(L"Network Service Temp", L"Временные файлы системного профиля NetworkService.",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Temp") });

    L(L"Windows Caches", L"Кэш системного профиля (INetCache, Temp системы)",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\System32\\config\\systemprofile\\AppData\\Local\\Microsoft\\Windows\\INetCache"),
        S(sysR + L"\\System32\\config\\systemprofile\\AppData\\Local\\Temp") });

    if (!pdata.empty())
        L(L"Defender Support Logs", L"Служебные журналы поддержки Microsoft Defender. Только просмотр.",
          CG_SYSTEM, true, false, { S(pdata + L"\\Microsoft\\Windows Defender\\Support") });

    if (!pdata.empty())
        L(L"Windows Search Logs", L"Журналы Windows Search. Могут быть заняты службой поиска — только просмотр.",
          CG_SYSTEM, true, false,
          { S(pdata + L"\\Microsoft\\Search\\Data\\Applications\\Windows") });

    L(L"Diagnostic ETL Logs", L"ETL-журналы диагностики (wdi/LogFiles, System32\\LogFiles)",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\System32\\wdi\\LogFiles", L"*.etl"),
        S(sysR + L"\\System32\\LogFiles", L"*.etl") });

    L(L"Power Efficiency Reports", L"Отчёты энергопотребления (SleepStudy, energy-report.html)",
      CG_SYSTEM, true, true, { S(sysR + L"\\System32\\SleepStudy"),
                               S(sysR + L"\\System32", L"energy-report.html", false) });

    if (!pdata.empty()) {
        L(L"Defender Trace Files", L"ETL-трассы Microsoft Defender. Только просмотр (диагностика).",
          CG_SYSTEM, true, false,
          { S(pdata + L"\\Microsoft\\Windows Defender", L"*.etl", false),
            S(pdata + L"\\Microsoft\\Windows Defender\\Scans", L"*.etl", false) });
    }

    L(L"WinSAT Results", L"Результаты тестов WinSAT (DataStore). Пересоздаются по запросу.",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\Performance\\WinSAT\\DataStore", L"*.xml", false) });

    L(L"App Compatibility Cache", L"Кэш совместимости приложений хранится в реестре (AppCompatCache): "
                                  L"файлового кэша нет — категория информационная.",
      CG_SYSTEM, true, false, {}, L"regonly");

    L(L"Local Service Certificate Cache", L"Кэш сертификатов профиля LocalService. Безопасная очистка "
                                          L"не подтверждена — только просмотр.",
      CG_SYSTEM, true, false,
      { S(sysR + L"\\ServiceProfiles\\LocalService\\AppData\\Local\\Microsoft\\Windows\\Caches") });

    L(L"Network Service Certificate Cache", L"Кэш сертификатов профиля NetworkService. Безопасная "
                                             L"очистка не подтверждена — только просмотр.",
      CG_SYSTEM, true, false,
      { S(sysR + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\Caches") });

    L(L"System Certificate Cache", L"Кэш сертификатов системного профиля. Безопасная очистка "
                                   L"не подтверждена — только просмотр.",
      CG_SYSTEM, true, false,
      { S(sysR + L"\\System32\\config\\systemprofile\\AppData\\Local\\Microsoft\\Windows\\Caches") });

    L(L"WMI Trace Logs", L"Журналы трассировки WMI (wbem\\Logs). Занятые файлы пропускаются.",
      CG_SYSTEM, true, true, { S(sysR + L"\\System32\\wbem\\Logs") });

    L(L"Setup Cleanup Logs", L"Журналы установки (MoSetup, UnattendGC) — безопасные файлы логов.",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\Logs\\MoSetup"),
        S(sysR + L"\\Panther\\UnattendGC", L"*.log") });

    L(L"Update Orchestrator Logs", L"Журналы Update Orchestrator (MoUsoCoreWorker).",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\Logs\\MoUsoCoreWorker", L"*.log") });

    L(L"Delivery Optimization Cache", L"Кэш оптимизации доставки (DO Cache). Пересоздаётся при загрузке.",
      CG_SYSTEM, true, true,
      { S(sysR + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache"),
        S(sysR + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Logs") });

    return cats;
}

// ================================================================ scanning

ScanReport ScanCategories(std::vector<CleanCategory>& cats, std::atomic<bool>* cancel,
                          void (*progress)(void*, const wchar_t*, double)) {
    ScanReport r;
    size_t total = cats.size();
    for (size_t i = 0; i < cats.size(); i++) {
        auto& c = cats[i];
        if (cancel && cancel->load()) break;
        c.size = 0; c.count = 0; c.denied = 0; c.status = CS_NONE;
        for (auto& s : c.srcs) {
            if (cancel && cancel->load()) break;
            std::vector<std::wstring> dirs;
            ResolveDirs(s, dirs);
            std::wstring flt = s.filter;
            for (auto& d : dirs) {
                if (cancel && cancel->load()) break;
                uint64_t ssz = 0;
                int nfiles = 0;
                int deniedBefore = c.denied;
                WalkDir(d, 0, flt, s.recursive, WM_COUNT, nullptr, &ssz, &c.denied, cancel,
                        [&](const std::wstring&, uint64_t) { nfiles++; });
                // Note: a dir that vanished may have been counted as PATH_NOT_FOUND earlier —
                // only ACCESS_DENIED bumps 'denied', which stays honest for missing dirs.
                (void)deniedBefore;
                c.size += ssz;
                c.count += nfiles;
            }
        }
        if (c.srcs.empty()) c.status = CS_NOSRC;
        else if (c.denied > 0) c.status = c.count > 0 ? CS_PARTIAL : CS_DENIED;
        else c.status = CS_OK;
        r.totalSize += c.size;
        r.totalFiles += c.count;
        if (progress) progress((void*)1, c.name.c_str(), total ? (double)(i + 1) / (double)total : 1.0);
    }
    return r;
}

// ================================================================ cleaning

namespace {

struct SvcState {
    bool exists = false;
    bool wasRunning = false;
    bool nowStopped = false;
};

static SvcState SvcStopForClean(const wchar_t* name, bool* failure) {
    SvcState st;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { *failure = true; return st; }
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
    if (!svc) { CloseServiceHandle(scm); return st; } // service doesn't exist -> nothing to stop
    SERVICE_STATUS ss;
    st.exists = QueryServiceStatus(svc, &ss) != FALSE;
    if (!st.exists) { CloseServiceHandle(svc); CloseServiceHandle(scm); return st; }
    if (ss.dwCurrentState == SERVICE_RUNNING) {
        st.wasRunning = true;
        if (!ControlService(svc, SERVICE_CONTROL_STOP, &ss)) {
            *failure = true; // could not stop -> caller must not delete
            CloseServiceHandle(svc); CloseServiceHandle(scm); return st;
        }
        DWORD tries = 0;
        while (tries++ < 40) {
            Sleep(250);
            if (QueryServiceStatus(svc, &ss) && ss.dwCurrentState == SERVICE_STOPPED) { st.nowStopped = true; break; }
        }
        if (!st.nowStopped) *failure = true;
    } else {
        st.nowStopped = (ss.dwCurrentState == SERVICE_STOPPED || ss.dwCurrentState == SERVICE_PAUSED ||
                         ss.dwCurrentState == SERVICE_STOP_PENDING);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return st;
}

static bool SvcStart(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    bool ok = StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) ok = true;
    if (ok) {
        DWORD tries = 0;
        SERVICE_STATUS ss;
        ok = false;
        while (tries++ < 40) {
            Sleep(250);
            if (QueryServiceStatus(svc, &ss)) {
                if (ss.dwCurrentState == SERVICE_RUNNING) { ok = true; break; }
                if (ss.dwCurrentState == SERVICE_STOPPED) break;
            }
        }
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

} // namespace

CleanReportData CleanCategories(const std::vector<CleanCategory>& cats, std::atomic<bool>* cancel,
                                void (*progress)(void*, int cur, int total)) {
    CleanReportData report;

    struct Job { const CleanCategory* cat; std::vector<std::wstring> files; };
    std::vector<Job> jobs;
    int totalFiles = 0;

    // Build jobs (files anchored to their category roots + blocklist).
    for (auto& c : cats) {
        if (cancel && cancel->load()) break;
        if (c.scanOnly || !c.canClean) continue;
        Job j; j.cat = &c;
        for (auto& s : c.srcs) {
            std::vector<std::wstring> dirs;
            ResolveDirs(s, dirs);
            std::wstring flt = s.filter;
            for (auto& d : dirs) {
                WalkDir(d, 0, flt, s.recursive, WM_COUNT, nullptr, nullptr, nullptr, cancel,
                        [&](const std::wstring& p, uint64_t) {
                            if (!PathUnder(p, d)) return;
                            if (BlockedForDelete(p)) return;
                            j.files.push_back(p);
                        });
            }
        }
        if (!j.files.empty()) { totalFiles += (int)j.files.size(); jobs.push_back(std::move(j)); }
    }

    int done = 0;
    for (auto& j : jobs) {
        if (cancel && cancel->load()) break;

        bool wu = (j.cat->kind == L"wu");
        if (wu) {
            bool fail = false;
            SvcState wuS = SvcStopForClean(L"wuauserv", &fail);
            SvcState bitsS = SvcStopForClean(L"bits", &fail);
            if (fail) {
                // Could not stop services — do not touch WU files at all.
                report.skipped += (int)j.files.size();
                done += (int)j.files.size();
                if (progress) progress((void*)1, done, totalFiles);
                continue;
            }
            for (auto& f : j.files) {
                if (cancel && cancel->load()) break;
                DWORD attrs = GetFileAttributesW(f.c_str());
                uint64_t fsz = 0;
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    WIN32_FIND_DATAW fd;
                    HANDLE h = FindFirstFileW(f.c_str(), &fd);
                    if (h != INVALID_HANDLE_VALUE) {
                        ULARGE_INTEGER li; li.LowPart = fd.nFileSizeLow; li.HighPart = fd.nFileSizeHigh;
                        fsz = li.QuadPart; FindClose(h);
                    }
                    if (SetFileAttributesW(f.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY) && DeleteFileW(f.c_str())) {
                        report.removed += fsz; report.deleted++;
                    } else report.skipped++;
                } else report.skipped++;
                done++;
                if (progress) progress((void*)1, done, totalFiles);
            }
            // Restore services to original state; verify.
            if (wuS.wasRunning)  { if (!SvcStart(L"wuauserv")) report.serviceIssue = true; }
            if (bitsS.wasRunning){ if (!SvcStart(L"bits"))      report.serviceIssue = true; }
        } else {
            for (auto& f : j.files) {
                if (cancel && cancel->load()) break;
                DWORD attrs = GetFileAttributesW(f.c_str());
                uint64_t fsz = 0;
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    WIN32_FIND_DATAW fd;
                    HANDLE h = FindFirstFileW(f.c_str(), &fd);
                    if (h != INVALID_HANDLE_VALUE) {
                        ULARGE_INTEGER li; li.LowPart = fd.nFileSizeLow; li.HighPart = fd.nFileSizeHigh;
                        fsz = li.QuadPart; FindClose(h);
                    }
                    if (SetFileAttributesW(f.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY) && DeleteFileW(f.c_str())) {
                        report.removed += fsz; report.deleted++;
                    } else report.skipped++;
                } else report.skipped++;
                done++;
                if (progress) progress((void*)1, done, totalFiles);
            }
        }
    }
    return report;
}
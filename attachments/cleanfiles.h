#pragma once
#ifndef CLEANFILES_H
#define CLEANFILES_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

// Per-category scan status (honest results, never fake "0 МБ" for denied access).
enum CatStatus {
    CS_NONE = 0,     // not scanned yet
    CS_OK,           // fully scanned
    CS_PARTIAL,      // some roots were inaccessible
    CS_DENIED,       // no access to any root
    CS_NOSRC         // category has no file sources (registry/info category)
};

// Grouping: 0 = Пользователь, 1 = Система
enum CatGroup { CG_USER = 0, CG_SYSTEM = 1 };

struct CatSource {
    std::wstring dir;       // base directory (env-expanded)
    std::wstring filter;    // if set: file-name wildcard, non-recursive (e.g. "*.msp", "FontCache*")
    bool recursive = true;  // descend into subdirectories (files only)
    bool operator==(const CatSource& o) const {
        return dir == o.dir && filter == o.filter && recursive == o.recursive;
    }
};

struct CleanCategory {
    std::wstring name;
    std::wstring desc;
    int group = CG_USER;
    std::vector<CatSource> srcs;
    bool requiresAdmin = false;   // read/write needs elevated privileges
    bool canClean = true;         // deletion allowed (after safety checks)
    bool scanOnly = false;        // informational only — never cleaned, always reported
    std::wstring kind;            // L"" = plain files; L"wu" = Windows Update guarded by services
    int  safetyLevel = 1;         // 0 normal, 1 caution, 2 dangerous; used for reports

    // scan results
    uint64_t size = 0;
    int      count = 0;
    int      denied = 0;
    int      status = CS_NONE;
};

struct ScanReport {
    uint64_t totalSize = 0;
    int      totalFiles = 0;
};

struct CleanReportData {
    uint64_t removed = 0;
    int      deleted = 0;
    int      skipped = 0;
    bool     serviceIssue = false;  // services could not be returned to their original state
};

// Builds the full category list (real Windows paths, env-expanded).
std::vector<CleanCategory> BuildCategories();

// Rebuilds scan results for the given categories in place. Returns global totals.
ScanReport ScanCategories(std::vector<CleanCategory>& cats, std::atomic<bool>* cancel,
                          void (*progress)(void*, const wchar_t*, double));

// Deletes files of the selected categories (safety-anchored). Returns stats.
// On return, each category keeps its size/count/status for re-scan accuracy:
// call ScanCategories again on the cleaned categories for honest post-clean numbers.
CleanReportData CleanCategories(const std::vector<CleanCategory>& cats, std::atomic<bool>* cancel,
                                void (*progress)(void*, int cur, int total));

std::wstring FormatBytes(uint64_t b);

#endif
// exegrep
// License: MIT
#define NOMINMAX
#include <windows.h>
#include <shlwapi.h>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <new>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fcntl.h> // for _setmode

typedef std::wstring file_t;
typedef std::set<std::wstring> files_t;

enum RET
{
    RET_OK = 0,
    RET_FILE_NOT_FOUND = 1,
    RET_INVALID_ARG = 2,
};

struct ExeGrep
{
    bool m_help = false;
    bool m_version = false;
    bool m_recursive = false;
    bool m_case_sensitive = false;
    bool m_quiet = false;
    std::wstring m_pattern;
    std::set<std::wstring> m_items;

    RET main(INT argc, WCHAR **argv);
    RET parse(INT argc, WCHAR **argv);
    RET execute();

protected:
    RET search(const files_t& files);
    RET wildcard(files_t& files, const file_t& item);
    RET dir(files_t& files, const file_t& item);
    bool find(const file_t& file, std::vector<BYTE>& data);
    bool match(const std::vector<BYTE>& data);
    RET do_item(files_t& files, const file_t& item);
    void usage();
    void version();

    template <bool t_case_sensitive>
    static bool find_pattern_a(const std::vector<BYTE>& data, const std::string& pattern);

    template <bool t_case_sensitive>
    static bool find_pattern_w(const std::vector<BYTE>& data, const std::wstring& pattern);
};

void ExeGrep::version(void)
{
    wprintf(L"exegrep version 1.2 by katahiromz\n");
}

void ExeGrep::usage(void)
{
    wprintf(
        L"Usage: exegrep [OPTIONS] STRING [FILES]\n"
        L"\n"
        L"Options:\n"
        L"  -r          Recursive mode.\n"
        L"  -q          Quiet mode.\n"
        L"  -c          Case sensitive search.\n"
        L"  --help      Show this message.\n"
        L"  --version   Show version info.\n"
        L"\n"
        L"Contact me: katayama.hirofumi.mz@gmail.com\n"
    );
}

RET ExeGrep::dir(files_t& files, const file_t& item)
{
    WCHAR szPath[MAX_PATH];
    lstrcpynW(szPath, item.c_str(), _countof(szPath));
    PathAppendW(szPath, L"*");
    return wildcard(files, szPath);
}

RET ExeGrep::do_item(files_t& files, const file_t& item)
{
    DWORD attrs = GetFileAttributesW(item.c_str());
    if (attrs == (DWORD)-1) {
        fwprintf(stderr, L"exegrep: error: File not found: '%ls'\n", item.c_str());
        return RET_FILE_NOT_FOUND;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        files.emplace(item.c_str());
        return RET_OK;
    }

    if (m_recursive)
        return dir(files, item.c_str());

    return RET_OK;
}

RET ExeGrep::wildcard(files_t& files, const file_t& item)
{
    if (item.find(L'*') == item.npos && item.find(L'?') == item.npos)
        return do_item(files, item);

    WIN32_FIND_DATAW find;
    HANDLE hFind = FindFirstFileW(item.c_str(), &find);
    DWORD error = GetLastError();
    if (hFind == INVALID_HANDLE_VALUE) {
        if (!m_quiet && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            fwprintf(stderr, L"exegrep: warning: FindFirstFile failed: '%ls' (error code: %lu)\n",
                     item.c_str(), error);
        }
        return RET_OK;
    }

    WCHAR szDir[MAX_PATH];
    lstrcpynW(szDir, item.c_str(), _countof(szDir));
    PathRemoveFileSpecW(szDir);

    WCHAR szFile[MAX_PATH];
    RET ret = RET_OK;
    do {
        if (wcscmp(find.cFileName, L".") == 0 || wcscmp(find.cFileName, L"..") == 0)
            continue;

        lstrcpynW(szFile, szDir, _countof(szFile));
        PathAppendW(szFile, find.cFileName);

        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (m_recursive) {
                ret = dir(files, szFile);
                if (ret == RET_FILE_NOT_FOUND)
                    break;
            }
        } else {
            WCHAR szFull[MAX_PATH];
            GetFullPathNameW(szFile, _countof(szFull), szFull, NULL);
            files.emplace(szFull);
            ret = RET_OK;
        }
    } while (FindNextFileW(hFind, &find));
    FindClose(hFind);

    return ret;
}

static bool AnsiFromWide(UINT codepage, std::string& ansi, const std::wstring& wide)
{
    INT cchA = WideCharToMultiByte(codepage, 0, wide.c_str(), wide.size(), NULL, 0, NULL, NULL);
    if (cchA == 0) {
        return false;
    }

    ansi.resize(cchA);
    WideCharToMultiByte(codepage, 0, wide.c_str(), wide.size(), &ansi[0], ansi.size(), NULL, NULL);
    return true;
}

template <bool t_case_sensitive>
bool ExeGrep::find_pattern_a(const std::vector<BYTE>& data, const std::string& pattern)
{
#if 1 // Use Boyer-Moore algorithm
    size_t patlen = pattern.size();
    size_t datalen = data.size();
    if (patlen == 0 || patlen > datalen)
        return false;

    // Preprocess pattern to create the bad character skip table
    std::vector<size_t> skip(256, patlen);
    auto adjust_char = [](char ch) -> unsigned char {
        return static_cast<unsigned char>(t_case_sensitive ? ch : std::tolower(static_cast<unsigned char>(ch)));
    };

    for (size_t i = 0; i < patlen - 1; ++i)
        skip[adjust_char(pattern[i])] = patlen - 1 - i;

    const char* text = reinterpret_cast<const char*>(data.data());
    size_t i = 0;

    while (i <= datalen - patlen) {
        size_t j = patlen - 1;
        while (j < patlen) {
            char text_ch = text[i + j];
            char pat_ch = pattern[j];
            if (!t_case_sensitive) {
                text_ch = std::tolower(static_cast<unsigned char>(text_ch));
                pat_ch = std::tolower(static_cast<unsigned char>(pat_ch));
            }
            if (text_ch != pat_ch)
                break;
            if (j == 0)
                return true;
            --j;
        }

        i += skip[adjust_char(text[i + patlen - 1])];
    }

    return false;
#else
    size_t patlenA = pattern.size();
    if (patlenA == 0 || patlenA > data.size())
        return false;

    const char *pchA = (const char *)data.data();
    size_t cchEndA = data.size() - patlenA;
    if (t_case_sensitive) {
        for (size_t ich = 0; ich <= cchEndA; ++ich) {
            if (strncmp(&pchA[ich], pattern.c_str(), pattern.size()) == 0)
                return true;
        }
    } else {
        for (size_t ich = 0; ich <= cchEndA; ++ich) {
            if (_strnicmp(&pchA[ich], pattern.c_str(), pattern.size()) == 0)
                return true;
        }
    }

    return false;
#endif
}

template <bool t_case_sensitive>
bool ExeGrep::find_pattern_w(const std::vector<BYTE>& data, const std::wstring& pattern)
{
#if 1 // Use Boyer-Moore algorithm
    size_t patlen = pattern.size();
    size_t datalen = data.size() / sizeof(WCHAR);
    if (patlen == 0 || patlen > datalen)
        return false;

    const WCHAR* text = reinterpret_cast<const WCHAR*>(data.data());

    // Bad character shift table using unordered_map
    std::unordered_map<WCHAR, size_t> skip;
    auto adjust_char = [](WCHAR ch) -> WCHAR {
        return t_case_sensitive ? ch : towlower(ch);
    };

    size_t default_skip = patlen;
    for (size_t i = 0; i < patlen - 1; ++i)
        skip[adjust_char(pattern[i])] = patlen - 1 - i;

    size_t i = 0;
    while (i <= datalen - patlen) {
        size_t j = patlen - 1;
        while (j < patlen) {
            WCHAR text_ch = text[i + j];
            WCHAR pat_ch = pattern[j];
            if (!t_case_sensitive) {
                text_ch = towlower(text_ch);
                pat_ch = towlower(pat_ch);
            }
            if (text_ch != pat_ch)
                break;
            if (j == 0)
                return true;
            --j;
        }

        WCHAR next_char = adjust_char(text[i + patlen - 1]);
        i += skip.count(next_char) ? skip[next_char] : default_skip;
    }

    return false;
#else
    size_t patlenW = pattern.size();
    size_t datalenW = data.size() / sizeof(WCHAR);
    if (patlenW == 0 || patlenW > datalenW)
        return false;

    LPCWSTR pchW = (LPCWSTR)data.data();
    size_t cchEndW = datalenW - patlenW;
    if (t_case_sensitive) {
        for (size_t ich = 0; ich <= cchEndW; ++ich) {
            if (wcsncmp(&pchW[ich], pattern.c_str(), pattern.size()) == 0)
                return true;
        }
    } else {
        for (size_t ich = 0; ich <= cchEndW; ++ich) {
            if (_wcsnicmp(&pchW[ich], pattern.c_str(), pattern.size()) == 0)
                return true;
        }
    }

    return false;
#endif
}

bool ExeGrep::match(const std::vector<BYTE>& data)
{
    std::string patA;

    if (AnsiFromWide(CP_ACP, patA, m_pattern)) {
        if (m_case_sensitive) {
            if (find_pattern_a<true>(data, patA))
                return true;
        } else {
            if (find_pattern_a<false>(data, patA))
                return true;
        }
    }

    if (AnsiFromWide(CP_UTF8, patA, m_pattern)) {
        if (m_case_sensitive) {
            if (find_pattern_a<true>(data, patA))
                return true;
        } else {
            if (find_pattern_a<false>(data, patA))
                return true;
        }
    }

    if (m_case_sensitive) {
        return find_pattern_w<true>(data, m_pattern);
    } else {
        return find_pattern_w<false>(data, m_pattern);
    }
}

bool ExeGrep::find(const file_t& file, std::vector<BYTE>& data)
{
    DWORD dwFileShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE hFile = CreateFileW(file.c_str(), GENERIC_READ, dwFileShare, NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        fwprintf(stderr, L"exegrep: warning: Cannot open file: '%ls' (error code: %lu)\n", file.c_str(),
                 error);
        return false;
    }

    ULARGE_INTEGER FileSize;
    FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
    if (FileSize.LowPart == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        fwprintf(stderr, L"exegrep: warning: Cannot read file: '%ls'\n", file.c_str());
        CloseHandle(hFile);
        return false;
    }
    if (FileSize.QuadPart >= MAXLONG) {
        fwprintf(stderr, L"exegrep: warning: Too large file: '%ls'\n", file.c_str());
        CloseHandle(hFile);
        return false;
    }

    try {
        data.resize((SIZE_T)FileSize.QuadPart);
    } catch (const std::bad_alloc&) {
        fwprintf(stderr, L"exegrep: warning: Not enough memory for file: '%ls'\n", file.c_str());
        CloseHandle(hFile);
        return false;
    }

    bool matched = false;
    DWORD cbRead;
    if (ReadFile(hFile, data.data(), (DWORD)data.size(), &cbRead, NULL) &&
        cbRead == FileSize.QuadPart)
    {
        matched = match(data);
    } else {
        fwprintf(stderr, L"exegrep: warning: Cannot read file: '%ls'\n", file.c_str());
    }

    CloseHandle(hFile);
    return matched;
}

RET ExeGrep::search(const files_t& files)
{
    size_t total = files.size();
    size_t processed = 0;
    bool show_progress = total >= 100 && !m_quiet;
    double percent;

    std::vector<BYTE> data;
    for (auto& file : files) {
        if (show_progress) {
            percent = 100.0 * processed / total;
            fwprintf(stderr, L"\rProcessing: %zu/%zu files (%.1f%%)...              ",
                     processed, total, percent);
        }
        if (find(file, data)) {
            if (show_progress)
                wprintf(L"\n");
            wprintf(L"%ls\n", file.c_str());
        }

        ++processed;
    }

    if (show_progress)
        fwprintf(stderr, L"\rProcessed: %zu files.              \n", total);

    return RET_OK;
}

RET ExeGrep::execute()
{
    if (m_help) {
        usage();
        return RET_OK;
    }

    if (m_version) {
        version();
        return RET_OK;
    }

    files_t files;
    for (auto& item : m_items) {
        RET ret = wildcard(files, item);
        if (ret != RET_OK)
            return ret;
    }

    return search(files);
}

RET ExeGrep::parse(INT argc, WCHAR **argv)
{
    if (argc <= 1) {
        m_help = true;
        return RET_OK;
    }

    bool has_pattern = false;

    for (INT iarg = 1; iarg < argc; ++iarg) {
        auto arg = argv[iarg];
        if (_wcsicmp(arg, L"/?") == 0 || _wcsicmp(arg, L"-h") == 0 ||
            _wcsicmp(arg, L"--help") == 0 || _wcsicmp(arg, L"-help") == 0)
        {
            m_help = true;
            return RET_OK;
        }
        if (_wcsicmp(arg, L"/V") == 0 || _wcsicmp(arg, L"-v") == 0 ||
            _wcsicmp(arg, L"--version") == 0 || _wcsicmp(arg, L"-version") == 0)
        {
            m_version = true;
            return RET_OK;
        }
        if (_wcsicmp(arg, L"/R") == 0 || _wcsicmp(arg, L"-r") == 0 ||
            _wcsicmp(arg, L"--recursive") == 0 || _wcsicmp(arg, L"-recursive") == 0)
        {
            m_recursive = true;
            continue;
        }
        if (_wcsicmp(arg, L"/C") == 0 || _wcsicmp(arg, L"-c") == 0 ||
            _wcsicmp(arg, L"--case-sensitive") == 0 ||
            _wcsicmp(arg, L"-case-sensitive") == 0)
        {
            m_case_sensitive = true;
            continue;
        }
        if (_wcsicmp(arg, L"/Q") == 0 || _wcsicmp(arg, L"-q") == 0 ||
            _wcsicmp(arg, L"--quiet") == 0 || _wcsicmp(arg, L"-quiet") == 0)
        {
            m_quiet = true;
            continue;
        }
        if (!has_pattern)
        {
            m_pattern = arg;
            has_pattern = true;
            continue;
        }
        m_items.emplace(arg);
    }

    if (!has_pattern) {
        fwprintf(stderr, L"exegrep: error: No pattern specified\n");
        return RET_INVALID_ARG;
    }

    if (m_items.empty()) {
        m_items.emplace(L"*");
    }

    return RET_OK;
}

RET ExeGrep::main(INT argc, WCHAR **argv)
{
    RET ret = parse(argc, argv);
    if (ret != RET_OK)
        return ret;

    return execute();
}

INT wmain(INT argc, WCHAR **argv)
{
    // Unicode output support
    setlocale(LC_CTYPE, "");
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);

    ExeGrep exegrep;
    return exegrep.main(argc, argv);
}

int main(void)
{
    INT argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    INT ret = wmain(argc, argv);
    LocalFree(argv);
    return ret;
}

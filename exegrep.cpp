// exegrep
// License: MIT
#include <windows.h>
#include <shlwapi.h>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>

typedef std::wstring file_t;
typedef std::vector<std::wstring> files_t;

enum RET
{
    RET_OK = 0,
    RET_FILE_NOT_FOUND = 1,
    RET_INVALID_ARG = 2,
};

bool g_recursive = false;
LPCWSTR g_pattern = NULL;

void version(void)
{
    wprintf(L"exegrep version 1.1 by katahiromz\n");
}

void usage(void)
{
    wprintf(
        L"Usage: exegrep [OPTIONS] STRING [FILES]\n"
        L"\n"
        L"Options:\n"
        L"  -r          Recursive mode.\n"
        L"  --help      Show this message.\n"
        L"  --version   Show version info.\n"
        L"\n"
        L"Contact me: katayama.hirofumi.mz@gmail.com\n"
    );
}

INT exegrep_wildcard(files_t& files, const file_t& item);

INT exegrep_dir(files_t& files, const file_t& item)
{
    WCHAR szPath[MAX_PATH];
    lstrcpynW(szPath, item.c_str(), _countof(szPath));
    PathAppendW(szPath, L"*");
    return exegrep_wildcard(files, szPath);
}

INT exegrep_item(files_t& files, const file_t& item)
{
    DWORD attrs = GetFileAttributesW(item.c_str());
    if (attrs == (DWORD)-1)
    {
        fwprintf(stderr, L"exegrep: error: File not found: '%ls'\n", item.c_str());
        return RET_FILE_NOT_FOUND;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        files.push_back(item);
        return RET_OK;
    }

    if (g_recursive)
        return exegrep_dir(files, item);

    return RET_OK;
}

INT exegrep_wildcard(files_t& files, const file_t& item)
{
    if (item.find(L'*') == item.npos && item.find(L'?') == item.npos)
        return exegrep_item(files, item);

    WIN32_FIND_DATAW find;
    HANDLE hFind = FindFirstFileW(item.c_str(), &find);
    if (hFind == INVALID_HANDLE_VALUE)
        return RET_OK;

    WCHAR szDir[MAX_PATH];
    lstrcpynW(szDir, item.c_str(), _countof(szDir));
    PathRemoveFileSpecW(szDir);

    WCHAR szFile[MAX_PATH];
    INT ret = RET_OK;
    do
    {
        if (wcscmp(find.cFileName, L".") == 0 || wcscmp(find.cFileName, L"..") == 0)
            continue;

        lstrcpynW(szFile, szDir, _countof(szFile));
        PathAppendW(szFile, find.cFileName);

        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (g_recursive)
            {
                ret = exegrep_dir(files, szFile);
                if (ret == RET_FILE_NOT_FOUND)
                    break;
            }
        }
        else
        {
            files.push_back(szFile);
            ret = RET_OK;
        }
    } while (FindNextFileW(hFind, &find));
    FindClose(hFind);

    return ret;
}

void exegrep_sort_unique(files_t& files)
{
    std::sort(files.begin(), files.end());
    auto last = std::unique(files.begin(), files.end());
    files.erase(last, files.end());
}

bool exegrep_match(const std::vector<BYTE>& data)
{
    std::wstring patW = g_pattern;

    std::string patA;
    bool is_ascii = true;
    for (auto wch : patW)
    {
        if (wch > 0xFF)
            is_ascii = false;
        patA += (char)wch;
    }

    if (is_ascii)
    {
        size_t patlenA = patA.size();
        if (patlenA > data.size())
            return false;

        LPCSTR pchA = (LPCSTR)data.data();
        size_t cchEndA = data.size() - patlenA;
        for (size_t ich = 0; ich < cchEndA; ++ich)
        {
            if (_strnicmp(&pchA[ich], patA.c_str(), patA.size()) == 0)
                return true;
        }
    }

    size_t patlenW = patW.size();
    size_t datalenW = data.size() / sizeof(WCHAR);
    if (patlenW > datalenW)
        return false;

    LPCWSTR pchW = (LPCWSTR)data.data();
    size_t cchEndW = datalenW - patlenW;
    for (size_t ich = 0; ich < cchEndW; ++ich)
    {
        if (_wcsnicmp(&pchW[ich], patW.c_str(), patW.size()) == 0)
            return true;
    }

    return false;
}

bool exegrep_find(const file_t& file, std::vector<BYTE>& data)
{
    DWORD dwFileShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE hFile = CreateFileW(file.c_str(), GENERIC_READ, dwFileShare, NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"exegrep: warning: Cannot open file: '%ls'\n", file.c_str());
        return RET_OK;
    }

    ULARGE_INTEGER FileSize;
    FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
    if (FileSize.LowPart == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    {
        fwprintf(stderr, L"exegrep: warning: Cannot read file: '%ls'\n", file.c_str());
        CloseHandle(hFile);
        return RET_OK;
    }

    data.resize((SIZE_T)FileSize.QuadPart);

    bool matched = false;
    DWORD cbRead;
    if (ReadFile(hFile, data.data(), (DWORD)data.size(), &cbRead, NULL) &&
        cbRead == FileSize.QuadPart)
    {
        matched = exegrep_match(data);
    }
    else
    {
        fwprintf(stderr, L"exegrep: warning: Cannot read file: '%ls'\n", file.c_str());
    }

    CloseHandle(hFile);
    return matched;
}

INT exegrep_search(const files_t& files)
{
    for (auto& file : files)
    {
        std::vector<BYTE> data;
        if (exegrep_find(file, data))
        {
            wprintf(L"%ls\n", file.c_str());
        }
    }
    return RET_OK;
}

INT exegrep(const files_t& items)
{
    files_t files;
    for (auto& item : items)
    {
        INT ret = exegrep_wildcard(files, item);
        if (ret != RET_OK)
            return ret;
    }

    exegrep_sort_unique(files);

    return exegrep_search(files);
}

INT wmain(INT argc, WCHAR **argv)
{
    setlocale(LC_CTYPE, "");
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);

    if (argc <= 1)
    {
        usage();
        return RET_OK;
    }

    g_pattern = NULL;
    g_recursive = false;

    files_t items;
    for (INT iarg = 1; iarg < argc; ++iarg)
    {
        auto arg = argv[iarg];
        if (_wcsicmp(arg, L"/?") == 0 || _wcsicmp(arg, L"-h") == 0 ||
            _wcsicmp(arg, L"--help") == 0 || _wcsicmp(arg, L"-help") == 0)
        {
            usage();
            return RET_OK;
        }
        if (_wcsicmp(arg, L"/V") == 0 || _wcsicmp(arg, L"-v") == 0 ||
            _wcsicmp(arg, L"--version") == 0 || _wcsicmp(arg, L"-version") == 0)
        {
            version();
            return RET_OK;
        }
        if (_wcsicmp(arg, L"/R") == 0 || _wcsicmp(arg, L"-r") == 0 ||
            _wcsicmp(arg, L"--recursive") == 0 || _wcsicmp(arg, L"-recursive") == 0)
        {
            g_recursive = true;
            continue;
        }
        if (!g_pattern)
        {
            g_pattern = arg;
            continue;
        }
        items.push_back(arg);
    }

    if (!g_pattern)
    {
        fwprintf(stderr, L"exegrep: error: No pattern specified\n");
        version();
        return RET_INVALID_ARG;
    }

    if (items.empty())
    {
        items.push_back(L"*");
    }

    return exegrep(items);
}

int main(void)
{
    INT argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    INT ret = wmain(argc, argv);
    LocalFree(argv);
    return ret;
}

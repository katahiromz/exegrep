// exegrep
// License: MIT
#include <windows.h>
#include <shlwapi.h>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <string>
#include <vector>
#include <set>
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
        L"ExeGrep:\n"
        L"  -r          Recursive mode.\n"
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
    if (attrs == (DWORD)-1)
    {
        fwprintf(stderr, L"exegrep: error: File not found: '%ls'\n", item.c_str());
        return RET_FILE_NOT_FOUND;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        files.emplace(item);
        return RET_OK;
    }

    if (m_recursive)
        return dir(files, item);

    return RET_OK;
}

RET ExeGrep::wildcard(files_t& files, const file_t& item)
{
    if (item.find(L'*') == item.npos && item.find(L'?') == item.npos)
        return do_item(files, item);

    WIN32_FIND_DATAW find;
    HANDLE hFind = FindFirstFileW(item.c_str(), &find);
    if (hFind == INVALID_HANDLE_VALUE)
        return RET_OK;

    WCHAR szDir[MAX_PATH];
    lstrcpynW(szDir, item.c_str(), _countof(szDir));
    PathRemoveFileSpecW(szDir);

    WCHAR szFile[MAX_PATH];
    RET ret = RET_OK;
    do
    {
        if (wcscmp(find.cFileName, L".") == 0 || wcscmp(find.cFileName, L"..") == 0)
            continue;

        lstrcpynW(szFile, szDir, _countof(szFile));
        PathAppendW(szFile, find.cFileName);

        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (m_recursive)
            {
                ret = dir(files, szFile);
                if (ret == RET_FILE_NOT_FOUND)
                    break;
            }
        }
        else
        {
            files.emplace(szFile);
            ret = RET_OK;
        }
    } while (FindNextFileW(hFind, &find));
    FindClose(hFind);

    return ret;
}

bool ExeGrep::match(const std::vector<BYTE>& data)
{
    std::string patA;
    bool is_ascii = true;
    for (auto wch : m_pattern)
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

    size_t patlenW = m_pattern.size();
    size_t datalenW = data.size() / sizeof(WCHAR);
    if (patlenW > datalenW)
        return false;

    LPCWSTR pchW = (LPCWSTR)data.data();
    size_t cchEndW = datalenW - patlenW;
    for (size_t ich = 0; ich < cchEndW; ++ich)
    {
        if (_wcsnicmp(&pchW[ich], m_pattern.c_str(), m_pattern.size()) == 0)
            return true;
    }

    return false;
}

bool ExeGrep::find(const file_t& file, std::vector<BYTE>& data)
{
    DWORD dwFileShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE hFile = CreateFileW(file.c_str(), GENERIC_READ, dwFileShare, NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"exegrep: warning: Cannot open file: '%ls'\n", file.c_str());
        return false;
    }

    ULARGE_INTEGER FileSize;
    FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
    if (FileSize.LowPart == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    {
        fwprintf(stderr, L"exegrep: warning: Cannot read file: '%ls'\n", file.c_str());
        CloseHandle(hFile);
        return false;
    }
    if (FileSize.QuadPart >= MAXLONG)
    {
        fwprintf(stderr, L"exegrep: warning: Too large file: '%ls'\n", file.c_str());
        CloseHandle(hFile);
        return false;
    }

    data.resize((SIZE_T)FileSize.QuadPart);

    bool matched = false;
    DWORD cbRead;
    if (ReadFile(hFile, data.data(), (DWORD)data.size(), &cbRead, NULL) &&
        cbRead == FileSize.QuadPart)
    {
        matched = match(data);
    }
    else
    {
        fwprintf(stderr, L"exegrep: warning: Cannot read file: '%ls'\n", file.c_str());
    }

    CloseHandle(hFile);
    return matched;
}

RET ExeGrep::search(const files_t& files)
{
    for (auto& file : files)
    {
        std::vector<BYTE> data;
        if (find(file, data))
        {
            wprintf(L"%ls\n", file.c_str());
        }
    }
    return RET_OK;
}

RET ExeGrep::execute()
{
    if (m_help)
    {
        usage();
        return RET_OK;
    }

    if (m_version)
    {
        version();
        return RET_OK;
    }

    files_t files;
    for (auto& item : m_items)
    {
        RET ret = wildcard(files, item);
        if (ret != RET_OK)
            return ret;
    }

    return search(files);
}

RET ExeGrep::parse(INT argc, WCHAR **argv)
{
    if (argc <= 1)
    {
        m_help = true;
        return RET_OK;
    }

    bool has_pattern = false;

    for (INT iarg = 1; iarg < argc; ++iarg)
    {
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
        if (!has_pattern)
        {
            m_pattern = arg;
            has_pattern = true;
            continue;
        }
        m_items.emplace(arg);
    }

    if (!has_pattern)
    {
        fwprintf(stderr, L"exegrep: error: No pattern specified\n");
        return RET_INVALID_ARG;
    }

    if (m_items.empty())
    {
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

// Marz.exe (built by setup.sh): starts marz\bin\ps2EntryRunner.exe on the Japanese game folder, or with --en on the
// English one, with the usual display settings (each kept when already set in the environment; the options below
// override both). Log: marz\logs\run_play.log.
#include <windows.h>

#include <fstream>
#include <string>

static void setDefault(const wchar_t *name, const std::wstring &value)
{
    if (GetEnvironmentVariableW(name, nullptr, 0) == 0)
        SetEnvironmentVariableW(name, value.c_str());
}

static int fail(const std::wstring &text)
{
    MessageBoxW(nullptr, text.c_str(), L"Marz", MB_ICONERROR);
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring top(self);
    top.resize(top.find_last_of(L'\\'));
    const std::wstring marz = top + L"\\marz";
    const std::wstring bin = marz + L"\\bin";
    // --en, --scale=N (internal resolution, PS2X_GS_SCALE), --widescreen=0|1 (16:9, PS2X_MARZ_WIDESCREEN),
    // --ot-handling=0|1 (Oratorio Tangram 5.66 handling, PS2X_MARZ_QUICK_HANDLING; off by default).
    bool english = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg(argv[i]);
        if (arg == L"--en")
            english = true;
        else if (arg.rfind(L"--scale=", 0) == 0)
            SetEnvironmentVariableW(L"PS2X_GS_SCALE", arg.substr(8).c_str());
        else if (arg.rfind(L"--widescreen=", 0) == 0)
            SetEnvironmentVariableW(L"PS2X_MARZ_WIDESCREEN", arg.substr(13).c_str());
        else if (arg.rfind(L"--ot-handling=", 0) == 0)
            SetEnvironmentVariableW(L"PS2X_MARZ_QUICK_HANDLING", arg.substr(14).c_str());
        else
            return fail(L"Unknown option " + arg + L"\nOptions: --en, --scale=N, --widescreen=0|1, --ot-handling=0|1");
    }
    const std::wstring elf = marz + (english ? L"\\iso_en" : L"\\iso_jp") + L"\\SLPM_653.03";
    if (GetFileAttributesW(elf.c_str()) == INVALID_FILE_ATTRIBUTES)
        return fail(L"Missing " + elf + L"\nRun setup.bat first.");

    std::string iso;
    std::ifstream isoFile((bin + L"\\iso_path.txt").c_str());
    std::getline(isoFile, iso);
    while (!iso.empty() && (iso.back() == '\r' || iso.back() == '\n'))
        iso.pop_back();
    std::wstring isoW(MultiByteToWideChar(CP_UTF8, 0, iso.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, iso.c_str(), -1, isoW.data(), static_cast<int>(isoW.size()));
    isoW.resize(isoW.size() - 1);

    setDefault(L"PS2X_GS_SCALE", L"3");
    setDefault(L"PS2X_MARZ_WIDESCREEN", L"1");
    setDefault(L"PS2X_GS_FLICKER_FILTER", L"0");
    setDefault(L"PS2X_GS_DITHER", L"0");
    setDefault(L"PS2X_CD_IMAGE", isoW);

    CreateDirectoryW((marz + L"\\logs").c_str(), nullptr);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW((marz + L"\\logs\\run_play.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    const std::wstring runner = bin + L"\\ps2EntryRunner.exe";
    std::wstring cmd = L"\"" + runner + L"\" \"" + elf + L"\"";
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(runner.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, bin.c_str(), &si, &pi))
        return fail(L"Could not start " + runner);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(log);
    return 0;
}

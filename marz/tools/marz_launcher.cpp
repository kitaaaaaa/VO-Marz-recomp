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
    // --ot-handling=0|1 (Oratorio Tangram 5.66 handling, PS2X_MARZ_QUICK_HANDLING; off by default),
    // --voot-buttons=0|1 (VOOT button layout, PS2X_MARZ_VOOT_BUTTONS; off by default), --flicker-filter=0|1
    // (PS2X_GS_FLICKER_FILTER), --dither=0|1 (PS2X_GS_DITHER), --sofdec=0|1 (the disc's movie player instead of the
    // mp4 files, PS2X_MARZ_SOFDEC), --skip-intro (passed to the runner: no opening movie).
    static const struct { const wchar_t *option, *variable; } switches[] = {
        {L"--scale=", L"PS2X_GS_SCALE"},
        {L"--widescreen=", L"PS2X_MARZ_WIDESCREEN"},
        {L"--ot-handling=", L"PS2X_MARZ_QUICK_HANDLING"},
        {L"--voot-buttons=", L"PS2X_MARZ_VOOT_BUTTONS"},
        {L"--flicker-filter=", L"PS2X_GS_FLICKER_FILTER"},
        {L"--dither=", L"PS2X_GS_DITHER"},
        {L"--sofdec=", L"PS2X_MARZ_SOFDEC"},
    };
    bool english = false;
    std::wstring runnerArgs;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg(argv[i]);
        bool known = false;
        for (const auto &s : switches)
        {
            const std::wstring option(s.option);
            if (arg.rfind(option, 0) == 0)
            {
                SetEnvironmentVariableW(s.variable, arg.substr(option.size()).c_str());
                known = true;
            }
        }
        if (known)
            continue;
        if (arg == L"--en")
            english = true;
        else if (arg == L"--skip-intro")
            runnerArgs += L" " + arg;
        else
            return fail(L"Unknown option " + arg +
                        L"\nOptions: --en, --scale=N, --widescreen=0|1, --ot-handling=0|1, --voot-buttons=0|1,"
                        L" --flicker-filter=0|1, --dither=0|1, --sofdec=0|1, --skip-intro");
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
    // A path without a drive or a \\ share is relative to the folder Marz.exe is in (an .iso kept next to it).
    if (!isoW.empty() && !(isoW.size() > 1 && isoW[1] == L':') && isoW.rfind(L"\\\\", 0) != 0)
        isoW = top + L"\\" + isoW;

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
    std::wstring cmd = L"\"" + runner + L"\" \"" + elf + L"\"" + runnerArgs;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(runner.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, bin.c_str(), &si, &pi))
        return fail(L"Could not start " + runner);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(log);
    return 0;
}

// "PDFable Creator.exe": starts PDFable.exe from the same folder with --creator, so the
// portable Windows folder has a clickable entry for the creator (shortcuts cannot be relative).
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR args, int)
{
    std::wstring self(MAX_PATH, L'\0');
    DWORD n = 0;
    while ((n = GetModuleFileNameW(nullptr, &self[0], DWORD(self.size()))) == self.size()) self.resize(self.size() * 2);
    self.resize(n);
    std::wstring exe = self.substr(0, self.find_last_of(L"\\/") + 1) + L"PDFable.exe";
    std::wstring cmd = L"\"" + exe + L"\" --creator";
    if (args && *args) cmd += std::wstring(L" ") + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        MessageBoxW(nullptr, (L"Cannot start " + exe).c_str(), L"PDFable Creator", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

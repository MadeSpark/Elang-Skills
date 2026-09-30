// Stupid-simple 32-bit DLL: when ANY 32-bit process loads this DLL, DllMain will
// enumerate ALL possible USER handle values 0x00000004..0x00FFFFFF step 4, and blindly
// call UnhookWindowsHookEx() for each candidate.
// For non-hook handles the call simply returns FALSE (ERROR_INVALID_HANDLE) - no side effects.
// For valid hook handles OWNED BY THIS PROCESS the call succeeds, REMOVING the hook from the
// global chain PERMANENTLY.
// This is injected into e.exe via CreateRemoteThread(LoadLibraryA(path)) just BEFORE we call TerminateProcess(e.exe).
// Result: 0 stale hooks remain after TerminateProcess => 0 * 300ms = NO MOUSE/KEYBOARD FREEZE.
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)hModule;
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        unsigned int hh;
        /* Scan 0x4..0x01000000 (16M iterations) step 4.
           Runtime inside e.exe process on modern CPU: ~150-300ms.
           Covers EVERY possible USER HANDLE value in a Windows session. */
        for (hh = 0x4u; hh < 0x01000000u; hh += 4u) {
            UnhookWindowsHookEx((HHOOK)(UINT_PTR)hh);
        }
    }
    return TRUE;
}
/* No exports needed. Loader calls DllMain which does everything. */

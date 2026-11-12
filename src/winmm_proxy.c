/* winmm_proxy.c - Lightweight WinMM Proxy Loader for Village SaveJP-to-EN Plugin
 * Forwards all winmm.dll calls to C:\Windows\System32\winmm.dll
 * Automatically loads save_jp_to_en.dll from Mods\SaveJPtoEN\, Mods\, or game root.
 */

#include <windows.h>
#include <stdio.h>

static void log_proxy(const wchar_t* game_dir, const char* msg)
{
    wchar_t log_dir[MAX_PATH];
    wchar_t log_path[MAX_PATH];
    swprintf(log_dir, MAX_PATH, L"%ls\\Mods\\SaveJPtoEN", game_dir);
    if (GetFileAttributesW(log_dir) != INVALID_FILE_ATTRIBUTES) {
        swprintf(log_path, MAX_PATH, L"%ls\\winmm_proxy.log", log_dir);
    } else {
        swprintf(log_path, MAX_PATH, L"%ls\\save_jp_to_en_proxy.log", game_dir);
    }

    FILE* f = _wfopen(log_path, L"a+");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] [winmm_proxy] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fclose(f);
    }
}

static DWORD WINAPI loader_thread(LPVOID param)
{
    (void)param;
    wchar_t exe_dir[MAX_PATH];
    GetModuleFileNameW(NULL, exe_dir, MAX_PATH);
    wchar_t* last_slash = wcsrchr(exe_dir, L'\\');
    if (last_slash) *last_slash = L'\0';

    const wchar_t* rel_candidates[] = {
        L"Mods\\SaveJPtoEN\\save_jp_to_en.dll",
        L"Mods\\save_jp_to_en.dll",
        L"save_jp_to_en.dll"
    };

    wchar_t target_dll[MAX_PATH] = { 0 };
    BOOL found = FALSE;
    for (size_t i = 0; i < sizeof(rel_candidates) / sizeof(rel_candidates[0]); i++) {
        swprintf(target_dll, MAX_PATH, L"%ls\\%ls", exe_dir, rel_candidates[i]);
        if (GetFileAttributesW(target_dll) != INVALID_FILE_ATTRIBUTES) {
            found = TRUE;
            break;
        }
    }

    if (!found) {
        log_proxy(exe_dir, "save_jp_to_en.dll not found in Mods\\SaveJPtoEN, Mods, or game root.");
        return 0;
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "Loading plugin DLL: %ls", target_dll);
    log_proxy(exe_dir, msg);

    HMODULE hMod = LoadLibraryW(target_dll);
    if (hMod) {
        snprintf(msg, sizeof(msg), "Loaded save_jp_to_en.dll successfully (Handle=%p)", (void*)hMod);
        log_proxy(exe_dir, msg);

        typedef void (*pfn_plugin_init)(void);
        pfn_plugin_init init_fn = (pfn_plugin_init)GetProcAddress(hMod, "plugin_init");
        if (init_fn) {
            init_fn();
        }
    } else {
        DWORD err = GetLastError();
        snprintf(msg, sizeof(msg), "Failed to load save_jp_to_en.dll (Error=%lu)", err);
        log_proxy(exe_dir, msg);
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        HANDLE hThread = CreateThread(NULL, 0, loader_thread, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}

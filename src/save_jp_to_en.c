/* save_jp_to_en.c - Force English Mode when Loading JP Saves (AoB Auto-Scanner)
 * Target: Village in the Shade (64-bit)
 * Independent standalone plugin: scans .text with wildcard signatures so normal game updates keep working.
 */

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int enabled;
    int lock_option_menu;
    int logging;
} PluginSettings;

static PluginSettings g_settings = { 1, 1, 1 };
static wchar_t g_dll_dir[MAX_PATH] = { 0 };
static LONG g_applied = 0;

static void init_dll_dir(HMODULE hmod)
{
    GetModuleFileNameW(hmod, g_dll_dir, MAX_PATH);
    wchar_t* slash = wcsrchr(g_dll_dir, L'\\');
    if (slash) *slash = L'\0';
}

static void log_msg(const char* fmt, ...)
{
    if (!g_settings.logging || g_dll_dir[0] == L'\0') return;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\save_jp_to_en.log", g_dll_dir);
    FILE* f = _wfopen(path, L"a");
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

static void load_settings(void)
{
    if (g_dll_dir[0] == L'\0') return;
    wchar_t ini_path[MAX_PATH];
    swprintf(ini_path, MAX_PATH, L"%ls\\save_jp_to_en.ini", g_dll_dir);

    g_settings.enabled = GetPrivateProfileIntW(L"Settings", L"enabled", 1, ini_path);
    g_settings.lock_option_menu = GetPrivateProfileIntW(L"Settings", L"lock_option_menu", 1, ini_path);
    g_settings.logging = GetPrivateProfileIntW(L"Settings", L"logging", 1, ini_path);
}

/* Obtain .text section bounds from main executable module in memory */
static BOOL get_text_section(uintptr_t base, uint8_t** out_start, size_t* out_size)
{
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;

    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(base + (uint32_t)dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            *out_start = (uint8_t*)(base + sec[i].VirtualAddress);
            *out_size = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            return TRUE;
        }
    }
    return FALSE;
}

/* Scan range [start, start + size) for hex pattern like "48 85 ?? 74 0B" */
static size_t find_aob_matches(
    const uint8_t* start, size_t size, const char* pat_str,
    uint8_t** out_hits, size_t max_hits)
{
    uint8_t bytes[128];
    uint8_t mask[128];
    size_t len = 0;

    const char* p = pat_str;
    while (*p && len < sizeof(bytes)) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') {
            bytes[len] = 0;
            mask[len] = 0;
            p += 2;
        } else {
            char buf[3] = { p[0], p[1], '\0' };
            bytes[len] = (uint8_t)strtoul(buf, NULL, 16);
            mask[len] = 1;
            p += (p[1] ? 2 : 1);
        }
        len++;
    }

    if (len == 0 || size < len) return 0;

    size_t count = 0;
    size_t limit = size - len;
    for (size_t i = 0; i <= limit; i++) {
        BOOL ok = TRUE;
        for (size_t j = 0; j < len; j++) {
            if (mask[j] && start[i + j] != bytes[j]) {
                ok = FALSE;
                break;
            }
        }
        if (ok) {
            if (count < max_hits) {
                out_hits[count] = (uint8_t*)(start + i);
            }
            count++;
        }
    }
    return count;
}

static BOOL write_patch(uintptr_t base, uint8_t* addr, const uint8_t* data, size_t len, const char* label)
{
    DWORD old_prot = 0;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old_prot)) {
        log_msg("[ERROR] VirtualProtect failed for %s at RVA 0x%llX (err=%lu)",
                label, (unsigned long long)((uintptr_t)addr - base), GetLastError());
        return FALSE;
    }
    memcpy(addr, data, len);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    VirtualProtect(addr, len, old_prot, &old_prot);
    log_msg("[OK] Patched %-28s (RVA 0x%06llX, %zu bytes)",
            label, (unsigned long long)((uintptr_t)addr - base), len);
    return TRUE;
}

static uintptr_t resolve_rip_disp32(const uint8_t* insn_end, const uint8_t* disp_ptr)
{
    int32_t disp = *(const int32_t*)disp_ptr;
    return (uintptr_t)(insn_end + disp);
}

static void apply_force_en_patches(void)
{
    if (InterlockedCompareExchange(&g_applied, 1, 0) != 0) {
        return;
    }

    load_settings();
    if (!g_settings.enabled) {
        log_msg("[INFO] Disabled via save_jp_to_en.ini (enabled=0). No patches applied.");
        return;
    }

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uint8_t* text_start = NULL;
    size_t text_size = 0;
    if (!get_text_section(base, &text_start, &text_size)) {
        log_msg("[ERROR] Failed to locate .text section in main executable.");
        return;
    }

    log_msg("[INIT] Main base=0x%llX, .text start=0x%p, size=0x%zX",
            (unsigned long long)base, (void*)text_start, text_size);

    uintptr_t global_lang_addr = 0;
    int total_ok = 0;
    uint8_t* hits[16];

    /* 1. Save Load Lang Switch + Option Lang Switch 2, 3, 5 (and auto-resolve GlobalLang) */
    size_t n_save = find_aob_matches(
        text_start, text_size,
        "48 85 C9 7E 13 3B 05 ?? ?? ?? ?? 74 0B 89 05 ?? ?? ?? ?? E8",
        hits, 16);
    if (n_save > 0 && n_save <= 8) {
        for (size_t i = 0; i < n_save; i++) {
            uint8_t* m = hits[i];
            uintptr_t t1 = resolve_rip_disp32(m + 11, m + 7);
            uintptr_t t2 = resolve_rip_disp32(m + 19, m + 15);
            if (t1 != t2) {
                log_msg("[WARN] Save/Opt site %zu RIP target mismatch (0x%llX vs 0x%llX), skipping",
                        i, (unsigned long long)t1, (unsigned long long)t2);
                continue;
            }
            if (global_lang_addr == 0) {
                global_lang_addr = t1;
                log_msg("[SCAN] Resolved GlobalLang RVA = 0x%06llX",
                        (unsigned long long)(global_lang_addr - base));
            } else if (global_lang_addr != t1) {
                continue;
            }
            /* Index 0 in .text address order is SaveLoadSkip (0x14A772); indices 1..3 are Option skips */
            if (i == 0 || g_settings.lock_option_menu) {
                const uint8_t jmp_op[2] = { 0xEB, 0x0B };
                if (write_patch(base, m + 11, jmp_op, 2, (i == 0) ? "SaveLoadLangSkip" : "OptionLangSkip_0B")) {
                    total_ok++;
                }
            }
        }
    } else {
        log_msg("[WARN] SaveAndOptLangSkip signature unexpected hit count: %zu", n_save);
    }

    /* 2. Steam GetGameLanguage -> return 1 (EN) */
    size_t n_steam = find_aob_matches(
        text_start, text_size,
        "48 89 5C 24 18 56 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 50 48 8D 0D ?? ?? ?? ?? 48 89 6C 24 70 FF 15",
        hits, 4);
    if (n_steam == 1) {
        /* push 1; pop rax; ret; nop */
        const uint8_t ret_en[5] = { 0x6A, 0x01, 0x58, 0xC3, 0x90 };
        if (write_patch(base, hits[0], ret_en, sizeof(ret_en), "SteamBootLang->EN")) total_ok++;
    } else {
        log_msg("[WARN] SteamBootLang signature hit count=%zu (expected 1)", n_steam);
    }

    /* 3. Engine Init Boot Lang -> mov r9d, ebx; nop (ebx=1 = EN right above) */
    size_t n_init = find_aob_matches(
        text_start, text_size,
        "49 3B D2 75 EA 48 85 C0 44 0F 4F CB 44 89 0D ?? ?? ?? ?? 41 B8 00 00 40 01",
        hits, 4);
    if (n_init == 1) {
        uintptr_t t = resolve_rip_disp32(hits[0] + 19, hits[0] + 15);
        if (global_lang_addr == 0) global_lang_addr = t;
        if (t == global_lang_addr) {
            const uint8_t mov_r9d_ebx[4] = { 0x44, 0x89, 0xD9, 0x90 };
            if (write_patch(base, hits[0] + 8, mov_r9d_ebx, 4, "EngineInitLang->EN")) total_ok++;
        } else {
            log_msg("[WARN] EngineInitLang GlobalLang mismatch");
        }
    } else {
        log_msg("[WARN] EngineInitLang signature hit count=%zu (expected 1)", n_init);
    }

    /* 4. Option Menu Lang Switch 1 & 4 (74 22 / 74 26 right before 44 89 05 [GlobalLang]) */
    if (g_settings.lock_option_menu) {
        size_t n_opt14 = find_aob_matches(
            text_start, text_size,
            "44 89 05 ?? ?? ?? ?? BA 07 00 00 00 49 8B",
            hits, 8);
        for (size_t i = 0; i < n_opt14; i++) {
            uint8_t* m = hits[i];
            uintptr_t t = resolve_rip_disp32(m + 7, m + 3);
            if (global_lang_addr != 0 && t != global_lang_addr) continue;
            if (m - text_start >= 2 && m[-2] == 0x74) {
                const uint8_t jmp_byte = 0xEB;
                if (write_patch(base, m - 2, &jmp_byte, 1, "OptionLangSkip_je")) total_ok++;
            }
        }
    }

    /* 5. Runtime Dialogue Lang Guard: mov eax, [GlobalLang] -> mov eax, 1; nop */
    size_t n_dlg = find_aob_matches(
        text_start, text_size,
        "89 9F 78 04 00 00 8B 05 ?? ?? ?? ?? 89 87 7C 04 00 00 C6 87 80 04 00 00",
        hits, 4);
    if (n_dlg == 1) {
        uintptr_t t = resolve_rip_disp32(hits[0] + 12, hits[0] + 8);
        if (global_lang_addr == 0 || t == global_lang_addr) {
            const uint8_t mov_eax_1[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x90 };
            if (write_patch(base, hits[0] + 6, mov_eax_1, 6, "DialogueLang->EN")) total_ok++;
        }
    } else {
        log_msg("[WARN] DialogueLang signature hit count=%zu (expected 1)", n_dlg);
    }

    /* 6. DB String Index Guard: movsxd rdx, [GlobalLang] -> mov edx, 1; nop; nop */
    size_t n_db = find_aob_matches(
        text_start, text_size,
        "EB 04 48 83 C0 18 48 63 15 ?? ?? ?? ?? 48 8B 08 48 8B 44 D1 58 44 38 20",
        hits, 4);
    if (n_db == 1) {
        uintptr_t t = resolve_rip_disp32(hits[0] + 13, hits[0] + 9);
        if (global_lang_addr == 0 || t == global_lang_addr) {
            const uint8_t mov_edx_1[7] = { 0xBA, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90 };
            if (write_patch(base, hits[0] + 6, mov_edx_1, 7, "DBStringIndex->EN")) total_ok++;
        }
    } else {
        log_msg("[WARN] DBStringIndex signature hit count=%zu (expected 1)", n_db);
    }

    /* 7. Dialogue Flow Guard: movsxd rax, [GlobalLang] (7 bytes!) -> mov eax, 1; nop; nop */
    size_t n_flow = find_aob_matches(
        text_start, text_size,
        "48 63 05 ?? ?? ?? ?? 83 F8 06 0F 87 ?? ?? ?? ?? 8B 8C 87",
        hits, 4);
    if (n_flow == 1) {
        uintptr_t t = resolve_rip_disp32(hits[0] + 7, hits[0] + 3);
        if (global_lang_addr == 0 || t == global_lang_addr) {
            const uint8_t mov_eax_1_7b[7] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90 };
            if (write_patch(base, hits[0], mov_eax_1_7b, 7, "DialogueFlow->EN")) total_ok++;
        }
    } else {
        log_msg("[WARN] DialogueFlow signature hit count=%zu (expected 1)", n_flow);
    }

    /* 8. Set GlobalLanguage in RAM to 1 (English) if resolved and SaveLoadSkip was patched */
    if (global_lang_addr != 0 && total_ok > 0) {
        uint32_t* p_lang = (uint32_t*)global_lang_addr;
        DWORD old_prot = 0;
        if (VirtualProtect(p_lang, sizeof(uint32_t), PAGE_READWRITE, &old_prot)) {
            uint32_t prev = *p_lang;
            *p_lang = 1;
            VirtualProtect(p_lang, sizeof(uint32_t), old_prot, &old_prot);
            log_msg("[OK] RAM GlobalLang [0x%p] set: %u -> 1 (EN). Total patches=%d",
                    (void*)p_lang, prev, total_ok);
        }
    } else {
        log_msg("[WARN] Skipped RAM GlobalLang write (addr=0x%llX, total_ok=%d)",
                (unsigned long long)global_lang_addr, total_ok);
    }
}

__declspec(dllexport) void plugin_init(void)
{
    apply_force_en_patches();
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        init_dll_dir(hinst);
        apply_force_en_patches();
    }
    return TRUE;
}

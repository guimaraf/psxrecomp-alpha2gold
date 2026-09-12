// game_core.c ? see game_core.h.
#include "game_core.h"

#include <stdio.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

psx_dispatch_game_compiled_fn g_psx_dispatch_game_compiled = NULL;
psx_game_text_native_ok_fn g_psx_game_text_native_ok = NULL;
psx_game_address_in_text_fn g_psx_game_address_in_text = NULL;
psx_game_is_function_entry_fn g_psx_game_is_function_entry = NULL;

#if defined(_WIN32)
static HMODULE s_core_handle = NULL;
#else
static void* s_core_handle = NULL;
#endif

int game_core_is_loaded(void) {
    return (s_core_handle != NULL && g_psx_dispatch_game_compiled != NULL) ? 1 : 0;
}

int game_core_load(const char* dll_path) {
    if (s_core_handle && g_psx_dispatch_game_compiled) {
        return 1;
    }
    const char* path = (dll_path && dll_path[0]) ? dll_path : "game_core.dll";

#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        DWORD err = GetLastError();
        fprintf(stderr, "psxrecomp: LoadLibraryA('%s') failed with error %lu\n", path, (unsigned long)err);
        return 0;
    }
    psx_dispatch_game_compiled_fn fn_dispatch =
        (psx_dispatch_game_compiled_fn)GetProcAddress(h, "psx_dispatch_game_compiled");
    psx_game_text_native_ok_fn fn_text_ok =
        (psx_game_text_native_ok_fn)GetProcAddress(h, "psx_game_text_native_ok");
    psx_game_address_in_text_fn fn_in_text =
        (psx_game_address_in_text_fn)GetProcAddress(h, "psx_game_address_in_text");
    psx_game_is_function_entry_fn fn_is_entry =
        (psx_game_is_function_entry_fn)GetProcAddress(h, "psx_game_is_function_entry");

    if (!fn_dispatch) {
        fprintf(stderr, "psxrecomp: %s found but missing 'psx_dispatch_game_compiled' export\n", path);
        FreeLibrary(h);
        return 0;
    }
    s_core_handle = h;
    g_psx_dispatch_game_compiled = fn_dispatch;
    g_psx_game_text_native_ok = fn_text_ok;
    g_psx_game_address_in_text = fn_in_text;
    g_psx_game_is_function_entry = fn_is_entry;
    fprintf(stdout, "psxrecomp: dynamic game core loaded successfully from %s\n", path);
    return 1;
#else
    void* h = dlopen(path, RTLD_NOW);
    if (!h) return 0;
    psx_dispatch_game_compiled_fn fn_dispatch =
        (psx_dispatch_game_compiled_fn)dlsym(h, "psx_dispatch_game_compiled");
    if (!fn_dispatch) {
        dlclose(h);
        return 0;
    }
    s_core_handle = h;
    g_psx_dispatch_game_compiled = fn_dispatch;
    g_psx_game_text_native_ok = (psx_game_text_native_ok_fn)dlsym(h, "psx_game_text_native_ok");
    g_psx_game_address_in_text = (psx_game_address_in_text_fn)dlsym(h, "psx_game_address_in_text");
    g_psx_game_is_function_entry = (psx_game_is_function_entry_fn)dlsym(h, "psx_game_is_function_entry");
    return 1;
#endif
}

void game_core_unload(void) {
    if (s_core_handle) {
#if defined(_WIN32)
        FreeLibrary(s_core_handle);
#else
        dlclose(s_core_handle);
#endif
        s_core_handle = NULL;
        g_psx_dispatch_game_compiled = NULL;
        g_psx_game_text_native_ok = NULL;
        g_psx_game_address_in_text = NULL;
        g_psx_game_is_function_entry = NULL;
    }
}

#ifndef PSX_HAS_STATIC_DISPATCH
int psx_dispatch_game_compiled(CPUState* cpu, uint32_t addr) {
    if (g_psx_dispatch_game_compiled) {
        return g_psx_dispatch_game_compiled(cpu, addr);
    }
    return 0;
}

int psx_game_address_in_text(uint32_t addr) {
    if (g_psx_game_address_in_text) {
        return g_psx_game_address_in_text(addr);
    }
    return 0;
}

int psx_game_is_function_entry(uint32_t addr) {
    if (g_psx_game_is_function_entry) {
        return g_psx_game_is_function_entry(addr);
    }
    return 0;
}

int psx_game_text_native_ok(uint32_t addr) {
    if (g_psx_game_text_native_ok) {
        return g_psx_game_text_native_ok(addr);
    }
    return 0;
}
#endif

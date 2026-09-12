// game_core.h ? Dynamic game core loader interface.
//
// Enables a clean decoupled launcher/runtime without embedded proprietary code.
// The game executable recompiled to C (SLUS_005.84_full.c + dispatch.c) is compiled
// locally by TinyCC into game_core.dll and loaded in-process at runtime.

#pragma once

#include "cpu_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*psx_dispatch_game_compiled_fn)(CPUState* cpu, uint32_t target);
typedef int (*psx_game_text_native_ok_fn)(uint32_t addr);
typedef int (*psx_game_address_in_text_fn)(uint32_t addr);
typedef int (*psx_game_is_function_entry_fn)(uint32_t addr);

extern psx_dispatch_game_compiled_fn g_psx_dispatch_game_compiled;
extern psx_game_text_native_ok_fn g_psx_game_text_native_ok;
extern psx_game_address_in_text_fn g_psx_game_address_in_text;
extern psx_game_is_function_entry_fn g_psx_game_is_function_entry;

// Check if game_core.dll is loaded and valid
int game_core_is_loaded(void);

// Dynamically load game_core.dll and bind function pointers
int game_core_load(const char* dll_path);

// Unload game_core.dll and clear function pointers
void game_core_unload(void);

#ifdef __cplusplus
}
#endif

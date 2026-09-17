#pragma once
#include <stdint.h>
#include <stddef.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * psx_gamepad_binds — configurable GAMEPAD -> PlayStation DualShock mappings.
 *
 * INI-driven, stored in `input.ini` next to the exe.
 * Supports Player 1 and Player 2 mappings independently.
 * Legacy `[mapping]` sections in input.ini fall back to Player 1 for full
 * backward compatibility.
 */

enum PsxGamepadButton {
    PSX_GP_UP = 0, PSX_GP_DOWN, PSX_GP_LEFT, PSX_GP_RIGHT,
    PSX_GP_CROSS, PSX_GP_CIRCLE, PSX_GP_SQUARE, PSX_GP_TRIANGLE,
    PSX_GP_L1, PSX_GP_R1, PSX_GP_L2, PSX_GP_R2,
    PSX_GP_L3, PSX_GP_R3,
    PSX_GP_START, PSX_GP_SELECT,
    PSX_GP_COUNT
};

typedef enum {
    PSX_GP_SRC_NONE = 0,
    PSX_GP_SRC_BUTTON,
    PSX_GP_SRC_AXIS_POS,
    PSX_GP_SRC_AXIS_NEG
} PsxGamepadSourceKind;

typedef struct {
    PsxGamepadSourceKind kind;
    int id; /* SDL_GameControllerButton or SDL_GameControllerAxis */
} PsxGamepadSource;

#define PSX_GP_MAX_SOURCES 2

typedef struct {
    PsxGamepadSource sources[PSX_GP_MAX_SOURCES];
} PsxGamepadBtnBind;

typedef struct {
    PsxGamepadBtnBind buttons[PSX_GP_COUNT];
} PsxPlayerGamepadBinds;

typedef struct {
    PsxPlayerGamepadBinds p1;
    PsxPlayerGamepadBinds p2;
} PsxGamepadBinds;

/* Initialize from <exe_dir>/input.ini or config directory. */
void psx_gamepad_binds_init(const char *exe_path);

/* Read-only view of the current bindings. */
const PsxGamepadBinds *psx_gamepad_binds_get(void);

/* Build the 16-bit ACTIVE-LOW PSX button word for `player` (1 or 2) from an open SDL_GameController. */
uint16_t psx_gamepad_binds_pad_word(SDL_GameController *pad, int player, int suppress_stick_axes);

/* Check if any D-pad direction is active for `player` (1 or 2). Used by hybrid pad mode. */
int psx_gamepad_binds_dpad_active(SDL_GameController *pad, int player);

/* ── Rebind API (Launcher Gamepad page) ──────────────────────────────────── */

int         psx_gamepad_binds_button_count(void);
const char *psx_gamepad_binds_button_name(int button);
const char *psx_gamepad_binds_button_label(int button);

/* Returns a human-friendly label for the button's current mapping (e.g. "A", "LT / L2"). */
void psx_gamepad_binds_get_label(int player, int button, char *buf, size_t buf_size);

/* Set the primary binding for a button (player is 1 or 2). */
void psx_gamepad_binds_set_source(int player, int button, PsxGamepadSource src);

/* Reset player bindings to default (player is 1 or 2). */
void psx_gamepad_binds_reset_player(int player);

/* Persist the current bindings to input.ini. */
void psx_gamepad_binds_save(void);

/* Helpers to convert SDL events to PsxGamepadSource */
PsxGamepadSource psx_gamepad_source_from_button(SDL_GameControllerButton btn);
PsxGamepadSource psx_gamepad_source_from_axis(SDL_GameControllerAxis axis, int sign);

#ifdef __cplusplus
}
#endif

/*
 * Validate PS1 CD-ROM ReadN/ReadS positioning around SetLoc and Pause.
 *
 * Build:
 *   cc -std=c99 -Wall -Wextra -Werror -I../src \
 *      -o test_cdrom_read_position test_cdrom_read_position.c
 */
#include "cdrom_read_position.h"

#include <stdio.h>

static int failures;

#define CHECK(expected_action, expected_lba, actual, label) do {              \
    if ((actual).action != (expected_action) ||                              \
        (actual).lba != (expected_lba)) {                                    \
        fprintf(stderr,                                                       \
                "FAIL: %s (expected action=%d lba=%d, got action=%d lba=%d)\n",\
                (label), (int)(expected_action), (expected_lba),             \
                (int)(actual).action, (actual).lba);                          \
        failures++;                                                          \
    }                                                                        \
} while (0)

int main(void) {
    CDROMReadPosition position;

    position = cdrom_select_read_position(1, 0, 0, 100, 210, 211);
    CHECK(CDROM_READ_POSITION_CONTINUE, 211, position,
          "active Read without SetLoc keeps the live cursor");

    position = cdrom_select_read_position(1, 0, 1, 400, 210, 211);
    CHECK(CDROM_READ_POSITION_SETLOC, 400, position,
          "pending SetLoc redirects an active Read");

    position = cdrom_select_read_position(0, 1, 0, 100, 210, 211);
    CHECK(CDROM_READ_POSITION_RESUME_LAST, 210, position,
          "Read after Pause repeats the most recently received sector");

    position = cdrom_select_read_position(0, 1, 1, 400, 210, 211);
    CHECK(CDROM_READ_POSITION_SETLOC, 400, position,
          "pending SetLoc wins over the paused resume position");

    position = cdrom_select_read_position(0, 1, 0, -1, -1, 12);
    CHECK(CDROM_READ_POSITION_KEEP_CURRENT, 12, position,
          "Read without prior sector keeps the controller cursor");

    position = cdrom_select_read_position(0, 0, 0, 100, 210, 211);
    CHECK(CDROM_READ_POSITION_KEEP_CURRENT, 211, position,
          "inactive non-Pause state does not repeat an old sector");

    if (failures) {
        fprintf(stderr, "FAILED (%d)\n", failures);
        return 1;
    }
    puts("ALL PASS");
    return 0;
}

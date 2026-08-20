#ifndef PSXRECOMP_CDROM_READ_POSITION_H
#define PSXRECOMP_CDROM_READ_POSITION_H

/*
 * Pure read-position policy for ReadN/ReadS.
 *
 * PSX-SPX "Setloc, Read, Pause":
 * - an unprocessed SetLoc redirects and is consumed by the next Read;
 * - a Read issued while already reading, without a pending SetLoc, continues;
 * - a Read issued after Pause, without a pending SetLoc, repeats the most
 *   recently received sector once;
 * - in any other inactive state, keep the controller's current cursor.
 */
typedef enum CDROMReadPositionAction {
    CDROM_READ_POSITION_CONTINUE = 0,
    CDROM_READ_POSITION_SETLOC,
    CDROM_READ_POSITION_RESUME_LAST,
    CDROM_READ_POSITION_KEEP_CURRENT
} CDROMReadPositionAction;

typedef struct CDROMReadPosition {
    CDROMReadPositionAction action;
    int lba;
} CDROMReadPosition;

static inline CDROMReadPosition cdrom_select_read_position(
    int reading,
    int read_paused,
    int setloc_pending,
    int setloc_lba,
    int last_sector_lba,
    int current_read_lba)
{
    CDROMReadPosition result;

    if (setloc_pending) {
        result.action = CDROM_READ_POSITION_SETLOC;
        result.lba = setloc_lba;
    } else if (reading) {
        result.action = CDROM_READ_POSITION_CONTINUE;
        result.lba = current_read_lba;
    } else if (read_paused && last_sector_lba >= 0) {
        result.action = CDROM_READ_POSITION_RESUME_LAST;
        result.lba = last_sector_lba;
    } else {
        result.action = CDROM_READ_POSITION_KEEP_CURRENT;
        result.lba = current_read_lba;
    }

    return result;
}

#endif /* PSXRECOMP_CDROM_READ_POSITION_H */

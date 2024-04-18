
#include <stdint.h>

#ifndef MOVEMENT_CUSTOM_BOOT_COMMANDS
#define MOVEMENT_CUSTOM_BOOT_COMMANDS()            \
    do                                             \
    {                                              \
        movement_state.settings.bit.time_zone = 3; \
        movement_location_t loc;                   \
        loc.bit.latitude = 4984;                   \
        loc.bit.longitude = 2401;                  \
        watch_store_backup_data(loc.reg, 1);       \
    } while (0);
#endif MOVEMENT_CUSTOM_BOOT_COMMANDS

// #ifdef WORLD_CLOCK2_FACE_H_

const uint8_t preselected_zones[] = {
    5,  // "GET", 4:00:00 (Georgia Standard Time)
    12, // "THA", 7:00:00 (Thailand Standard Time)
    31, // "MST", -7:00:00 (Mountain Standard Time)
    32, // "CST", -6:00:00 (Central Standard Time)
};
#define INITIAL_WORLD_ZONE (preselected_zones[0]);

#define PRESELECTED_ZONES_LENGTH (sizeof(preselected_zones) / sizeof(uint8_t))

// #endif
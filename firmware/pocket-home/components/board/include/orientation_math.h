#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Physical enclosure orientation, viewed from the display side.
 *
 * 0 degrees is the fixed product orientation: buttons on top, USB-C on bottom.
 * Positive angles mean the enclosure has been turned clockwise from that pose.
 */
typedef enum {
    BOARD_ROTATION_0 = 0,
    BOARD_ROTATION_90,
    BOARD_ROTATION_180,
    BOARD_ROTATION_270,
} board_rotation_t;

/**
 * Convert QMI8658 acceleration to the absolute enclosure orientation.
 *
 * The PCB silkscreen defines +X toward the right edge and +Y toward the button
 * edge when viewed from the display side. The function returns false while the
 * display is close to horizontal or close to a 45-degree boundary.
 */
bool board_rotation_from_acceleration(float x_g, float y_g, float z_g,
                                      board_rotation_t *rotation);

const char *board_rotation_name(board_rotation_t rotation);

#ifdef __cplusplus
}
#endif

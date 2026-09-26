#include "orientation_math.h"

#include <math.h>

#define ORIENTATION_MAX_ABS_Z_G 0.75f
#define ORIENTATION_MIN_IN_PLANE_G 0.55f
#define ORIENTATION_AXIS_MARGIN_G 0.15f

bool board_rotation_from_acceleration(float x_g, float y_g, float z_g,
                                      board_rotation_t *rotation)
{
    if (rotation == nullptr || fabsf(z_g) >= ORIENTATION_MAX_ABS_Z_G) {
        return false;
    }

    const float abs_x = fabsf(x_g);
    const float abs_y = fabsf(y_g);
    const float primary = fmaxf(abs_x, abs_y);
    const float secondary = fminf(abs_x, abs_y);
    if (primary < ORIENTATION_MIN_IN_PLANE_G ||
        primary - secondary < ORIENTATION_AXIS_MARGIN_G) {
        return false;
    }

    // PCB coordinates when viewed from the display side:
    //   +X -> right edge, +Y -> button edge, +Z -> out of the display.
    if (abs_y > abs_x) {
        *rotation = y_g > 0.0f ? BOARD_ROTATION_0 : BOARD_ROTATION_180;
    } else {
        *rotation = x_g > 0.0f ? BOARD_ROTATION_90 : BOARD_ROTATION_270;
    }
    return true;
}

const char *board_rotation_name(board_rotation_t rotation)
{
    switch (rotation) {
    case BOARD_ROTATION_90:
        return "90";
    case BOARD_ROTATION_180:
        return "180";
    case BOARD_ROTATION_270:
        return "270";
    default:
        return "0";
    }
}

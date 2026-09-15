#include <assert.h>
#include <stdio.h>

#include "orientation_math.h"

static void expect_rotation(float x, float y, float z,
                            board_rotation_t expected)
{
    board_rotation_t actual = BOARD_ROTATION_0;
    assert(board_rotation_from_acceleration(x, y, z, &actual));
    assert(actual == expected);
}

int main(void)
{
    // Physical reference: viewed from the display, +X is right and +Y points
    // toward the buttons. Accelerometers at rest report support acceleration.
    expect_rotation(0.00f, 0.98f, 0.05f, BOARD_ROTATION_0);
    expect_rotation(0.98f, 0.00f, 0.05f, BOARD_ROTATION_90);
    expect_rotation(0.00f, -0.98f, 0.05f, BOARD_ROTATION_180);
    expect_rotation(-0.98f, 0.00f, 0.05f, BOARD_ROTATION_270);

    board_rotation_t unchanged = BOARD_ROTATION_180;
    assert(!board_rotation_from_acceleration(0.10f, 0.10f, 0.98f,
                                             &unchanged));
    assert(unchanged == BOARD_ROTATION_180);
    assert(!board_rotation_from_acceleration(0.70f, 0.70f, 0.05f,
                                             &unchanged));
    assert(!board_rotation_from_acceleration(0.30f, 0.10f, 0.05f,
                                             &unchanged));
    assert(!board_rotation_from_acceleration(0.0f, 1.0f, 0.0f, nullptr));

    puts("orientation tests passed");
    return 0;
}

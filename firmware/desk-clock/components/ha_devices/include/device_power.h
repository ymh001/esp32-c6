#pragma once
enum device_power_t { DEVICE_UNKNOWN, DEVICE_OFF, DEVICE_ON, DEVICE_UNAVAILABLE };
device_power_t device_power_parse(const char *state, bool climate);

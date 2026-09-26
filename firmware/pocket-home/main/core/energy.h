#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
namespace pocket {
struct EnergySnapshot {
  double kwh = 0, cost = 0, balance = 0;
  time_t updated = 0, fetched = 0;
  uint8_t valid = 0;
  bool failed = false;
};
bool energy_parse(const char *json, size_t length, EnergySnapshot &out);
EnergySnapshot energy_snapshot();
void energy_init();
} // namespace pocket

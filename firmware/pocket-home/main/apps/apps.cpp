#include "apps.h"
namespace pocket {
const App *app_catalog(unsigned *count) {
  static const App apps[] = {
      {"clock", "时钟", true, flip_create, flip_tick, flip_close},
      {"energy", "耗电", false, energy_create, energy_tick, energy_close},
  };
  *count = sizeof(apps) / sizeof(apps[0]);
  return apps;
}
} // namespace pocket

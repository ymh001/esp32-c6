#include "cJSON.h"
#include "energy.h"
#include <cmath>
#include <cstring>
namespace pocket {
bool energy_parse(const char *json, size_t length, EnergySnapshot &out) {
  auto root = cJSON_ParseWithLength(json, length);
  auto result = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "results"), "A");
  auto status = cJSON_GetObjectItem(result, "status");
  auto frames = cJSON_GetObjectItem(result, "frames");
  bool ok = result && !cJSON_GetObjectItem(result, "error") &&
            cJSON_IsNumber(status) && status->valueint == 200 &&
            cJSON_GetArraySize(frames) == 1;
  EnergySnapshot next;
  if (ok) {
    auto frame = cJSON_GetArrayItem(frames, 0);
    auto fields =
        cJSON_GetObjectItem(cJSON_GetObjectItem(frame, "schema"), "fields");
    auto values =
        cJSON_GetObjectItem(cJSON_GetObjectItem(frame, "data"), "values");
    unsigned seen = 0;
    const char *names[] = {"kwh", "cost", "balance", "updated"};
    for (int i = 0; i < cJSON_GetArraySize(fields); ++i) {
      auto name = cJSON_GetObjectItem(cJSON_GetArrayItem(fields, i), "name");
      auto column = cJSON_GetArrayItem(values, i);
      if (!cJSON_IsString(name))
        continue;
      for (unsigned k = 0; k < 4; ++k) {
        if (strcmp(name->valuestring, names[k]))
          continue;
        seen |= 1U << k;
        auto value = cJSON_GetArrayItem(column, 0);
        if (cJSON_GetArraySize(column) != 1 || !cJSON_IsNumber(value) ||
            !std::isfinite(value->valuedouble))
          continue;
        double n = value->valuedouble;
        if (k == 3) {
          if (n >= 1704067200.0 && n < 4102444800.0)
            next.updated = (time_t)n;
        } else if (std::abs(n) < 1000000 && (k == 2 || n >= 0)) {
          if (k == 0)
            next.kwh = n;
          if (k == 1)
            next.cost = n;
          if (k == 2)
            next.balance = n;
          next.valid |= 1U << k;
        }
      }
    }
    ok = seen == 15 && next.valid && next.updated;
  }
  cJSON_Delete(root);
  if (ok)
    out = next;
  return ok;
}
} // namespace pocket

#include "core/energy.h"
#include "ui/rotation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
static std::string fixture(const char *values) {
  return std::string(
             R"({"results":{"A":{"status":200,"frames":[{"schema":{"fields":[{"name":"balance"},{"name":"updated"},{"name":"cost"},{"name":"kwh"}]},"data":{"values":)") +
         values + "}}]}}}";
}
int main() {
  pocket::EnergySnapshot e;
  auto parse = [&](const std::string &s) {
    return pocket::energy_parse(s.c_str(), s.size() + 1, e);
  };
  assert(parse(fixture("[[84.216],[1790361659.3],[0.198],[0.18]]")));
  assert(e.valid == 7 && e.kwh == .18 && e.cost == .198 &&
         e.updated == 1790361659);
  assert(parse(fixture("[[null],[1790361659.3],[0.198],[0.18]]")));
  assert(e.valid == 3);
  assert(parse(fixture("[[-12.5],[1790361659.3],[0.198],[0.18]]")));
  assert(e.balance == -12.5);
  auto saved = e;
  assert(!parse(
      "{\"results\":{\"A\":{\"status\":500,\"error\":\"db offline\"}}}"));
  assert(e.kwh == saved.kwh && e.valid == saved.valid);
  assert(!parse("not json"));
  assert(!parse(fixture("[[null],[1790361659],[null],[null]]")));
  assert(!parse(fixture("[[84],[null],[1],[1]]")));
  assert(!parse(fixture("[[],[],[],[]]")));
  // Non-square strip catches width/height and row-stride errors.
  const uint16_t src[] = {1, 2, 3, 4, 5, 6};
  const uint16_t expected[4][6] = {{1, 2, 3, 4, 5, 6},
                                   {3, 6, 2, 5, 1, 4},
                                   {6, 5, 4, 3, 2, 1},
                                   {4, 1, 5, 2, 6, 3}};
  for (unsigned r = 0; r < 4; ++r) {
    uint16_t dst[8] = {0, 0, 0, 0, 0, 0, 123, 456};
    rotate_strip(src, dst, 3, 2, r);
    assert(!memcmp(dst, expected[r], sizeof(src)) && dst[6] == 123 &&
           dst[7] == 456);
  }
  puts("PASS: energy parsing, partial/null/error responses, negative balance, "
       "rotated strips");
}

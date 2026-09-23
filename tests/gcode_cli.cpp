// Local preview/demo helper. Runs exactly the parser compiled into the master ESP.
#include "../shared/gcode.h"
#include <iostream>
#include <string>
int main() {
  timiac::GcodeState state; std::string line;
  while (std::getline(std::cin, line)) {
    timiac::Step step;
    const char* error = timiac::compileLine(line.c_str(), state, step);
    if (error) { std::cout << "{\"error\":\"" << error << "\"}\n"; return 1; }
    std::cout << "{";
    for (unsigned b = 0; b < 2; ++b) {
      std::cout << (b ? ",\"b\":[" : "\"a\":[");
      for (unsigned i = 0; i < step.count[b]; ++i) std::cout << (i ? ",\"" : "\"") << step.lines[b][i] << "\"";
      std::cout << "]";
    }
    std::cout << "}\n";
  }
}

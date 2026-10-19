#include "../shared/firmware_image.h"
#include <cassert>
#include <string>
#include <iostream>
int main() {
  std::string bytes = "TIMIAC_OTA_ROLE:master"; bytes += '\0';
  bytes += "TIMIAC2-123456789abc"; bytes += '\0'; bytes += "2026-10-19.2"; bytes += '\0';
  for (unsigned size = 1; size < bytes.size(); ++size) {
    timiac::FirmwareImageMarkers check;
    check.begin("TIMIAC_OTA_ROLE:master", "TIMIAC2-123456789abc", "2026-10-19.2");
    for (unsigned offset = 0; offset < bytes.size(); offset += size) check.write(reinterpret_cast<const uint8_t*>(bytes.data()+offset), std::min(size, unsigned(bytes.size()-offset)));
    assert(check.complete());
  }
  for (const char* wrong : {"TIMIAC_OTA_ROLE:master2", "TIMIAC2-123456789abcd", "2026-10-19.20"}) {
    timiac::FirmwareMarker check;
    check.begin(wrong[0] == '2' ? "2026-10-19.2" : wrong[6] == '_' ? "TIMIAC_OTA_ROLE:master" : "TIMIAC2-123456789abc");
    for (const char* p = wrong; *p; ++p) check.write(*p);
    check.write(0); assert(!check.complete());
  }
  timiac::FirmwareImageMarkers recovery;
  recovery.begin("TIMIAC_OTA_ROLE:master", "TIMIAC2-123456789abc", "2026-10-19.2", "TIMIAC_LOCAL_OTA_V1");
  recovery.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()); assert(!recovery.complete());
  const char* marker = "TIMIAC_LOCAL_OTA_V1";
  recovery.write(reinterpret_cast<const uint8_t*>(marker), strlen(marker)); assert(recovery.complete());
  std::cout << "Local firmware markers: every chunk boundary and wrong role/identity/revision prefix rejected\n";
}

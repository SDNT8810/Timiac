#include "../shared/tls_record_buffers.h"
#include "../shared/network_memory.h"
#include <assert.h>
#include <stdint.h>
#include <iostream>

int main() {
  // Full 16 KB records plus overhead, independent of all general-heap space.
  timiac::TlsRecordBuffers<16717, 16717> records;
  assert(!records.matches(2048) && records.matches(16717));
  for (unsigned session = 0; session < 1000; ++session) {
    auto* input = static_cast<unsigned char*>(records.allocate(16717));
    auto* output = static_cast<unsigned char*>(records.allocate(16717));
    assert(input && output && input != output);
    assert(!records.allocate(16717)); // Concurrent TLS cannot steal live records.
    for (unsigned n = 0; n < 16717; ++n) {
      assert(!input[n] && !output[n]); input[n] = 0x5a; output[n] = 0xa5;
    }
    assert(records.release(input));
    for (unsigned n = 0; n < 16717; ++n) assert(!input[n]);
    assert(records.allocate(16717) == input);
    assert(records.release(output) && records.release(input));
  }
  int unrelated;
  assert(!records.release(nullptr) && !records.release(&unrelated));
  timiac::TlsRecordBuffers<17000, 4200> asymmetric;
  auto* input = asymmetric.allocate(17000); auto* output = asymmetric.allocate(4200);
  assert(input && output && !asymmetric.allocate(17000) && !asymmetric.allocate(4200));
  assert(asymmetric.release(input) && asymmetric.release(output));
  // Recorded total/largest heap: charge dedicated records outside the heap,
  // retain a handshake/HTTP reserve and still reject genuine low memory.
  assert(timiac::networkMemoryReady(120628 - 38900 - 33434 + 4096, 14000, true, true));
  assert(timiac::networkMemoryReady(49152, 8192, true, true));
  assert(!timiac::networkMemoryReady(49151, 38900, true, true));
  assert(!timiac::networkMemoryReady(100000, 8191, true, true));
  assert(!timiac::networkMemoryReady(90000, 38900, true));
  assert(timiac::networkMemoryReady(24576, 8192, false, true));
  std::cout << "TLS records: 1000 allocation/cleanup cycles without heap allocation, no concurrent reuse, plaintext scrub and memory budgets passed\n";
}

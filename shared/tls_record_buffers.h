#pragma once
#include <stddef.h>
#include <string.h>

namespace timiac {
// Two full-size TLS records must survive arbitrary fragmentation of the general
// heap. Callers serialize access; smaller certificate/crypto allocations keep
// using the SDK allocator. Cloud sync and firmware downloads share these slots.
template<size_t InputBytes, size_t OutputBytes> class TlsRecordBuffers {
  alignas(8) unsigned char input[InputBytes], output[OutputBytes];
  bool inputUsed = false, outputUsed = false;
public:
  bool matches(size_t bytes) const { return bytes == InputBytes || bytes == OutputBytes; }
  void* allocate(size_t bytes) {
    if (bytes == InputBytes && !inputUsed) { inputUsed = true; memset(input, 0, InputBytes); return input; }
    if (bytes == OutputBytes && !outputUsed) { outputUsed = true; memset(output, 0, OutputBytes); return output; }
    return nullptr;
  }
  bool release(void* pointer) {
    if (pointer != input && pointer != output) return false;
    // Scrub plaintext before another session can acquire this record.
    volatile unsigned char* record = static_cast<unsigned char*>(pointer);
    const size_t bytes = pointer == input ? InputBytes : OutputBytes;
    for (size_t i = 0; i < bytes; ++i) record[i] = 0;
    if (pointer == input) inputUsed = false; else outputUsed = false;
    return true;
  }
};
}

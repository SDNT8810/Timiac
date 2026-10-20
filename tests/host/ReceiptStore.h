#pragma once
#include <map>
#include <string>
#include <limits>

// LittleFS-like files for testing the production atomic receipt writer/reader.
struct ReceiptFile {
  std::string* content = nullptr;
  size_t cursor = 0, limit = std::numeric_limits<size_t>::max();
  ReceiptFile() = default;
  ReceiptFile(std::string* bytes, size_t cap) : content(bytes), limit(cap) {}
  explicit operator bool() const { return content != nullptr; }
  size_t size() const { return content ? content->size() : 0; }
  size_t write(const uint8_t* data, size_t count) {
    if (!content) return 0;
    const size_t room = limit > content->size() ? limit - content->size() : 0;
    count = std::min(room, count); content->append(reinterpret_cast<const char*>(data), count); return count;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  int read() { return content && cursor < content->size() ? static_cast<unsigned char>((*content)[cursor++]) : -1; }
  size_t readBytes(char* data, size_t count) {
    count = content ? std::min(count, content->size() - cursor) : 0;
    if (count) memcpy(data, content->data() + cursor, count);
    cursor += count; return count;
  }
  void flush() {} void close() { content = nullptr; }
};
struct ReceiptStore {
  std::map<std::string, std::string> files;
  size_t writeLimit = std::numeric_limits<size_t>::max();
  bool failOpen = false, failRename = false;
  ReceiptFile open(const char* path, const char* mode) {
    if (failOpen) return {};
    if (*mode == 'w') { files[path].clear(); return ReceiptFile(&files[path], writeLimit); }
    const auto found = files.find(path);
    return found == files.end() ? ReceiptFile() : ReceiptFile(&found->second, writeLimit);
  }
  bool exists(const char* path) const { return files.count(path); }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (failRename || !exists(from)) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
};

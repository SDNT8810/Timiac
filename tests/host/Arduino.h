#pragma once
// Minimal host adapters for exercising the production coordinator with actual
// ArduinoJson packets. Network timing, radio delivery and hardware are simulated.
#include <stdint.h>
#include <string>
#include <type_traits>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>

class String {
  std::string value;
public:
  String() = default;
  String(const char* text) : value(text ? text : "") {}
  String(const std::string& text) : value(text) {}
  template<class T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  String(T number) : value(std::to_string(number)) {}
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }
  bool reserve(size_t capacity) { value.reserve(capacity); return true; }
  bool isEmpty() const { return value.empty(); }
  char operator[](size_t i) const { return value[i]; }
  String substring(size_t from, size_t to = std::string::npos) const { return value.substr(from, to == std::string::npos ? to : to - from); }
  int indexOf(const char* text) const { const auto i = value.find(text); return i == std::string::npos ? -1 : int(i); }
  bool startsWith(const char* text) const { return value.find(text) == 0; }
  bool endsWith(const char* text) const { const size_t n = strlen(text); return value.size() >= n && value.compare(value.size() - n, n, text) == 0; }
  void remove(size_t from, size_t count) { value.erase(from, count); }
  void trim() { const auto first = value.find_first_not_of(" \r\n\t"); if (first == std::string::npos) value.clear(); else value = value.substr(first, value.find_last_not_of(" \r\n\t") - first + 1); }
  bool concat(const char* text) { value += text; return true; }
  String& operator+=(const String& text) { value += text.value; return *this; }
  String& operator+=(char c) { value += c; return *this; }
  friend String operator+(const String& a, const String& b) { return a.value + b.value; }
  friend bool operator==(const String& a, const String& b) { return a.value == b.value; }
  friend bool operator!=(const String& a, const String& b) { return !(a == b); }
};

static uint32_t hostNow = 1000;
inline uint32_t millis() { return hostNow; }
inline uint32_t platformRandom() { return 1; }
struct IPAddress {
  IPAddress(unsigned = 0, unsigned = 0, unsigned = 0, unsigned = 0) {}
  String toString() const { return "192.168.4.2"; }
};
struct WiFiClient {
  bool connectedFlag = false, writeSucceeds = true, connectSucceeds = false;
  size_t writeCapacity = 4096, maxWrite = 4096;
  std::string wire, assembling;
  std::vector<String> sent;
  bool connected() const { return connectedFlag; }
  void stop() { connectedFlag = false; }
  size_t availableForWrite() const { return writeCapacity; }
  size_t available() const { return 0; }
  char read() { return 0; }
  void setTimeout(unsigned) {}
  void setNoDelay(bool) {}
  bool connect(IPAddress, unsigned) { connectedFlag = connectSucceeds; return connectedFlag; }
  bool connect(IPAddress ip, unsigned port, int) { return connect(ip, port); }
  IPAddress remoteIP() const { return IPAddress(); }
};
inline int writePeerChunk(WiFiClient& client, const char* data, size_t length) {
  if (!client.writeSucceeds) return -1;
  const size_t count = std::min(length, std::min(client.writeCapacity, client.maxWrite));
  client.writeCapacity -= count; client.wire.append(data, count);
  for (size_t i = 0; i < count; ++i) {
    client.assembling += data[i];
    if (data[i] == '\n') { client.sent.push_back(client.assembling); client.assembling.clear(); }
  }
  return int(count);
}
struct WiFiServer {
  bool hasClient() const { return false; }
  WiFiClient accept() { return WiFiClient(); }
};
constexpr unsigned WL_CONNECTED = 3;
static struct Wifi { unsigned status() const { return WL_CONNECTED; } void reconnect() {} } WiFi;
struct File {
  std::vector<uint8_t> bytes; size_t cursor = 0;
  void close() {} String readStringUntil(char) { return ""; }
  size_t read(uint8_t* data, size_t count) { count = std::min(count, bytes.size() - cursor); memcpy(data, bytes.data() + cursor, count); cursor += count; return count; }
};
static struct SerialPort {
  std::string sent, incoming; size_t offset = 0;
  bool hasOverrun() const { return false; }
  unsigned available() const { return incoming.size() - offset; }
  char read() { return incoming[offset++]; }
  void feed(const std::string& text) { assert(!available()); incoming = text; offset = 0; }
  void print(const char* text) { sent += text; }
  void print(const String& text) { sent += text.c_str(); }
  void print(char c) { sent += c; }
} Serial;

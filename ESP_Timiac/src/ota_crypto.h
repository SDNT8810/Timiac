#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#else
#include <bearssl/bearssl.h>
#endif

String otaHex(const uint8_t* bytes, unsigned count) {
  const char digits[] = "0123456789abcdef"; String result; result.reserve(count * 2);
  for (unsigned i = 0; i < count; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
  return result;
}
struct OtaSha256 {
#if defined(ARDUINO_ARCH_ESP32)
  mbedtls_sha256_context context;
  OtaSha256() { mbedtls_sha256_init(&context); }
  ~OtaSha256() { mbedtls_sha256_free(&context); }
  void begin() { mbedtls_sha256_starts_ret(&context, 0); }
  void write(const uint8_t* bytes, size_t size) { mbedtls_sha256_update_ret(&context, bytes, size); }
  String finish() { uint8_t hash[32]; mbedtls_sha256_finish_ret(&context, hash); return otaHex(hash, 32); }
#else
  br_sha256_context context;
  void begin() { br_sha256_init(&context); }
  void write(const uint8_t* bytes, size_t size) { br_sha256_update(&context, bytes, size); }
  String finish() { uint8_t hash[32]; br_sha256_out(&context, hash); return otaHex(hash, 32); }
#endif
};
String otaMac(const String& message) {
  uint8_t hash[32];
#if defined(ARDUINO_ARCH_ESP32)
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
    reinterpret_cast<const uint8_t*>(TIMIAC_CLOUD_DEVICE_KEY), strlen(TIMIAC_CLOUD_DEVICE_KEY),
    reinterpret_cast<const uint8_t*>(message.c_str()), message.length(), hash);
#else
  br_hmac_key_context key; br_hmac_context context;
  br_hmac_key_init(&key, &br_sha256_vtable, TIMIAC_CLOUD_DEVICE_KEY, strlen(TIMIAC_CLOUD_DEVICE_KEY));
  br_hmac_init(&context, &key, 32); br_hmac_update(&context, message.c_str(), message.length()); br_hmac_out(&context, hash);
#endif
  return otaHex(hash, 32);
}
bool otaEqual(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t different = 0; for (unsigned i = 0; i < a.length(); ++i) different |= a[i] ^ b[i];
  return !different;
}
bool otaHashValid(const String& hash) {
  if (hash.length() != 64) return false;
  for (unsigned i = 0; i < 64; ++i) if (timiac::firmwareHex(hash[i]) < 0) return false;
  return true;
}
String otaAuthorization(const String& nonce, const String& id, const String& target, const String& kind,
                        uint32_t size, const String& hash, uint32_t crc, const String& revision) {
  return otaMac("timiac-ota-v1|" + nonce + "|" + id + "|" + target + "|" + kind + "|" + String(size) + "|" + hash + "|" + String(crc) + "|" + revision);
}

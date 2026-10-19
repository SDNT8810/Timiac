#pragma once
String localFirmwareNonce;
String localFirmwareAuthorization(JsonVariant doc) {
  return otaMac("timiac-local-v1|" + bootId + "|" + localFirmwareNonce + "|" + doc["id"].as<String>() + "|" +
    doc["target"].as<String>() + "|" + doc["kind"].as<String>() + "|" + String(doc["size"].as<uint32_t>()) + "|" +
    doc["sha256"].as<String>() + "|" + String(doc["crc32"] | 0U) + "|" + doc["revision"].as<String>() + "|" +
    doc["machine"].as<String>() + "|" + String(doc["port"].as<unsigned>()) + "|" + doc["imageId"].as<String>());
}
const char* validateLocalFirmwareRequest(JsonVariant doc) {
  if (!doc.is<JsonObject>() || doc["session"] != bootId) return "Controller restarted; read local update info again";
  if (!localFirmwareNonce.length() || doc["nonce"] != localFirmwareNonce ||
      !doc["auth"].is<const char*>() || !strlen(TIMIAC_CLOUD_DEVICE_KEY)) return "Local update authentication failed";
  const String id = doc["id"] | "", image = doc["imageId"] | "", hash = doc["sha256"] | "";
  const String target = doc["target"] | "", kind = doc["kind"] | "", revision = doc["revision"] | "";
  if (!otaIdentifier(id) || !otaIdentifier(image) || !otaHashValid(hash) || !revision.length() || revision.length() > 32 ||
      !doc["port"].is<unsigned>() || doc["port"].as<unsigned>() < 1024 || doc["port"].as<unsigned>() > 65535 ||
      !doc["size"].is<uint32_t>()) return "Invalid local firmware manifest";
  if (doc["machine"] != timiac::machineId) return "Hardware identity changed; install a matched USB/SD package";
  const uint32_t size = doc["size"].as<uint32_t>();
  const bool master = target == "master" && (kind == "app" || kind == "filesystem");
  const bool bridge = (target == "esp1" || target == "esp2") && kind == "app";
  const bool octopus = (target == "octopus1" || target == "octopus2") && kind == "octopus";
  if ((!master && !bridge && !octopus) || size < 1024 ||
      (kind == "filesystem" ? size != 0x160000 : size > (octopus ? timiac::octopusFirmwareLimit : bridge ? 0x100000U : 0x140000U))) return "Wrong firmware target or size";
  if (!otaEqual(doc["auth"].as<String>(), localFirmwareAuthorization(doc))) return "Local update authentication failed";
  return nullptr;
}

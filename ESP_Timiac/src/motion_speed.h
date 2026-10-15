#pragma once

void configureMotionSpeed() {
  // Ignore previously saved slider values, including on upgrades.
  motionSpeedPercent = timiac::defaultSpeedPercent;
}
const char* saveMotionSpeed(JsonVariant doc) {
  if (!doc["percent"].is<unsigned>() || doc["percent"].as<unsigned>() != timiac::defaultSpeedPercent)
    return "Movement speed is fixed at 75%";
  return nullptr;
}

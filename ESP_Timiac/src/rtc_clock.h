#pragma once
RTC_DS3231 rtc;
bool rtcInitialized = false, rtcPresent = false, rtcValid = false;
String rtcText, rtcError = "DS3231 not connected";
DateTime rtcTime;
uint32_t rtcChecked = 0;
bool rtcBusReady = false, rtcSdaHigh = false, rtcSclHigh = false;
uint8_t rtcI2cCode = 0, rtcRegister = 0, rtcExpectedBytes = 0;
unsigned rtcReceivedBytes = 0;
const char* rtcStage = "not_started";

void configureClock() {
  rtcBusReady = Wire.begin(TIMIAC_RTC_SDA, TIMIAC_RTC_SCL, 100000);
  Wire.setTimeOut(20);
}
void sampleRtcLines() {
  rtcSdaHigh = digitalRead(TIMIAC_RTC_SDA);
  rtcSclHigh = digitalRead(TIMIAC_RTC_SCL);
}
void writeRtcStatus(JsonObject clock, bool diagnostics = false) {
  clock["present"] = rtcPresent; clock["valid"] = rtcValid;
  clock["localTime"] = rtcText; clock["error"] = rtcError;
  if (!diagnostics) return;
  clock["sdaPin"] = TIMIAC_RTC_SDA; clock["sclPin"] = TIMIAC_RTC_SCL;
  clock["address"] = "0x68"; clock["frequencyHz"] = 100000;
  clock["busStarted"] = rtcBusReady; clock["stage"] = rtcStage;
  clock["i2cCode"] = rtcI2cCode;
  clock["register"] = rtcRegister;
  clock["expectedBytes"] = rtcExpectedBytes; clock["receivedBytes"] = rtcReceivedBytes;
  clock["sdaHighAfterTransaction"] = rtcSdaHigh;
  clock["sclHighAfterTransaction"] = rtcSclHigh;
  clock["sampleAgeMs"] = millis() - rtcChecked;
}

bool rtcRead(uint8_t address, uint8_t* data, uint8_t count) {
  rtcStage = "register_read"; rtcRegister = address;
  rtcExpectedBytes = count; rtcReceivedBytes = 0;
  Wire.beginTransmission(0x68); Wire.write(address);
  rtcI2cCode = Wire.endTransmission(false);
  if (rtcI2cCode != 0) { sampleRtcLines(); return false; }
  rtcReceivedBytes = Wire.requestFrom(uint8_t(0x68), count);
  sampleRtcLines();
  if (rtcReceivedBytes != count) return false;
  for (unsigned i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}
unsigned rtcBcd(uint8_t value) { return (value >> 4) * 10 + (value & 15); }
void rtcTick(bool force = false) {
  if (!force && millis() - rtcChecked < 1000) return;
  rtcChecked = millis(); rtcValid = false; rtcText = "";
  rtcPresent = false; rtcI2cCode = rtcExpectedBytes = rtcReceivedBytes = rtcRegister = 0;
  rtcStage = "bus_init";
  if (!rtcBusReady) configureClock();
  if (!rtcBusReady) { rtcError = "ESP32 I2C initialization failed"; return; }
  rtcStage = "address_probe";
  Wire.beginTransmission(0x68);
  rtcI2cCode = Wire.endTransmission(true);
  sampleRtcLines();
  rtcPresent = rtcI2cCode == 0;
  if (!rtcPresent) {
    String pins = " (SDA GPIO" + String(TIMIAC_RTC_SDA) + ", SCL GPIO" + String(TIMIAC_RTC_SCL) + ")";
    if (rtcI2cCode == 2) rtcError = "RTC did not acknowledge I2C address 0x68" + pins;
    else if (rtcI2cCode == 5) rtcError = "RTC I2C bus timed out" + pins;
    else rtcError = "RTC I2C error " + String(rtcI2cCode) + pins;
    return;
  }
  rtcStage = "driver_init";
  if (!rtcInitialized) rtcInitialized = rtc.begin(&Wire);
  if (!rtcInitialized) { rtcError = "RTC acknowledged 0x68, but driver initialization failed"; return; }
  uint8_t raw[7], status;
  if (!rtcRead(0, raw, 7) || !rtcRead(0x0f, &status, 1)) {
    rtcError = "RTC acknowledged 0x68, but register read failed; open /api/rtc for details"; return;
  }
  rtcStage = "validate_time";
  if (status & 0x80) { rtcError = "RTC lost power; set the date and time"; return; }
  unsigned hour = rtcBcd(raw[2] & 0x3f);
  if (raw[2] & 0x40) hour = (rtcBcd(raw[2] & 0x1f) % 12) + ((raw[2] & 0x20) ? 12 : 0);
  if ((raw[5] & 0x80) || !timiac::validClockDate(2000 + rtcBcd(raw[6]), rtcBcd(raw[5] & 0x1f), rtcBcd(raw[4]), hour, rtcBcd(raw[1]), rtcBcd(raw[0]))) {
    rtcError = "Invalid RTC date; set the date and time"; return;
  }
  rtcTime = DateTime(2000 + rtcBcd(raw[6]), rtcBcd(raw[5] & 0x1f), rtcBcd(raw[4]),
                     hour, rtcBcd(raw[1]), rtcBcd(raw[0]));
  if ((raw[5] & 0x80) || !rtcTime.isValid()) { rtcError = "Invalid RTC date; set the date and time"; return; }
  char text[20];
  snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u", rtcTime.year(), rtcTime.month(),
           rtcTime.day(), rtcTime.hour(), rtcTime.minute(), rtcTime.second());
  rtcText = text; rtcValid = true; rtcError = ""; rtcStage = "ready";
}
bool parseRtcDate(const String& text, DateTime& date) {
  if (text.length() != 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') return false;
  for (unsigned i = 0; i < 19; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (text[i] < '0' || text[i] > '9') return false;
  }
  unsigned year = text.substring(0, 4).toInt(), month = text.substring(5, 7).toInt(), day = text.substring(8, 10).toInt();
  unsigned hour = text.substring(11, 13).toInt(), minute = text.substring(14, 16).toInt(), second = text.substring(17, 19).toInt();
  if (!timiac::validClockDate(year, month, day, hour, minute, second)) return false;
  date = DateTime(year, month, day, hour, minute, second);
  return date.isValid();
}
bool setRtcDate(const DateTime& date) {
  rtcTick(true);
  if (!rtcPresent || !rtcInitialized) return false;
  rtc.adjust(date);
  releaseClockMinute = -1; // Setting/synchronizing time is not an hourly tick.
  rtcTick(true);
  return rtcValid && rtcTime.unixtime() >= date.unixtime() && rtcTime.unixtime() - date.unixtime() <= 1;
}

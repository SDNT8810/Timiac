#pragma once
#include <driver/adc.h>
#include <esp_adc_cal.h>

// ADC1 remains available with AP + station Wi-Fi active. Allocate no heap and
// characterize once, rather than allocating/recharacterizing each sample.
adc1_channel_t proximityAdcChannel = ADC1_CHANNEL_6;
esp_adc_cal_characteristics_t proximityAdcCalibration = {};
bool configureProximityAdc(unsigned pin) {
  static const unsigned pins[] = {36, 37, 38, 39, 32, 33, 34, 35};
  unsigned channel = 0;
  while (channel < 8 && pins[channel] != pin) ++channel;
  if (channel == 8) return false;
  proximityAdcChannel = static_cast<adc1_channel_t>(channel);
  // Classic ESP32's most sensitive supported range: 0 dB / nominal 1.1 V.
  // The 1100 below is calibration's fallback Vref, not a selectable lower range.
  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK ||
      adc1_config_channel_atten(proximityAdcChannel, ADC_ATTEN_DB_0) != ESP_OK) return false;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_12, 1100, &proximityAdcCalibration);
  return true;
}
bool readProximityAdc(uint16_t& raw, uint32_t& millivolts, uint32_t& floorMillivolts) {
  // A median of nine rejects up to four isolated spikes without smoothing a
  // hand transition over the short gesture debounce.
  uint16_t samples[9];
  for (unsigned i = 0; i < 9; ++i) {
    const int value = adc1_get_raw(proximityAdcChannel);
    if (value < 0 || value > 4095) return false; // Never turn an ADC error into a hand.
    unsigned at = i;
    while (at && samples[at - 1] > value) { samples[at] = samples[at - 1]; --at; }
    samples[at] = value;
  }
  raw = samples[4];
  millivolts = esp_adc_cal_raw_to_voltage(raw, &proximityAdcCalibration);
  floorMillivolts = esp_adc_cal_raw_to_voltage(0, &proximityAdcCalibration);
  return true;
}

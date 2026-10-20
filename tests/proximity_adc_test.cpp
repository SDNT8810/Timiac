#include <cassert>
#include <iostream>
#include <vector>
#include <driver/adc.h>
#include <esp_adc_cal.h>
bool configureFails = false;
adc1_channel_t channel;
unsigned next = 0, characterizations = 0;
std::vector<int> samples;
esp_err_t adc1_config_width(adc_bits_width_t width) { assert(width == ADC_WIDTH_BIT_12); return configureFails ? -1 : ESP_OK; }
esp_err_t adc1_config_channel_atten(adc1_channel_t pin, adc_atten_t atten) { assert(atten == ADC_ATTEN_DB_0); channel = pin; return ESP_OK; }
int adc1_get_raw(adc1_channel_t pin) { assert(pin == channel && next < samples.size()); return samples[next++]; }
int esp_adc_cal_characterize(adc_unit_t unit, adc_atten_t atten, adc_bits_width_t width, uint32_t vref, esp_adc_cal_characteristics_t*) {
  assert(unit == ADC_UNIT_1 && atten == ADC_ATTEN_DB_0 && width == ADC_WIDTH_BIT_12 && vref == 1100); ++characterizations; return 0;
}
uint32_t esp_adc_cal_raw_to_voltage(uint32_t raw, const esp_adc_cal_characteristics_t* cal) { return cal->offset + raw / 4; }
#include "../ESP_Timiac/src/proximity_adc.h"
int main() {
  assert(!configureProximityAdc(27) && !characterizations); // Never touch Wi-Fi's ADC2.
  assert(configureProximityAdc(34) && channel == ADC1_CHANNEL_6 && characterizations == 1);
  uint16_t raw; uint32_t mv, floor;
  samples = {0, 0, 79, 80, 4095, 81, 80, 82, 4095}; next = 0;
  assert(readProximityAdc(raw, mv, floor) && raw == 80 && mv == 95 && floor == 75);
  samples.assign(9, 0); next = 0;
  assert(readProximityAdc(raw, mv, floor) && raw == 0 && mv == 75 && floor == 75);
  for (int bad : {-1, 4096}) {
    samples.assign(9, 80); samples[4] = bad; next = 0; assert(!readProximityAdc(raw, mv, floor));
  }
  assert(characterizations == 1); // No repeated calibration/heap work in the sampling loop.
  configureFails = true; assert(!configureProximityAdc(34));
  std::cout << "Production sensor ADC: ADC1 GPIO34, 0 dB, nine-sample median, zero-count floor and invalid-read rejection passed\n";
}

#pragma once
#include <driver/adc.h>
struct esp_adc_cal_characteristics_t { uint32_t offset = 75; };
int esp_adc_cal_characterize(adc_unit_t, adc_atten_t, adc_bits_width_t, uint32_t, esp_adc_cal_characteristics_t*);
uint32_t esp_adc_cal_raw_to_voltage(uint32_t, const esp_adc_cal_characteristics_t*);

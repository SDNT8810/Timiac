#pragma once
#include <stdint.h>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
enum adc1_channel_t { ADC1_CHANNEL_0, ADC1_CHANNEL_1, ADC1_CHANNEL_2, ADC1_CHANNEL_3,
                      ADC1_CHANNEL_4, ADC1_CHANNEL_5, ADC1_CHANNEL_6, ADC1_CHANNEL_7 };
enum adc_bits_width_t { ADC_WIDTH_BIT_12 = 3 };
enum adc_atten_t { ADC_ATTEN_DB_0 };
enum adc_unit_t { ADC_UNIT_1 = 1 };
esp_err_t adc1_config_width(adc_bits_width_t);
esp_err_t adc1_config_channel_atten(adc1_channel_t, adc_atten_t);
int adc1_get_raw(adc1_channel_t);

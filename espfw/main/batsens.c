
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include "batsens.h"

/* voltage divider for battery sensing is attached to GPIO8,
 * a.k.a. ADC1_CHANNEL_5 */
#define BSGPIO ADC_CHANNEL_5

static adc_cali_handle_t adc_calhan;
static adc_oneshot_unit_handle_t bs_adchan;

void batsens_init(void)
{
  /* Initialize the ADC for the battery sensor */
  /* 13 bit width (everything else throws an error anyways). */
  /* No attenuation (0 dB) - gives full-scale voltage of 0.75V
   * for the ADC. */
  adc_oneshot_unit_init_cfg_t unitcfg = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitcfg, &bs_adchan));
  adc_oneshot_chan_cfg_t chconf = {
    .bitwidth = ADC_BITWIDTH_13,
    .atten = ADC_ATTEN_DB_0,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(bs_adchan, BSGPIO, &chconf));
  adc_cali_line_fitting_config_t caliconfig = {
    .unit_id = ADC_UNIT_1,
    .bitwidth = ADC_BITWIDTH_13,
    .atten = ADC_ATTEN_DB_0,
  };
  ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&caliconfig, &adc_calhan));
}

float batsens_read(void)
{
  int adcv;
  if (adc_oneshot_read(bs_adchan, BSGPIO, &adcv) != ESP_OK) {
  ESP_LOGE("batsens.c", "adc_oneshot_read returned error!");
    adcv = -9999.99;
  }
  int v;
  if (adc_cali_raw_to_voltage(adc_calhan, adcv, &v) != ESP_OK) {
    ESP_LOGE("batsens.c", "adc_cali_raw_to_voltage returned error!");
    v = -9999.99;
  }
  /* Voltage divider: 1000000 Ohm towards "+", 47000 Ohm towards "-",
   * ADC maximum: 0.75V, but we don't care - adc_cali_raw_to_voltage
   * already returned mV! */
  float res = ((float)v / 1000.0 ) * ((1000000.0 + 47000.0) / 47000.0);
  ESP_LOGI("batsens.c", "v = %.2f (voltage on VD: %d mV, raw ADV: %d)", res, v, adcv);
  return res;
}


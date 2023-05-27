
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>
#include "secrets.h"
#include "wifiap.h"

static wifi_config_t wc = {
  .ap.ssid = "FoxisMobileWeatherStation",
  .ap.password = WIFIPSK,
  .ap.max_connection = 8,
  .ap.authmode = WIFI_AUTH_WPA2_PSK,
  .ap.pmf_cfg = {
    .required = true,
  },
};

static esp_netif_t * wifinetif;

void wifiap_init(void)
{
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifinetif = esp_netif_create_default_wifi_ap();
  esp_netif_dhcp_status_t dhcpst;
  ESP_ERROR_CHECK(esp_netif_dhcps_get_status(wifinetif, &dhcpst));
  if (dhcpst != ESP_NETIF_DHCP_STOPPED) {
    ESP_LOGI("wifiap.c", "Temporarily stopping DHCP server");
    /* DHCP is started, so stop it and restart after changing settings. */
    esp_netif_dhcps_stop(wifinetif);
  }
  ESP_ERROR_CHECK(esp_netif_set_hostname(wifinetif, "mobilews"));
  esp_netif_ip_info_t ipi;
  esp_netif_set_ip4_addr(&ipi.ip,10,5,5,1);
  esp_netif_set_ip4_addr(&ipi.gw,10,5,5,1);
  esp_netif_set_ip4_addr(&ipi.netmask,255,255,255,0);
  ESP_ERROR_CHECK(esp_netif_set_ip_info(wifinetif, &ipi));
  if (dhcpst != ESP_NETIF_DHCP_STOPPED) {
    esp_netif_dhcps_start(wifinetif);
  }

  wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wic));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
#if 0
  /* No 802.11b, only g and better.
   * Supporting 802.11b causes a MASSIVE waste of airtime, and it
   * hasn't been of any use for many years now. That is why at most
   * hacker camps, the network rules state "turn of that nonsense".
   * While we would love to do that, unfortunately Espressif is too
   * lazy to support that. It is - as of ESP-IDF 5.0.1 - not possible
   * to turn off b, the following line will just throw an "invalid
   * argument". */
  ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, (WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N)));
#endif
  /* Only 20 MHz channel width, not 40. */
  ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
}

void wifiap_on(void)
{
  esp_err_t e = esp_wifi_start();
  if (e != ESP_OK) {
    ESP_LOGE("wifiap.c", "Failed to start WiFi! Error returned: %s", esp_err_to_name(e));
  }
}

void wifiap_off(void)
{
  esp_err_t e = esp_wifi_stop();
  if (e != ESP_OK) {
    ESP_LOGE("wifiap.c", "Failed to stop WiFi! Error returned: %s", esp_err_to_name(e));
  }
}


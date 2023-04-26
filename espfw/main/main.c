#include <stdio.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "mobilenet.h"

static const char *TAG = "mobilews";

#define sleep_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

void app_main(void)
{
  ESP_LOGI(TAG, "Early initialization starting...");
  mn_init();
  ESP_LOGI(TAG, "Early initialization finished, waking LTE module...");
  mn_wakeltemodule();
  /* Send some commands to the IoT 6 click (uBlox Sara-R412M) module */
#if 0 /* one-off LTE module setup */
  /* These are settings that are saved in the NVRAM of the module, so
   * there is no need to execute these every time, just once on first
   * time setup. */
  /* According to u-blox docs, the module takes up to 5 seconds to power up. */
  sleep_ms(5000); /* NOTE: we cannot waitforltemoduleready here, because the
                   * configuration AT+CSGT may not have been executed yet. */
  sendatcmd("AT", 4);
  // Set greeting text - this is emitted whenever the module powers on,
  // so that we can know when it has finished powering on.
  sendatcmd("AT+CSGT=1,\"LTEmodule now ready\"", 4);
  // Configure IPv6 address format: :, not .
  sendatcmd("AT+CGPIAF=1,1,1,0", 4);
  // disconnect from network, needed for the following
  sendatcmd("AT+CFUN=0", 10);
  // set MNO-profile to generic europe (100)
  sendatcmd("AT+UMNOPROF=100", 4);
  // reboot to make that take effect
  sendatcmd("AT+CFUN=15", 4);
  waitforltemoduleready();
  sendatcmd("AT", 4);
  // disconnect from network, needed for the following
  sendatcmd("AT+CFUN=0", 10);
  /* configure context (we only use one, nr. 1, the module could handle up to 8)
   * So obviously, it's tempting to say "IPV4V6" here, and expect the R412M
   * to do something sane: Try to use IPv6, or IPv4, whatever is available
   * from the network, we really don't care. However, that is not what the
   * module does in this mode. What "IPV4V6" seems to mean for u-blox is
   * "Try to use both IPv4 and IPv6, and break completely unless BOTH are
   * available, throwing nonsense-error-messages on every command
   * attempting to use the network". Great work, u-blox, great work.
   * So it's 2023 and we have to resort to IPv4 only because we
   * simply cannot guarantee to always have IPv6 available in every
   * network. */
  sendatcmd("AT+CGDCONT=1,\"IP\",\"resiot.m2m\"", 10);
  // reboot again to make that take effect
  sendatcmd("AT+CFUN=15", 4);
  waitforltemoduleready();
  sendatcmd("AT", 4);
  // disconnect from network, needed for the following
  sendatcmd("AT+CFUN=0", 10);
  // we would want to enable IPv4+IPv6, but we can't -
  // see comment above AT+CGDCONT. So we enable IPv4 only.
  sendatcmd("AT+UPSD=0,0,0", 10);
  // dynamic IP. Cannot be set on R412M and defaults to this anyways.
  //sendatcmd("AT+UPSD=0,7,\"0.0.0.0\"", 10);
  // servicedomain: CS (voice), PS (data) or both. Should already
  // default to PS due to our european MNOPROFILE.
  sendatcmd("AT+USVCDOMAIN=2", 10);
  // Not sure if the following two are really needed, the ublox
  // documentation raises more questiosn than it answers, but
  // it probably does not hurt.
  //sendatcmd("AT+USIMSTAT=4", 4);
  //sendatcmd("AT+UCUSATA=4", 4);
  // reboot again to make that take effect
  sendatcmd("AT+CFUN=15", 4);
#endif /* one-off LTE module setup */
  /* Wait until the LTE module signals that it is ready. */
  mn_waitforltemoduleready();
  // Possibly useful for later: The module has a RTC - command AT+CCLK
  // Just send an "AT", so the module can see and set the correct baudrate.
  sendatcmd("AT", 4);
  // Do not echo back commands.
  sendatcmd("ATE0", 4);
  // Tell the module to send "verbose" error messages, even though
  // they really aren't what anybody in his right mind would call
  // verbose...
  sendatcmd("AT+CMEE=2", 4);
  // Set in- and output of socket functions to hex-encoded, so we don't need
  // to deal with escaping special characters.
  sendatcmd("AT+UDCONF=1,1", 4);
  // Show a bunch of info about the mobile network module
  sendatcmd("ATI", 4);
  // select active profile
  sendatcmd("AT+UPSD=0,100,1", 60);
  /* wait for network connection (but with timeout).
   * We don't really care if this succeeds or not, we'll just try to send
   * data anyways. */
  mn_waitfornetworkconn(181);
  mn_waitforipaddr(61);
  // Query status info - this is only for debugging really.
  sendatcmd("AT+CGACT?", 4);
  sendatcmd("AT+CGDCONT?", 4);
  // Lets open a network connection
  int sock = mn_opentcpconn("wetter.poempelfox.de", 80, 61);
  if (sock >= 0) {
    char tst[500]; int br;
    strcpy(tst, "GET /api/getlastvalue/11 HTTP/1.1\r\nHost: wetter.poempelfox.de\r\nConnection: close\r\n\r\n");
    mn_writesock(sock, tst, strlen(tst), 30);
    sleep_ms(3000); // FIXME
    while ((br = mn_readsock(sock, tst, sizeof(tst) - 1, 30)) > 0) {
      tst[br] = 0;
      ESP_LOGI(TAG, "Received HTTP reply: %s", tst);
    }
  } else {
    ESP_LOGE(TAG, "Connection to web server failed :( retcode %d", sock);
  }
  
  int i = 0;
  while (1) {
/*    char serialbuf[200];
    if (seriallineavailable()) {
      getserialline(serialbuf, sizeof(serialbuf));
      printf("[%d] received serial: '%s'\n", i, serialbuf);
    } else {
      printf("[%d] nothing received serial.\n", i);
    }*/
    printf(" [%d]", i);
    i++;
    sleep_ms(1000);
  }
}

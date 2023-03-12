#include <hardware/adc.h>
#include <hardware/rtc.h>
//#include <hardware/watchdog.h>
#include <pico/stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define UART_ID uart1
#define UART_IRQ UART1_IRQ
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

/* for now we use UART1 on GPIO-pins 4 to 7.
 * There are a few options here, including but not
 * limited to: UART0 on 0 to 3; UART1 on 4 to 7; UART1 on 8 to 11; UART0 on 12 to 15; */
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define UART_CTS_PIN 6
#define UART_RTS_PIN 7
#define POWER_PIN 0
#define RESET_PIN 1

volatile char serialinbuf[500];
volatile int serialinbufrpos = 0;
volatile int serialinbufwpos = 0;
volatile int linesavailable = 0;

/* Interrupt routine, called when there is a byte on the serial UART */
void on_uart_rx(void) {
  while (uart_is_readable(UART_ID)) {
    uint8_t ch = uart_getc(UART_ID);
    if (ch == '\r') { ch = '\n'; }
    if (ch == 0) continue;
    //printf("%c (%02x)", ch, ch);
    serialinbuf[serialinbufwpos] = ch;
    serialinbufwpos = (serialinbufwpos + 1) % sizeof(serialinbuf);
    if (ch == '\n') {
      linesavailable++;
      //printf("Got a line from serial.\n");
    }
  }
}

void getserialline(char * buf) {
  int rpos = 0;
  if (linesavailable > 0) {
    linesavailable--;
    while (serialinbuf[serialinbufrpos] != '\n') {
      buf[rpos] = serialinbuf[serialinbufrpos];
      serialinbufrpos = (serialinbufrpos + 1) % sizeof(serialinbuf);
      rpos++;
    }
    /* Advance to the next position for the next read */
    serialinbufrpos = (serialinbufrpos + 1) % sizeof(serialinbuf);
    buf[rpos] = 0;
    //printf("getserialline returning: '%s' rpos now %d wpos %d\n", buf, serialinbufrpos, serialinbufwpos);
    return;
  } else {
    printf("Warning: Calling getserialline but no line is available.\n");
    buf[0] = 0;
    return;
  }
}

/* Read serial line with timeout.
 * This stops reading on a \r or \n, or after a timeout. The \r or \n
 * are NOT in the returned string.
 * Returns: number of bytes read, or <0 on error.
 * valid errors are: -1 timeout hit, -2 buffer too small.
 */
int readseriallinewto(uint8_t * buf, int buflen, float timeout)
{
  int res = 0;
  absolute_time_t toend = make_timeout_time_ms(timeout * 1000.0);
  if (buflen <= 1) { return -2; }
  do {
    if (linesavailable > 0) {
      getserialline(buf);
      return strlen(buf);
    }
    /* best_effort_wfe_or_timeout returns true if timeout is hit. */
  } while (best_effort_wfe_or_timeout(toend) == false);
  buf[res] = 0;
  return -1;
}

/*
 * Note that the timeout here is the timeout PER LINE. This may
 * theoretically wait for multiple times the timeout if there is
 * constant output.
 */
int waitforatreplywto(uint8_t * buf, int buflen, float timeout)
{
  int res = 0;
  int lastreadrc = 0;
  uint8_t * cptr = &buf[res];
  do {
    lastreadrc = readseriallinewto(cptr, buflen - res, timeout);
    if (lastreadrc > 0) {
      res += lastreadrc;
      /* Is this either "OK" or "ERROR" or "+CME ERROR.*"? Then this
       * is the end of the reply to the last AT command. */
      if ((strcmp(cptr, "OK") == 0)
       || (strcmp(cptr, "ERROR") == 0)
       || (strncmp(cptr, "+CME ERROR", 9) == 0)) {
        return res;
      }
      if ((res + 1) < buflen) {
        buf[res] = '\n';
        res++;
      }
      cptr = &buf[res];
    }
  } while (lastreadrc >= 0);
  buf[res] = 0;
  return -1;
}

int sendatcmd(uint8_t * cmd, float timeout)
{
  uint8_t rcvbuf[300];
  while (uart_is_readable(UART_ID)) {
    uint8_t c = uart_getc(UART_ID);
    printf("Warning: clearing unread byte from RX queue: %02x\n", c);
  }
  sprintf(rcvbuf, "%s\r\n", cmd);
  //printf("Before crappy PICO-SDK function: '%s'\n", rcvbuf);
  uart_write_blocking(UART_ID, rcvbuf, strlen(rcvbuf));
  //printf("After crappy PICO-SDK function.\n");
  int res = waitforatreplywto(&rcvbuf[0], sizeof(rcvbuf), timeout);
  printf("Sent '%s', Received serial: error=%s, Text '%s'\n", cmd, ((res < 0) ? "Yes" : "No"), rcvbuf);
}

/* Return the network state, as returned in AT+COPS.
 * So 3 = GPRS, 7 = LTE-M?, 9 = NB-IOT?
 * -2 if AT+COPS could not be read. -1 if not connected. */
int readnetworkstate(void)
{
  uint8_t rcvbuf[300];
  sprintf(rcvbuf, "%s\r\n", "AT+COPS?");
  printf("...readnetworkstate: sending AT+COPS?\n");
  uart_write_blocking(UART_ID, rcvbuf, strlen(rcvbuf));
  int res = waitforatreplywto(&rcvbuf[0], sizeof(rcvbuf), 0.5);
  if (res <= 0) { return -2; }
  printf("...readnetworkstate: read %s\n", rcvbuf);
  char * sp1; char * sp2;
  char * st1 = strtok_r(rcvbuf, "\n", &sp1);
  do {
    if (strncmp(st1, "+COPS:", 6) == 0) {
      char * st2 = strtok_r(st1, ",", &sp2);
      int ctr = 1;
      do {
        if (ctr == 4) {
          return strtol(st2, NULL, 10);
        }
        st2 = strtok_r(NULL, ",", &sp2);
        ctr++;
      } while (st2 != NULL);
    }
    st1 = strtok_r(NULL, "\n", &sp1);
  } while (st1 != NULL);
  /* There was no access technology in +COPS output, so we're not connected. */
  return -1;
}

void waitfornetwork(float timeout)
{
  int nws;
  absolute_time_t toend = make_timeout_time_ms(timeout * 1000.0);
  do {
    nws = readnetworkstate();
    printf("network state: %d\n", nws);
    if (nws <= 0) {
      sleep_ms(500);
    } else {
      /* we have a valid network connection. */
      if (nws == 3) { /* GPRS */
        /* GPRS requires manual context activation, LTE does it automatically */
        printf("GPRS connection, sending AT+CGACT=1,1...\n");
        sendatcmd("AT+CGACT=1,1", 30.0);
      }
      return;
    }
  } while (absolute_time_diff_us(get_absolute_time(), toend) > 0);
}

void wakeltemodule(void)
{
    /* Note: The PWR pin is inverted on the click board - so we need to pull
     * it high for enabling it. */
    gpio_put(POWER_PIN, true);
    sleep_ms(500);
    gpio_put(POWER_PIN, false);
}

void resetltemodule(void)
{
    /* Note: The PWR pin is inverted on the click board - so we need to pull
     * it high for enabling it. */
    gpio_put(RESET_PIN, true);
    sleep_ms(500);
    gpio_put(RESET_PIN, false);
}

int main(void)
{
    // Unfortunately, the WDT has a timeout of at most 8.3 seconds.
    //watchdog_enable(121000, true);

    stdio_init_all();
    sleep_ms(2000); /* This is really just here to allow the serial console to connect */
    printf("MobileWS starting!\r\n");

    /* Prepare RTC for use later */
    rtc_init();
    /* Prepare ADC for use later */
    adc_init();
    
    /* Let's get serial */
    /* The example puts uart_init BEFORE gpio_set_function which seems rather
     * weird/wrong, but I guess they know what they're doing?! */
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_CTS_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RTS_PIN, GPIO_FUNC_UART);
    /* We do use flow control */
    uart_set_hw_flow(UART_ID, true, true);
    /* set the rest of the parameters */
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, true);
    uart_set_translate_crlf(UART_ID, false);
    // set up and enable UART RX interrupt handler
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);
    /* Configure the GPIOs for PWR / RST of the LTE module */
    gpio_init(POWER_PIN);
    gpio_set_dir(POWER_PIN, GPIO_OUT);
    gpio_set_drive_strength(POWER_PIN, GPIO_DRIVE_STRENGTH_4MA);
    gpio_put(POWER_PIN, false);
    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_OUT);
    gpio_set_drive_strength(RESET_PIN, GPIO_DRIVE_STRENGTH_4MA);
    gpio_put(RESET_PIN, false);
    /* From the MIKROE page: "If the device is already powered up, a LOW pulse
     * with a duration of 1.5s on this pin will power the module down." - so
     * Unfortunately, this does not seem to work reliably for guaranteeing
     * a reset. So we also cabled the reset pin. */
    wakeltemodule();
    resetltemodule();
    
    printf("Sleeping for a bit to allow things to come up...\r\n");

    /* According to u-blox docs, the module takes up to 5 seconds to power up. */
    sleep_ms(6000);

    /* Send some commands to the IoT 6 click (uBlox Sara-R412M) module */
#if 0 /* one-off LTE module setup */
    /* These are settings that are saved in the NVRAM of the module, so
     * there is no need to execute these every time, just once on first
     * time setup. */
    sendatcmd("AT", 4.0);
    // Set greeting text - this is emitted whenever the module powers on,
    // so that we can know when it has finished powering on.
    sendatcmd("AT+CSGT=1,\"LTEmodule now ready\"", 4.0);
    // Configure IPv6 address format: :, not .
    sendatcmd("AT+CGPIAF=1,1,1,0", 4.0);
    // disconnect from network, needed for the following
    sendatcmd("AT+CFUN=0", 10.0);
    // set MNO-profile to generic europe (100)
    sendatcmd("AT+UMNOPROF=100", 4.0);
    // reboot to make that take effect
    sendatcmd("AT+CFUN=15", 4.0);
    // FIXME: hardware flow control always gets stuck after the reboot.
    sleep_ms(8000);
    printf("slept.\n");
    sendatcmd("AT", 4.0);
    // disconnect from network, needed for the following
    sendatcmd("AT+CFUN=0", 10.0);
    // configure context (we only use one, nr. 1, the module could handle up to 8)
    sendatcmd("AT+CGDCONT=1,\"IPV4V6\",\"resiot.m2m\"", 10.0);
    // reboot again to make that take effect
    //sendatcmd("AT+CFUN=15", 4.0);
    sleep_ms(8000);
    sendatcmd("AT", 4.0);
    // disconnect from network, needed for the following
    sendatcmd("AT+CFUN=0", 10.0);
    // we want to enable IPv4+IPv6
    sendatcmd("AT+UPSD=0,0,3", 10.0);
    // servicedomain: CS (voice), PS (data) or both. Should already
    // default to PS due to our european MNOPROFILE.
    sendatcmd("AT+USVCDOMAIN=1", 10.0);
    // Not sure if the following two are really needed, the ublox
    // documentation raises more questiosn than it answers, but
    // it probably does not hurt.
    sendatcmd("AT+USIMSTAT=4", 4.0);
    sendatcmd("AT+UCUSATA=4", 4.0);
    // reboot again to make that take effect
    sendatcmd("AT+CFUN=15", 4.0);
    sleep_ms(8000);
#endif /* one-off LTE module setup */
    // Possibly useful for later: The module has a RTC - command AT+CCLK
    // Just send an "AT", so the module can see and set the correct baudrate.\r\n");
    sendatcmd("AT", 4.0);
    // Tell the module to send "verbose" error messages, even though
    // they really aren't what anybody in his right mind would call
    // verbose...
    sendatcmd("AT+CMEE=2", 4.0);
    // Show a bunch of info about the module
    sendatcmd("ATI", 4.0);
    // wait for network connection (but with timeout)
    waitfornetwork(60.0);
    // select active profile
    sendatcmd("AT+UPSD=0,100,1", 15.0);
    // try non-async DNS resolution
    sendatcmd("AT+UDNSRN=0,\"www.poempelfox.de\",0,1", 30.0);

    /* Main loop */
    do {
      printf("Loopy MC Loop...\r\n");
      sleep_ms(10000);
    } while (1);
    printf("Exited main loop.\r\n");
    
    printf("main() will now return 0...\r\n");
    return 0;
}


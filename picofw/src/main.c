#include <hardware/adc.h>
#include <hardware/rtc.h>
#include <hardware/sync.h>
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

/* we use UART1 on GPIO-pins 4 to 7.
 * There are a few options here, including but not
 * limited to: UART0 on 0 to 3; UART1 on 4 to 7; UART1 on 8 to 11; UART0 on 12 to 15; */
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define UART_CTS_PIN 6
#define UART_RTS_PIN 7
/* We also need an I/O to connect to the "power on" pin of the ublox module. */
#define POWER_PIN 0

volatile char serialinbuf[300];
volatile int serialinbufrpos = 0;
int serialinbufwpos = 0; /* this is ONLY used by the IRQ routine */
volatile int linesavailable = 0;

/* Interrupt routine, called when there is a byte on the serial UART */
void on_uart_rx(void) {
  while (uart_is_readable(UART_ID)) {
    uint8_t ch = uart_getc(UART_ID);
    //printf("%c (%02x)", ((ch == 0) ? ' ' : ch), ch);
    if (ch == '\r') continue;
    if (ch == 0) continue;
    serialinbuf[serialinbufwpos] = ch;
    serialinbufwpos = (serialinbufwpos + 1) % sizeof(serialinbuf);
    if (ch == '\n') {
      linesavailable++;
      //printf("Got a line from serial.\n");
    }
  }
}

int getlinesavailable(void)
{
  int res;
  uint32_t irqs = save_and_disable_interrupts();
  res = linesavailable;
  restore_interrupts(irqs);
  return res;
}

void getserialline(char * buf) {
  int rpos = 0;
  if (getlinesavailable() > 0) {
    /* This obviously must not execute in parallel with the IRQ routine. */
    uint32_t irqs = save_and_disable_interrupts();
    linesavailable--;
    restore_interrupts(irqs);
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
    printf("Warning: Calling getserialline but no line is available.\r\n");
    buf[0] = 0;
    return;
  }
}

/* Read serial line with timeout.
 * This stops reading on a \n, or after a timeout. The \n is NOT in the
 * returned string.
 * Returns: number of bytes read, or <0 on error.
 * valid errors are: -1 timeout hit, -2 buffer too small.
 */
int readseriallinewto(uint8_t * buf, int buflen, float timeout)
{
  int res = 0;
  absolute_time_t toend = make_timeout_time_ms(timeout * 1000.0);
  if (buflen <= 1) { return -2; }
  int ctr = 0;
  do {
    if (ctr > 79) { printf("\r\n"); ctr = 0; }
    printf("_"); fflush(stdout); sleep_ms(10);
    ctr++;
    if (getlinesavailable() > 0) {
      getserialline(buf);
      return strlen(buf);
    }
    /* best_effort_wfe_or_timeout returns true if timeout is hit. */
  } while (time_reached(toend) == false);
  //} while (best_effort_wfe_or_timeout(toend) == false);
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

/* Sends data out the serial port.
 * Linefeeds need to be included in line! */
void sendserialline(uint8_t * line)
{
  // This is a modified implementation of uart_write_blocking, with the
  // modification being that we are not braindead, and therefore ADD A FSCKING
  // TIMEOUT so we don't loop indefinitely on a port that became stuck.
  //uart_write_blocking(UART_ID, line, strlen(line));
  static int lastwritetimedout = 0;
  size_t len = strlen(line);
  for (size_t i = 0; i < len; i++) {
    if (lastwritetimedout > 0) {
      if (uart_is_writable(UART_ID)) { /* It's writeable again, so reset the state "It's broken" */
        lastwritetimedout = 0;
        printf("sendserialline: port seems to work again.\r\n");
      }
    } else {
      absolute_time_t toend = make_timeout_time_ms(10.0 * 1000.0);
      while (!uart_is_writable(UART_ID)) {
        if (time_reached(toend)) {
          lastwritetimedout = 1; /* Mark it as broken, so we don't wait again on the next byte. */
          printf("sendserialline: port seems to be broken.\r\n");
          break;
        }
      }
    }
    if (lastwritetimedout == 0) {
      uart_get_hw(UART_ID)->dr = *line;
      line++;
    }
  }
}

/*
 * sends an AT command to the LTE module, and waits for it to return a reply,
 * whether it be an OK or an ERROR. With timeout.
 */
int sendatcmd(uint8_t * cmd, float timeout)
{
  uint8_t rcvbuf[500];
  printf("sendatcmd: about to send '%s'\r\n", cmd);
  sleep_ms(101);
  sprintf(rcvbuf, "%s\r\n", cmd);
  sendserialline(rcvbuf);
  printf("...sent\r\n");
  sleep_ms(101);
  int res = waitforatreplywto(&rcvbuf[0], sizeof(rcvbuf), timeout);
  printf("Sent '%s', Received serial: error=%s, Text '%s'\n", cmd, ((res < 0) ? "Yes" : "No"), rcvbuf);
}

/* Return the network state, as returned in AT+COPS.
 * So 3 = GPRS, 7 = LTE-M?, 9 = NB-IOT?
 * -2 if AT+COPS could not be read. -1 if not connected. */
int readnetworkstate(void)
{
  uint8_t rcvbuf[300];
  printf("...readnetworkstate: sending AT+COPS?\n");
  sprintf(rcvbuf, "%s\r\n", "AT+COPS?");
  sendserialline(rcvbuf);
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

/* Tries to wait until the mobile network has a data connection, with a timeout.
 * This will also automatically handle the case where the module did a
 * fallback to 2G, in which case we have to send an extra command to
 * enable the GPRS data connection. */
void mn_waitfornetworkconn(float timeout)
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
        printf("GPRS connection detected, sending AT+CGACT=1,1...\n");
        sendatcmd("AT+CGACT=1,1", 30.0);
      }
      return;
    }
  } while (absolute_time_diff_us(get_absolute_time(), toend) > 0);
}

void mn_resolvedns(uint8_t * hostname, uint8_t * obuf, int obufsize, float timeout)
{
  uint8_t buf[500];
  sprintf(buf, "AT+UDNSRN=0,\"%s\"\r\n", hostname);
  sendserialline(buf);
  int res = waitforatreplywto(buf, sizeof(buf), timeout);
  if (res <= 0) { /* No success or failure within timeout */
    strcpy(obuf, "");
    return;
  }
  char * sp1;
  char * st1 = strtok_r(buf, "\n", &sp1);
  do {
    if (strncmp(st1, "+UDNSRN: \"", 10) == 0) {
      /* This is the line containing the answer */
      int bufpos = 10;
      while ((st1[bufpos] != 0) && (obufsize > 1)) {
        if (st1[bufpos] == '"') { /* The " marks the end of the IP */
          break;
        }
        /* Copy one char and advance to next */
        *obuf = st1[bufpos];
        obufsize--;
        bufpos++;
      }
      *obuf = 0;
      return;
    }
    st1 = strtok_r(NULL, "\n", &sp1);
  } while (st1 != NULL);
  /* No DNS reply found in output. Return empty string to signal the error. */
  strcpy(obuf, "");
}

/* Closes a socket number.
 * This does not care about errors, and it uses the asynchronous version of
 * the command, because we really don't care and don't want to wait for a reply.
 * The reply will come at some later time in the form of an URC and will be
 * ignored anyways.
 */
void mn_closesocket(int socketnr)
{
  uint8_t buf[80];
  sprintf(buf, "AT+USOCL=%d,1\r\n", socketnr);
  sendserialline(buf);
}

/* Opens a TCP connection to hostname on port.
 * returns a socket number (should be between 0 and 6 because the module can
 * only create 7 sockets at a time), or <0 on error. */
int mn_opentcpconn(uint8_t * hostname, uint16_t port, float timeout)
{
  uint8_t buf[500];
  uint8_t ipasstring[42];
  int socket = -1;
  mn_resolvedns(hostname, ipasstring, sizeof(ipasstring), timeout);
  if (strlen(ipasstring) < 4) { /* DNS resolution failed */
    return -1;
  }
  sendserialline("AT+USOCR=6\r\n"); /* Get a TCP socket with random local port */
  int res = waitforatreplywto(buf, sizeof(buf), timeout);
  if (res <= 0) { return -2; }
  char * sp1;
  char * st1 = strtok_r(buf, "\n", &sp1);
  do {
    if (strncmp(st1, "+USOCR: ", 8) == 0) {
      /* This is the line containing the Socket ID */
      socket = strtol(&st1[8], NULL, 10);
      break;
    }
    st1 = strtok_r(NULL, "\n", &sp1);
  } while (st1 != NULL);
  if (socket < 0) { /* no valid socket in +USOCR line, or no +USOCR at all. */
    return -3;
  }
  /* Now connect that socket with the connect command */
  sprintf(buf, "AT+USOCO=%d,\"%s\",%u,0\r\n", socket, ipasstring, port);
  sendserialline(buf);
  res = waitforatreplywto(buf, sizeof(buf), timeout);
  if (res <= 0) { /* We ran into timeout. */
    /* Try to not leave a mess behind, send command to close the socket */
    mn_closesocket(socket);
    return -4;
  }
  /* look for "OK", otherwise close socket again */
  st1 = strtok_r(buf, "\n", &sp1);
  do {
    if (strncmp(st1, "OK", 2) == 0) { /* We received an "OK"! */
      /* The socket should now be connected. */
      return socket;
    }
    st1 = strtok_r(NULL, "\n", &sp1);
  } while (st1 != NULL);
  /* If we reach this, there was some error connecting the socket.
   * Free the socket number by closing it (asynchronously). */
  mn_closesocket(socket);
  return -5;
}

/* Attempts to write data to a network socket.
 * Errors are just silently ignored.
 */
void mn_writesock(int socket, uint8_t * buf, int bufsize, float timeout)
{
  if (socket < 0) { return; }
  if (bufsize <= 0) { return; }
  while (bufsize > 0) {
    int bts = bufsize;
    char wrstr[400];
    if (bts > 128) { bts = 128; /* Write at most 128 bytes at once. */ }
    sprintf(wrstr, "AT+USOWR=%d,%d,\"", socket, bts);
    for (int i = 0; i < bts; i++) {
      char prch[3]; int nib;
      nib = ((*buf) >> 4) & 0x0f;
      if (nib <= 9) {
        prch[0] = '0' + nib;
      } else {
        prch[0] = 'a' + nib;
      }
      nib = ((*buf) >> 0) & 0x0f;
      if (nib <= 9) {
        prch[1] = '0' + nib;
      } else {
        prch[1] = 'a' + nib;
      }
      prch[2] = 0;
      strcat(wrstr, prch);
      buf++; /* Advance to next position in buffer */
    }
    strcat(wrstr, "\"\r\n");
    sendatcmd(wrstr, timeout);
    bufsize -= bts; /* Update size of remaining buffer */
  }
}

/* Attempts to read data from a network socket.
 * Returns: Number of bytes actually read.
 */
int mn_readsock(int socket, uint8_t * buf, int bufsize, float timeout)
{
  int res = 0;
  if (socket < 0) { return res; }
  while (bufsize > 0) {
    int btr = bufsize;
    char rdstr[400];
    if (btr > 128) { btr=128; /* Read at most 128 bytes at once. */ }
    sprintf(rdstr, "AT+USORD=%d,%d", socket, btr);
    sendserialline(rdstr);
    int rc = waitforatreplywto(rdstr, sizeof(rdstr), timeout);
    if (rc <= 0) break;
    char * sp1;
    char * st1 = strtok_r(rdstr, "\n", &sp1);
    do {
      if (strncmp(st1, "+USORD: ", 8) == 0) {
        /* This contains the read output. */
        int riti = 0; /* # bytes read in this iteration */
        char * rp = st1 + 8;
        /* We could check the socket id in the reply, but since we're using
         * this in blocking mode it's not strictly needed.
         * So we just skip ahead to the '"'. */
        while ((*rp != '"') && (*rp != 0)) { rp++; }
        if (*rp == 0) { // End of the string and no '"'.
          /* That also means we did read 0 bytes, and thus abort reading here. */
          return res;
        }
        rp++;
        while ((*rp != '"') && (*rp != 0)) { // Loop until end of string.
          unsigned int b;
          if (sscanf(rp, "%2x", &b) == 1) { // Try to read 2 bytes as hex
            *buf = b & 0xff;
            buf++;
            bufsize--;
            riti++;
            res++;
          } else { // Reading 2 bytes as hex failed. Abort here.
            break;
          }
        }
        if (riti == 0) { // 0 bytes read in this iteration. End reading.
          return res;
        }
      }
      st1 = strtok_r(NULL, "\n", &sp1);
    } while (st1 != NULL);
  }
  return res;
}

void wakeltemodule(void)
{
  /* Note: The PWR pin is inverted on the click board - so we need to pull
   * it high for enabling it even though it's low active according to
   * R412M documentation. */
  /* The data sheet says that this needs to be pulled for 0.15 to 3.2 seconds.
   * Which of course directly contradicts the next line, where it says that
   * more than 1.5 seconds will turn it off. */
  gpio_put(POWER_PIN, true);
  sleep_ms(600);
  gpio_put(POWER_PIN, false);
}

/* From the MIKROE docs: "If the device is already powered up, a LOW pulse
 * with a duration of 1.5s on this pin will power the module down.".
 * Unfortunately, this did not seem to work reliably for guaranteeing
 * a reset. So we tried cabling the reset pin.
 * Then we read that this cannot be used either, because is only intended
 * "for emergencies", and can break the module. It also would need to be
 * pulled for 10 whole seconds. This module is one absolute mess. */
#if 0
void resetltemodule(void)
{
  /* Note: The Reset pin is inverted on the click board - so we need to pull
   * it high for enabling it. */
  gpio_put(RESET_PIN, true);
  sleep_ms(1000);
  gpio_put(RESET_PIN, false);
}

/* Tries to trigger a shutdown of the LTE module. You will need to sleep for
 * a moment after issuing this, because it only triggers the shutdown, and that
 * might take a while.
 * Unfortunately, this simply does not work. The module just completely
 * ignores it, or sees it as poweron. */
void poweroffltemodule(void)
{
  gpio_put(POWER_PIN, true);
  sleep_ms(3300);
  gpio_put(POWER_PIN, false);
}
#endif

void waitforltemoduleready(void)
{
  uint8_t buf[500];
  /* Timeout is 7.5 because specification says it may need at most 5 seconds
   * to come up. */
  int res;
  do {
    res = readseriallinewto(buf, sizeof(buf), 7.5);
    if (res >= 19) {
      if (strncmp(buf, "LTEmodule now ready", 19) == 0) {
        printf("LTEmodule reported ready.\r\n");
        return;
      }
    }
  } while (res > 0);
  printf("Timed out waiting for LTEmodule to become ready.\r\n");
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
    uart_set_fifo_enabled(UART_ID, false);
    uart_set_translate_crlf(UART_ID, false);
    // set up and enable UART RX interrupt handler
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);
    /* Configure the GPIOs for PWR / RST of the LTE module */
    gpio_init(POWER_PIN);
    gpio_put(POWER_PIN, false);
    gpio_set_dir(POWER_PIN, GPIO_OUT);
    gpio_set_drive_strength(POWER_PIN, GPIO_DRIVE_STRENGTH_4MA);
    //printf("LTE poweroff...\r\n");
    //poweroffltemodule();
    //sleep_ms(10000);
    
    printf("LTE poweron...\r\n");
    wakeltemodule();

    printf("Sleeping for a tiny bit to allow things to come up...\r\n");
    sleep_ms(1000);

    /* Send some commands to the IoT 6 click (uBlox Sara-R412M) module */
#if 0 /* one-off LTE module setup */
    /* According to u-blox docs, the module takes up to 5 seconds to power up. */
    sleep_ms(5000); /* NOTE: we cannot waitforltemoduleready here, because
     * the configuration AT+CSGT may not have been executed yet. */
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
    waitforltemoduleready();
    sendatcmd("AT", 4.0);
    // disconnect from network, needed for the following
    sendatcmd("AT+CFUN=0", 10.0);
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
    sendatcmd("AT+CGDCONT=1,\"IP\",\"resiot.m2m\"", 10.0);
    // reboot again to make that take effect
    sendatcmd("AT+CFUN=15", 4.0);
    waitforltemoduleready();
    sendatcmd("AT", 4.0);
    // disconnect from network, needed for the following
    sendatcmd("AT+CFUN=0", 10.0);
    // we would want to enable IPv4+IPv6, but we can't -
    // see comment above AT+CGDCONT. So we enable IPv4 only.
    sendatcmd("AT+UPSD=0,0,0", 10.0);
    // dynamic IP
    sendatcmd("AT+UPSD=0,7,\"0.0.0.0\"", 10.0);
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
#endif /* one-off LTE module setup */
    waitforltemoduleready();
    // Possibly useful for later: The module has a RTC - command AT+CCLK
    // Just send an "AT", so the module can see and set the correct baudrate.\r\n");
    sendatcmd("AT", 4.0);
    // Do not echo back commands.
    sendatcmd("ATE0", 4.0);
    // Tell the module to send "verbose" error messages, even though
    // they really aren't what anybody in his right mind would call
    // verbose...
    sendatcmd("AT+CMEE=2", 4.0);
    // Set in- and output of socket functions to hex-encoded, so we don't need
    // to deal with escaping special characters.
    sendatcmd("AT+UDCONF=1,1", 4.0);
    // Show a bunch of info about the mobile network module
    sendatcmd("ATI", 4.0);
    /* wait for network connection (but with timeout).
     * We don't really care if this succeeds or not, we'll just try to send
     * data anyways. */
    mn_waitfornetworkconn(60.0);
    // select active profile
    sendatcmd("AT+UPSD=0,100,1", 15.0);
    // Query status info
    sendatcmd("AT+CGACT?", 4.0);
    // Lets open a network connection
    int sock = mn_opentcpconn("wetter.poempelfox.de", 80, 30.0);
    if (sock >= 0) {
      char tst[500]; int br;
      strcpy(tst, "GET /getlastvalue/11 HTTP/1.1\r\nHost: wetter.poempelfox.de\r\nConnection: close");
      mn_writesock(sock, tst, strlen(tst), 30.0);
      while ((br = (mn_readsock, tst, sizeof(tst) - 1, 30.0)) > 0) {
        tst[br] = 0;
        printf("Received HTTP reply: %s\r\n", tst);
      }
    }

    /* Main loop */
    do {
      printf("Loopy MC Loop...\r\n");
      sleep_ms(10000);
    } while (1);
    printf("Exited main loop.\r\n");
    
    printf("main() will now return 0...\r\n");
    return 0;
}


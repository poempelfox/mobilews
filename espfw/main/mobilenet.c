#include <driver/uart.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "mobilenet.h"

/* Port / pin definitions for the LTE modem */
#define LTEMUART UART_NUM_1
#define LTEMTX 1
#define LTEMRX 2
#define LTEMRTS 3
#define LTEMCTS 4
/* We also need an I/O to connect to the "power on" pin of the ublox module. */
#define LTEMPOWERPIN 5
/* and another pin to control the relays that can powercycle the ublox
 * module, because that POS keeps locking up with no way to recover it
 * in software. */
#define LTEMRELAYPIN 26

#define LTEMRXQUEUESIZE 1500
#define LTEMTXQUEUESIZE 0  /* 0 means block until everything is written. */

/* If RUNONETIMEMODEMCONFIG is 1, we will send the config commands
 * for the things that the LTE modem module stores in its non-volatile
 * memory, like e.g. the APN name, to the modem on bootup.
 * This obviously needs to be done only once for a new LTE modem module.
 * Do NOT keep this enabled. */
#ifndef RUNONETIMEMODEMCONFIG
#define RUNONETIMEMODEMCONFIG 0
#endif

static const char *TAG = "mobilenet";
#define sleep_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

static char serialinlinebuf[500];
static int serialinlbpos = 0;
static int serialinlblav = 0;
static char queuedcommands[100];
static portMUX_TYPE cmdqueuespinlock = portMUX_INITIALIZER_UNLOCKED;

/* removes all occurences of char b from string a. */
static void delchar(char * a, char b) {
  char * s; char * d;
  s = a; d = a;
  while (*s != '\0') {
    if (*s != b) {
      *d = *s;
      s++; d++;
    } else {
      s++;
    }
  }
  *d = '\0';
}

/* Helper function to clear anything still unread on the serial input before
 * sending a new command. */
static void clearserialinputbuf(void)
{
  /* Clear our buffered input. */
  serialinlbpos = 0;
  serialinlblav = 0;
  /* Clear UART buffer */
  size_t sba;
  if (uart_get_buffered_data_len(LTEMUART, &sba) != ESP_OK) {
    return;
  }
  while (sba > 0) {
    char b[2];
    (void) uart_read_bytes(LTEMUART, b, 1, 1);
    sba--;
  }
}

static void readavailableserialuntillinefeed(void)
{
  if (serialinlblav > 0) { /* Don't read anything if the previous line has */
    return; /* not been fetched yet */
  }
  size_t sba;
  if (uart_get_buffered_data_len(LTEMUART, &sba) != ESP_OK) {
    return;
  }
  while (sba > 0) {
    char b[2];
    int rb = uart_read_bytes(LTEMUART, b, 1, 1);
    if (rb > 0) {
      if ((b[0] != '\0') && (b[0] != '\r')) {
        /* ignore \0 - they occour occasionally when the LTE module goes to
         * sleep or wakes up. Ignore \r because we don't care. */
        serialinlinebuf[serialinlbpos] = b[0];
        if (serialinlbpos < (sizeof(serialinlinebuf) - 2)) {
          // This means we'll just cut off long lines instead of overwriting memory
          serialinlbpos++;
        }
        if (b[0] == '\n') {
          serialinlinebuf[serialinlbpos] = '\0'; // null-terminate the string
          serialinlblav = 1; // We now have a full line
          return;
        }
      }
    }
    sba--;
  }
}

static int seriallineavailable(void)
{
  readavailableserialuntillinefeed();
  return serialinlblav;
}

/* Gets a whole line received over serial.
 * You are only permitted to call this after seriallineavailable() signalled
 * that there is indeed a line waiting to be fetched. */
void getserialline(char * buf, int buflen) {
  if (serialinlblav > 0) { /* There is a line available, lets return it. */
    if (buflen >= 2) {
      strncpy(buf, serialinlinebuf, buflen - 2);
      buf[buflen - 1] = '\0';
    }
    serialinlblav = 0;
    serialinlbpos = 0;
  } else {
    ESP_LOGI(TAG, "Warning: getserialline called, but no line available.\n");
    buf[0] = '\0';
  }
}

/* Read serial line with timeout.
 * This stops reading on a \n, or after a timeout. The \n is NOT in the
 * returned string.
 * Returns: number of bytes read, or <0 on error.
 * valid errors are: -1 timeout hit, -2 buffer too small (not implemented).
 */
int readseriallinewto(char * buf, int buflen, int timeout)
{
  time_t stts = time(NULL);
  do {
    if (seriallineavailable() > 0) {
      getserialline(buf, buflen);
      delchar(buf, '\n');
      return strlen(buf);
    }
    sleep_ms(50);
  } while ((time(NULL) - stts) < timeout);
  /* Timed out. */
  return -1;
}

/* Note that the timeout here is the timeout PER LINE. This may
 * theoretically wait for multiple times the timeout if there is
 * constant output. */
int waitforatreplywto(char * buf, int buflen, int timeout)
{
  int res = 0;
  int lastreadrc = 0;
  char * cptr = &buf[res];
  do {
    lastreadrc = readseriallinewto(cptr, buflen - res - 2, timeout);
    if (lastreadrc > 0) {
      res += lastreadrc;
      /* Is this either "OK" or "ERROR" or "+CME ERROR.*"? Then this
       * is the end of the reply to the last AT command. */
      //ESP_LOGI(TAG, "cptr: %s len %d", cptr, strlen(cptr));
      if ((strcmp(cptr, "OK") == 0)
       || (strcmp(cptr, "ERROR") == 0)
       || (strncmp(cptr, "+CME ERROR", 10) == 0)) {
        return res;
      }
      buf[res] = '\n'; // At this point we're overwriting the terminating \0 with a \n.
      res++;
      cptr = &buf[res];
    }
  } while (lastreadrc >= 0);
  buf[res] = 0;
  return -1;
}

static void uart_write_bytes_wto(uart_port_t uart_num, const char * s, uint32_t l, int timeout)
{
  time_t stts = time(NULL);
  do {
    int bs = uart_tx_chars(uart_num, s, l);
    if (bs >= 0) {
      l -= bs;
      s += bs;
    }
    if (l > 0) { /* Buffer was not completely written, delay a short time to give it time to flush */
      sleep_ms(50);
    }
  } while ((l > 0) && ((time(NULL) - stts) < timeout));
  if (l > 0) {
    ESP_LOGE(TAG, "Serial port lost %lu bytes of data on write due to TX overrun/timeout!", l);
  }
}

/* Sends data out the serial port to the LTE modem.
 * Linefeeds already need to be included in line! */
void sendserialline(char * line)
{
  uart_write_bytes_wto(LTEMUART, line, strlen(line), 5);
}

/* sends an AT command to the LTE module, and waits for it to return a reply,
 * whether it be an OK or an ERROR. With timeout.
 * Returns <0 if there is an error, but note that error does not mean the
 * string "ERROR" was returned, it means there was no or no valid reply.
 * This function is obviously NOT useful if you care about the reply at
 * all, because you won't see it. It's mostly useful for firing off
 * initialization sequences. */
int sendatcmd(char * cmd, int timeout)
{
  char rcvbuf[350];
  clearserialinputbuf();
  sprintf(rcvbuf, "%s\r\n", cmd);
  sendserialline(rcvbuf);
  int res = waitforatreplywto(&rcvbuf[0], sizeof(rcvbuf), timeout);
  ESP_LOGI(TAG, "sendatcmd: Sent '%s', Received serial: error=%s, Text '%s'", cmd, ((res < 0) ? "Yes" : "No"), rcvbuf);
  return res;
}

void mn_wakeltemodule(void)
{
  /* Note: The PWR pin is inverted on the click board - so we need to pull
   * it high for enabling it even though it's low active according to
   * R412M documentation. */
  /* The data sheet says that this needs to be pulled for 0.15 to 3.2 seconds.
   * Which of course directly contradicts the next line, where it says that
   * more than 1.5 seconds will turn it off. */
  gpio_set_level(LTEMPOWERPIN, 1);
  sleep_ms(1000);
  gpio_set_level(LTEMPOWERPIN, 0);
}

/* Waits for a sign that the LTE module is ready.
 * There are two signs that we accept: Either the module sends its configured
 * powerup-message, or it sends an "OK", which we consider to be a reply to the
 * 'AT' command we try to send on entering this function (if the hardware FIFO
 * has space for it, if it hasn't, then output has probably been blocked for a
 * long time already and everything is broken anyways). */
int mn_waitforltemoduleready(void)
{
  char buf[250];
  int res;
  /* Clear input buffer before we start */
  clearserialinputbuf();
  if (uart_tx_chars(LTEMUART, "AT\r\n", 4) != 4) {
    ESP_LOGI(TAG, "waitforltemoduleready: not enough TX buffer space for AT command.");
  }
  do {
    /* Timeout is 6 because specification says it may need at most 5 seconds
     * to power up after waking it via power pin aka mn_wakeltemodule(). */
    res = readseriallinewto(buf, sizeof(buf), 6);
    if (res >= 19) {
      if (strncmp(buf, "LTEmodule now ready", 19) == 0) {
        ESP_LOGI(TAG, "LTEmodule reported ready.");
        return 0;
      }
    }
    if (res == 2) {
      if (strncmp(buf, "OK", 2) == 0) {
        ESP_LOGI(TAG, "LTEmodule did not report ready but returned 'OK'");
        return 0;
      }
    }
  } while (res >= 0);
  ESP_LOGE(TAG, "Timeout waiting for LTEmodule to report ready.");
  return 1;
}

/* Return the network state, as returned in AT+COPS.
 * So 3 = GPRS, 7 = LTE-M?, 9 = NB-IOT?
 * -2 if AT+COPS could not be read. -1 if not connected. */
int readnetworkstate(void)
{
  char rcvbuf[200];
  clearserialinputbuf();
  sprintf(rcvbuf, "%s\r\n", "AT+COPS?");
  sendserialline(rcvbuf);
  int res = waitforatreplywto(&rcvbuf[0], sizeof(rcvbuf), 2);
  if (res <= 0) { return -2; }
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
void mn_waitfornetworkconn(int timeout)
{
  int nws;
  time_t stts = time(NULL);
  do {
    nws = readnetworkstate();
    ESP_LOGI(TAG, "network state: %d\n", nws);
    if (nws <= 0) {
      sleep_ms(500);
    } else {
      /* we have a valid network connection. */
      if (nws == 3) { /* GPRS */
        /* GPRS requires manual context activation, LTE does it automatically */
        ESP_LOGI(TAG, "GPRS connection detected, sending 'AT+CGACT=1,1'...");
        sendatcmd("AT+CGACT=1,1", timeout);
      }
      return;
    }
  } while ((time(NULL) - stts) < timeout);
}

/* Tries to wait until the mobile network connection has gotten an IP
 * address, with a timeout. */
void mn_waitforipaddr(int timeout)
{
  char buf[200]; char ipbuf[34];
  char * obuf = &ipbuf[0];
  int obufsize = sizeof(ipbuf);
  time_t stts = time(NULL);
  clearserialinputbuf();
  do {
    sprintf(buf, "AT+CGPADDR=1\r\n");
    sendserialline(buf);
    int res = waitforatreplywto(buf, sizeof(buf), 4);
    if (res > 0) {
      char * sp1;
      char * st1 = strtok_r(buf, "\n", &sp1);
      do {
        ESP_LOGI(TAG, "CGPADDRrb: '%s'", st1);
        if (strncmp(st1, "+CGPADDR: 1,", 12) == 0) {
          /* This is the line containing the answer */
          /* Documentation says the IP is enclosed in quotes, reality says it
           * is not - so we better handle both cases. */
          int bufpos = 12;
          if (st1[bufpos] == '"') { bufpos++; }
          while ((st1[bufpos] != 0) && (obufsize > 1)) {
            if (st1[bufpos] == '"') { /* The " marks the end of the IP */
              break;
            }
            /* Copy one char and advance to next */
            *obuf = st1[bufpos];
            obuf++;
            obufsize--;
            bufpos++;
          }
          *obuf = 0;
          if (strcmp(ipbuf, "0.0.0.0") == 0) { /* Not really an IP. */
            /* Reset parse buffer and try again. */
            obuf = &ipbuf[0]; obufsize = sizeof(ipbuf);
            break;
          }
          ESP_LOGI(TAG, "mobile network got an IP address: %s", ipbuf);
          return;
        }
        st1 = strtok_r(NULL, "\n", &sp1);
      } while (st1 != NULL);
    }
  } while ((time(NULL) - stts) < timeout);
}

/* Known problems:
 * Sometimes the LTE module seems to just reply "OK" - without returning the
 * +UDNSRN-reply. In this case, just retry. */
void mn_resolvedns(char * hostname, char * obuf, int obufsize, int timeout)
{
  char buf[250]; char * opp = obuf;
  clearserialinputbuf();
  sprintf(buf, "AT+UDNSRN=0,\"%s\"\r\n", hostname);
  sendserialline(buf);
  int res = waitforatreplywto(buf, sizeof(buf), timeout);
  if (res <= 0) { /* No success or failure within timeout */
    ESP_LOGE(TAG, "mn_resolvedns: Timeout waiting for DNS reply");
    strcpy(obuf, "");
    return;
  }
  char * sp1;
  char * st1 = strtok_r(buf, "\n", &sp1);
  do {
    //ESP_LOGI(TAG, "DNSrb: '%s'", st1);
    if (strncmp(st1, "+UDNSRN: \"", 10) == 0) {
      /* This is the line containing the answer */
      int bufpos = 10;
      while ((st1[bufpos] != 0) && (obufsize > 1)) {
        if (st1[bufpos] == '"') { /* The " marks the end of the IP */
          break;
        }
        /* Copy one char and advance to next */
        *opp = st1[bufpos];
        opp++;
        obufsize--;
        bufpos++;
      }
      *opp = 0;
      ESP_LOGI(TAG, "Resolved '%s' to '%s'", hostname, obuf);
      return;
    }
    st1 = strtok_r(NULL, "\n", &sp1);
  } while (st1 != NULL);
  /* No DNS reply found in output. Return empty string to signal the error. */
  ESP_LOGE(TAG, "mn_resolvedns: No DNS reply received.");
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
  char buf[80];
  sprintf(buf, "AT+USOCL=%d,1", socketnr);
  sendatcmd(buf, 61);
}

/* Opens a TCP connection to hostname on port.
 * returns a socket number (should be between 0 and 6 because the module can
 * only create 7 sockets at a time), or <0 on error. */
int mn_opentcpconn(char * hostname, uint16_t port, int timeout)
{
  char buf[250];
  char ipasstring[42];
  int socket = -1;
  mn_resolvedns(hostname, ipasstring, sizeof(ipasstring), timeout);
  if (strlen(ipasstring) < 4) { /* DNS resolution failed. Try again. */
    mn_resolvedns(hostname, ipasstring, sizeof(ipasstring), timeout);
    if (strlen(ipasstring) < 4) { /* DNS resolution failed */
      return -1;
    }
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
 * Returns 0 on success.
 */
int mn_writesock(int socket, char * buf, int bufsize, int timeout)
{
  if (socket < 0) { return -1; }
  if (bufsize <= 0) { return -2; }
  while (bufsize > 0) {
    int bts = bufsize;
    char wrstr[400];
    if (bts > 128) { bts = 128; /* Write at most 128 bytes at once. */ }
    sprintf(wrstr, "AT+USOWR=%d,%d,\"", socket, bts);
    for (int i = 0; i < bts; i++) {
      unsigned char prch[3]; unsigned int nib;
      nib = ((unsigned char)(*buf) >> 4) & 0x0f;
      if (nib <= 9) {
        prch[0] = '0' + nib;
      } else {
        prch[0] = 'a' - 10 + nib;
      }
      nib = ((unsigned char)(*buf) >> 0) & 0x0f;
      if (nib <= 9) {
        prch[1] = '0' + nib;
      } else {
        prch[1] = 'a' - 10 + nib;
      }
      prch[2] = 0;
      strcat(wrstr, (char *)prch);
      buf++; /* Advance to next position in buffer */
    }
    strcat(wrstr, "\"\r\n");
    sendserialline(wrstr);
    bufsize -= bts; /* Update size of remaining buffer */
    int res = waitforatreplywto(&wrstr[0], sizeof(wrstr), timeout);
    if (res <= 0) {
      ESP_LOGE(TAG, "mn_writesock: aborting because we received no reply from the modem before timeout.");
      return -3;
    } else {
      wrstr[res] = 0;
      char * sp1;
      char * st1 = strtok_r(wrstr, "\n", &sp1);
      int hadok = 0;
      do {
        if (strncmp(st1, "OK", 2) == 0) { // We got an "OK"
          hadok = 1;
          /* Do not 'break;', we need to output the original string later, and
           * that requires iterating through strtok until the end. */
        }
        st1 = strtok_r(NULL, "\n", &sp1);
      } while (st1 != NULL);
      if (hadok != 1) {
        ESP_LOGE(TAG, "mn_writesock: aborting because we received no 'OK' reply from the modem: '%s'.", wrstr);
        return -4;
      }
    }
  }
  return 0;
}

/* Attempts to read data from a network socket.
 * Returns: Number of bytes actually read.
 */
int mn_readsock(int socket, char * buf, int bufsize, int timeout)
{
  int res = 0;
  if (socket < 0) { return res; }
  while (bufsize > 0) {
    int btr = bufsize;
    char rdstr[400];
    if (btr > 128) { btr=128; /* Read at most 128 bytes at once. */ }
    sprintf(rdstr, "AT+USORD=%d,%d\r\n", socket, btr);
    //ESP_LOGI(TAG, "mn_readsock: Sending: %s", rdstr);
    sendserialline(rdstr);
    int rc = waitforatreplywto(rdstr, sizeof(rdstr), timeout);
    if (rc <= 0) break;
    //ESP_LOGI(TAG, "mn_readsock: got: %s", rdstr);
    char * sp1;
    char * st1 = strtok_r(rdstr, "\n", &sp1);
    int riti = 0; /* # bytes read in this iteration */
    do {
      //ESP_LOGI(TAG, "mn_readsock: now parsing: '%s'", st1);
      if (strncmp(st1, "+USORD: ", 8) == 0) {
        /* This contains the read output. */
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
            rp += 2;
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
    if (riti == 0) { /* Nothing read in the whole loop. Abort reading. */
      return res;
    }
  }
  return res;
}

void mn_getmninfo(char * obuf)
{
  char rdstr[500]; int rc;
  strcpy(obuf, "");
  sprintf(rdstr, "AT+COPS?\r\n");
  strcat(obuf, "# AT+COPS?\n");
  sendserialline(rdstr);
  rc = waitforatreplywto(rdstr, sizeof(rdstr), 2);
  if (rc > 0) {
    strcat(obuf, rdstr);
    ESP_LOGI(TAG, "mn_getmninfo: %s", rdstr);
  } else {
    strcat(obuf, "(no reply received)");
    ESP_LOGI(TAG, "mn_getmninfo: AT+COPS? failed.");
  }
  sprintf(rdstr, "AT+CESQ\r\n");
  strcat(obuf, "# AT+CESQ\n");
  sendserialline(rdstr);
  rc = waitforatreplywto(rdstr, sizeof(rdstr), 2);
  if (rc > 0) {
    strcat(obuf, rdstr);
    ESP_LOGI(TAG, "mn_getmninfo: %s", rdstr);
  } else {
    strcat(obuf, "(no reply received)");
    ESP_LOGI(TAG, "mn_getmninfo: AT+CESQ failed.");
  }
}

void mn_powercycleltemodem(void)
{
  /* Configure the pin controlling the relays for the LTE modem power. */
  gpio_config_t ltemrelaypingpioconf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT, /* output mode, start driving this pin */
    .pin_bit_mask = (1ULL << LTEMRELAYPIN),
    .pull_down_en = 0,
    .pull_up_en = 0,
  };
  ESP_ERROR_CHECK(gpio_config(&ltemrelaypingpioconf));
  ESP_ERROR_CHECK(gpio_set_level(LTEMRELAYPIN, 0));
  /* Keep this for 5 seconds */
  sleep_ms(5000);
  ESP_ERROR_CHECK(gpio_set_level(LTEMRELAYPIN, 1));
  /* give it 1/3rd of a second to go 'high' */
  sleep_ms(300);
  /* Now stop driving that pin. */
  ltemrelaypingpioconf.mode = GPIO_MODE_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&ltemrelaypingpioconf));
}

void mn_init(void)
{
  /* Let's get serial */
  uart_config_t uart_config = { /* 115200 8n1 with HW Flow Control */
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
    .rx_flow_ctrl_thresh = 122,
    .source_clk = UART_SCLK_DEFAULT, // needed as of ESP-IDF 5.0, but not documented yet.
  };
  /* Configure UART parameters */
  ESP_ERROR_CHECK(uart_param_config(LTEMUART, &uart_config));
  /* Set which pins to use for the UART. */
  ESP_ERROR_CHECK(uart_set_pin(LTEMUART, LTEMTX, LTEMRX, LTEMRTS, LTEMCTS));
  /* Install the UART driver. We do not use an eventqueue. */
  ESP_ERROR_CHECK(uart_driver_install(LTEMUART, LTEMRXQUEUESIZE, LTEMTXQUEUESIZE, 10, NULL, 0));
  /* Configure the Power Pin for the LTE modem. */
  gpio_config_t ltempowerpingpioconf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = (1ULL << LTEMPOWERPIN),
    .pull_down_en = 0,
    .pull_up_en = 0,
  };
  ESP_ERROR_CHECK(gpio_config(&ltempowerpingpioconf));
  /* Configure the pin controlling the relays for the LTE modem power. */
  gpio_config_t ltemrelaypingpioconf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_DISABLE, /* For now we disable this - don't drive or read it, just let it float. */
    .pin_bit_mask = (1ULL << LTEMRELAYPIN),
    .pull_down_en = 0,
    .pull_up_en = 0,
  };
  ESP_ERROR_CHECK(gpio_config(&ltemrelaypingpioconf));
}

void mn_configureltemodule(void)
{
#if (RUNONETIMEMODEMCONFIG == 1) /* one-off LTE module setup */
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
  mn_waitforltemoduleready();
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
  mn_waitforltemoduleready();
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
  // documentation raises more questions than it answers, but
  // it probably does not hurt.
  //sendatcmd("AT+USIMSTAT=4", 4);
  //sendatcmd("AT+UCUSATA=4", 4);
  /* UNTESTED and badly documented territory: Configure powersaving.
   * The only readable documentation I found was on
   * https://www.twilio.com/docs/iot/supersim/low-power-optimization-for-cellular-modules
   * everything ublox provided was useless.
   * There are two power saving mechanisms that are independant from each other:
   * PSM and EDRX. The former has higher latency but can save more power. Both
   * can be combined. And in both cases, you can request things from the network,
   * but it's always the networks decision if or what values it allows. */
  sendatcmd("AT+CPSMS?", 10); // print the old settings before we change them
  /* Note: Actual reply to that from our module with our SIM:
   * +CPSMS:0,"01100000","00000000",,"00000000" */
  /* par. 2: requested periodic RAU - default "00011000" == 4 hours
   * par. 3: requested GPRS READY Timer - default "00001010" == 20 seconds.
   * par. 4: requested periodic TAU - default undocumented, possibly "00010011" == 11400s
   * par. 5: requested active time - default undocumented, possibly "00000011" == 6s
   * We should probably increase the GPRS READY timer so we don't have to do
   * a full reconnect every minute. 00100010 == 2 minutes. */
  sendatcmd("AT+CPSMS=1,\"00011000\",\"00100010\",\"00010011\",\"00000011\"", 10);
  sendatcmd("AT+CEDRXS?", 10); // print the old settings before we change them
  /* Note: Actual reply to that from our module with our SIM:
   * +CEDRXS: 2,"0010"
   * +CEDRXS: 4,"0010"
   * +CEDRXS: 5,"0010" */
  sendatcmd("AT+CEDRXS=1,2,\"0101\"", 10); // 0101=81.92s, 0011=40.96s
  sendatcmd("AT+CEDRXS=1,4,\"0101\"", 10);
  sendatcmd("AT+CEDRXS=1,5,\"0101\"", 10);
  // reboot again to make that take effect
  sendatcmd("AT+CFUN=15", 4);
#endif /* (RUNONETIMEMODEMCONFIG == 1) - one-off LTE module setup */
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
  sendatcmd("AT+UPSD=0,100,1", 61);
}

void mn_repeatcfgcmds(void)
{
  // Do not echo back commands.
  sendatcmd("ATE0", 4);
  // "verbose" error messages
  sendatcmd("AT+CMEE=2", 4);
  // Set in- and output of socket functions to hex-encoded
  sendatcmd("AT+UDCONF=1,1", 4);
  // select active profile
  sendatcmd("AT+UPSD=0,100,1", 61);
}

void mn_rebootltemodule(void)
{
  /* MT silent reset with detach from network, saving of NVM parameters,
   * and reset of SIM card. */
  sendatcmd("AT+CFUN=16", 4);
}

int mn_queuecommand(char * cmd)
{
  int res = 0;
  taskENTER_CRITICAL(&cmdqueuespinlock);
  /* Do NOT call any of the ESP_LOG functions while holding the lock! */
  if ((strlen(cmd) + strlen(queuedcommands) + 1) < sizeof(queuedcommands)) {
    strcat(queuedcommands, cmd);
    ESP_LOGI(TAG, "Successfully queued command '%s' for sending to LTE module.", cmd);
  } else {
    ESP_LOGI(TAG, "Could not queue command '%s' because buffer does not have enough space.", cmd);
    res = 1;
  }
  taskEXIT_CRITICAL(&cmdqueuespinlock);
  return res;
}

void mn_sendqueuedcommands(void)
{
  taskENTER_CRITICAL(&cmdqueuespinlock);
  if (strlen(queuedcommands) > 0) {
    ESP_LOGI(TAG, "Sending queued command(s): '%s'", queuedcommands);
    sendserialline(queuedcommands);
    strcpy(queuedcommands, "");
  }
  taskEXIT_CRITICAL(&cmdqueuespinlock);
}

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <driver/uart.h>
#include <driver/gpio.h>
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

#define LTEMRXQUEUESIZE 1500
#define LTEMTXQUEUESIZE 0  /* 0 means block until everything is written. */

static const char *TAG = "mobilenet";
#define sleep_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

static char serialinlinebuf[500];
static int serialinlbpos = 0;
static int serialinlblav = 0;

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
      buf[res] = '\n';
      res++;
      cptr = &buf[res];
    }
  } while (lastreadrc >= 0);
  buf[res] = 0;
  return -1;
}

/* Sends data out the serial port to the LTE modem.
 * Linefeeds already need to be included in line! */
void sendserialline(char * line)
{
  uart_write_bytes(LTEMUART, line, strlen(line));
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
  sprintf(rcvbuf, "%s\r\n", cmd);
  sendserialline(rcvbuf);
  int res = waitforatreplywto(&rcvbuf[0], sizeof(rcvbuf), timeout);
  ESP_LOGI(TAG, "sendatcmd: Sent '%s', Received serial: error=%s, Text '%s'\n", cmd, ((res < 0) ? "Yes" : "No"), rcvbuf);
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

/* Waits for the LTE module ready message on the serial port. */
void mn_waitforltemoduleready(void)
{
  char buf[250];
  int res;
  do {
    /* Timeout is 7 because specification says it may need at most 5 seconds
     * to come up. */
    res = readseriallinewto(buf, sizeof(buf), 7);
    if (res >= 19) {
      if (strncmp(buf, "LTEmodule now ready", 19) == 0) {
        ESP_LOGI(TAG, "LTEmodule reported ready.");
        return;
      }
    }
  } while (res >= 0);
  ESP_LOGE(TAG, "Timeout waiting for LTEmodule to report ready.");
}

/* Return the network state, as returned in AT+COPS.
 * So 3 = GPRS, 7 = LTE-M?, 9 = NB-IOT?
 * -2 if AT+COPS could not be read. -1 if not connected. */
int readnetworkstate(void)
{
  char rcvbuf[200];
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

void mn_resolvedns(char * hostname, char * obuf, int obufsize, int timeout)
{
  char buf[250];
  sprintf(buf, "AT+UDNSRN=0,\"%s\"\r\n", hostname);
  sendserialline(buf);
  int res = waitforatreplywto(buf, sizeof(buf), timeout);
  if (res <= 0) { /* No success or failure within timeout */
    ESP_LOGE(TAG, "Timeout waiting for DNS reply");
    strcpy(obuf, "");
    return;
  }
  char * sp1;
  char * st1 = strtok_r(buf, "\n", &sp1);
  do {
    ESP_LOGI(TAG, "DNSrb: '%s'", st1);
    if (strncmp(st1, "+UDNSRN: \"", 10) == 0) {
      /* This is the line containing the answer */
      int bufpos = 10;
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
  char buf[80];
  sprintf(buf, "AT+USOCL=%d,1\r\n", socketnr);
  sendserialline(buf);
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
 * Errors are just silently ignored.
 */
void mn_writesock(int socket, char * buf, int bufsize, int timeout)
{
  if (socket < 0) { return; }
  if (bufsize <= 0) { return; }
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
    strcat(wrstr, "\"");
    sendatcmd(wrstr, timeout);
    bufsize -= bts; /* Update size of remaining buffer */
  }
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
    ESP_LOGI(TAG, "mn_readsock: Sending: %s", rdstr);
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
}

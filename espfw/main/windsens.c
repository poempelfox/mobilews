/* Functions for talking to our wind sensors:
 * DFROBOT SEN0483 Wind speed sensor
 * DFROBOT SEN0482 Wind direction sensor
 * Both are connected via RS485 */

#include <driver/gpio.h>
#include <esp_log.h>
#include <time.h>
#include "windsens.h"
#include "wk2132.h"

static uint8_t windsensport;

/* Which pin is connected to the direction switching (TX vs RX)
 * pins on the Olimex MOD-RS485 (RS485 is half-duplex!)
 * That pin is pulled high for transmission, and set to 0 to
 * receive. */
#define RS485DIRSWITCHPIN 13

/* NOTE: The factory default setting for both the wind direction
 * and the wind speed sensor unfortunately is 0x02, meaning they
 * cannot coexist on the RS485-Modbus in factory config. At least
 * one of them will need to be reprogrammed before they can be
 * used. See FIXME */

/* What modbus-address does the wind direction sensor use? */
#define WDAD 0x02

static void switchtoRX(void)
{
  /* Set to input */
  gpio_set_level(RS485DIRSWITCHPIN, 0);
}

static void switchtoTX(void)
{
  /* Set to output */
  gpio_set_level(RS485DIRSWITCHPIN, 1);
}

/* This seems to be the default modbus crc calculation,
 * we mostly copied this from dfrobots example code. */
static uint16_t crc16_mb(uint8_t * buf, int len)
{
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  /* now swap byte order */
  crc = ((crc & 0x00ff) << 8) | ((crc & 0xff00) >> 8);
  return crc;
}

void windsens_init(uint8_t wsp)
{
  windsensport = wsp;
  gpio_config_t diswpi = {
    .pin_bit_mask = (1ULL << RS485DIRSWITCHPIN),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = 0,
    .pull_down_en = 0,
    .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&diswpi));
  switchtoRX();
  wk2132_serialportinit(windsensport, 9600); /* Wind sensors run at 9600 baud */
}

float windsens_getwinddir(void)
{
  /*                 Addr  Func  RegisterAd  Length____  CRC_______ */
  uint8_t wsq[8] = { WDAD, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
  uint16_t crc = crc16_mb(wsq, 6);
  wsq[6] = (crc >> 8);   /* MSB */
  wsq[7] = (crc & 0xff); /* LSB */
  uint8_t rcvbuf[10];
  /* First clear anything that is still in the input buffer */
  while (wk2132_get_available_to_read(windsensport) > 0) {
    wk2132_read_serial(windsensport, (char *)rcvbuf, 1);
  }
  switchtoTX();
  wk2132_write_serial(windsensport, (const char *)wsq, sizeof(wsq));
  wk2132_flush(windsensport);
  switchtoRX();
  /* We expect a reply of exactly 7 bytes, so wait for that with timeout */
  time_t stts = time(NULL);
  int bav;
  do {
    vTaskDelay(pdMS_TO_TICKS(333));
    bav = wk2132_get_available_to_read(windsensport);
  } while ((bav < 7) && ((time(NULL) - stts) < 3));
  if (bav == 7) {
    wk2132_read_serial(windsensport, (char *)rcvbuf, 7);
    /* check received data */
    uint16_t crc = crc16_mb(rcvbuf, 5);
    if ((rcvbuf[0] != WDAD) || (rcvbuf[1] != 0x03)
     || (rcvbuf[2] != 0x02)
     || ((crc >> 8) != rcvbuf[5]) || ((crc & 0xff) != rcvbuf[6])) {
      ESP_LOGE("windsens.c", "Invalid reply from wind-direction-sensor received:");
      ESP_LOGE("windsens.c", " `- %02x %02x %02x %02x %02x %02x %02x",
                           rcvbuf[0], rcvbuf[1], rcvbuf[2], rcvbuf[3],
                           rcvbuf[4], rcvbuf[5], rcvbuf[6]);
      ESP_LOGE("windsens.c", " `- calculated CRC: %04x", crc);
      return -1;
    }
    int calcdir = (rcvbuf[3] * 256) + rcvbuf[4];
    if (calcdir <= 3600) {
      return ((float)calcdir / 10.0);
    }
  }
  ESP_LOGE("windsens.c", "No (valid) reply received from wind-direction-sensor (%d bytes)", bav);
  return -1.0;
}


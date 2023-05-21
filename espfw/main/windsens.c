/* Functions for talking to our wind sensors:
 * DFROBOT SEN0483 Wind speed sensor
 * DFROBOT SEN0482 (V2) Wind direction sensor
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
 * used together. See windsens_init() for one ugly way to do that. */

/* What modbus-address does the wind direction sensor use? */
#define WDAD 0x23

/* What modbus-address does the wind speed sensor use? */
#define WSAD 0x02

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
#if 0
  /* This bit here reprograms everything that is currently on the modbus to
   * a different address. For very obvious reasons, this should NOT normally
   * be compiled in. You need to
   * - make absolutely sure there is ONLY ONE device connected on the modbus
   * - set the "#if 0" above to 1, recompile and reflash
   * - wait till it says it is done
   * - set the "#if 1" back to 0, recompile and reflash
   * - power-cycle the sensor.
   */
  ESP_LOGI("wk2132.c", "=================================================================");
  ESP_LOGI("wk2132.c", "= REPROGRAMMING MODBUS ADDRESS - THIS IS NOT A NORMAL FIRMWARE  =");
  ESP_LOGI("wk2132.c", "=================================================================");
  uint8_t newad = 0x23;
  uint8_t reprogrammsg[] = { 0x00,        /* Slave Address, 0 == broadcast to all devices */
                             0x10,        /* Function code: write multiple registers */
                             0x10, 0x00,  /* Register start address: 0x1000 (which contains
                                           * the slave address) */
                             0x00, 0x01,  /* Length of the write (1x 16 bit) */
                             0x02,        /* Number of bytes */
                             0x00, newad, /* The new value to be written - new address in LSB */
                             0x00, 0x00   /* the CRC (will be filled below) */
                           };
  uint16_t crc = crc16_mb(reprogrammsg, 9);
  reprogrammsg[9] = crc >> 8;
  reprogrammsg[10] = crc & 0xff;
  vTaskDelay(pdMS_TO_TICKS(1333)); /* The bus needs to have been idle for a while before we can send */
  switchtoTX();
  wk2132_write_serial(windsensport, (const char *)reprogrammsg, sizeof(reprogrammsg));
  wk2132_flush(windsensport);
  switchtoRX();
  int bav;
  time_t stts = time(NULL);
  do {
    vTaskDelay(pdMS_TO_TICKS(333));
    bav = wk2132_get_available_to_read(windsensport);
  } while ((bav < 7) && ((time(NULL) - stts) < 10));
  if (bav >= 7) {
    uint8_t rbuf[bav];
    wk2132_read_serial(windsensport, (char *)rbuf, bav);
    ESP_LOGI("wk2132.c", "Received %u bytes:", bav);
    for (int i = 0; i < bav; i++) {
      ESP_LOGI("wk2132.c", " 0x%02x", rbuf[i]);
    }
  }
  ESP_LOGI("wk2132.c", "=== END OF MODBUS ADDRESS REPROGRAMMING ===");
  ESP_LOGI("wk2132.c", "This will now go into an endless loop. You need to");
  ESP_LOGI("wk2132.c", "flash proper firmware to the ESP32 again, and then");
  ESP_LOGI("wk2132.c", "also powercycle the sensor whose address was just changed.");
  for (int i = 0; i < 999999; i++) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
#endif
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


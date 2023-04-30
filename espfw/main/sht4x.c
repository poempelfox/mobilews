/* Talking to SHT4x (SHT40, SHT41, SHT45) temperature / humidity sensors */

#include "esp_log.h"
#include "sht4x.h"
#include "sdkconfig.h"


#define SHT4XADDR 0x44

/* Measurement with high precision */
#define SHT4X_CMD_MEASURE_HIGH 0xFD
/* Turn on heater with medium power (110 mW) for 1 second */
#define SHT4X_CMD_HEAT_MID_LONG 0x2F

#define I2C_MASTER_TIMEOUT_MS 1000  /* Timeout for I2C communication */

static i2c_port_t sht4xi2cport;

void sht4x_init(i2c_port_t port)
{
    sht4xi2cport = port;

    /* The default power-on-config of the sensor should
     * be perfectly fine for us, so there is nothing to
     * configure here. */
}

void sht4x_startmeas(void)
{
    uint8_t cmd[1] = { SHT4X_CMD_MEASURE_HIGH };
    i2c_master_write_to_device(sht4xi2cport, SHT4XADDR,
                               cmd, sizeof(cmd),
                               pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    /* We ignore the return value. If that failed, we'll notice
     * soon enough, namely when we try to read the result... */
}

/* This function is based on Sensirons example code and datasheet
 * for the SHT3x and was written for that. CRC-calculation is
 * exactly the same for the SHT4x, so we reuse it. */
static uint8_t sht4x_crc(uint8_t b1, uint8_t b2)
{
    uint8_t crc = 0xff; /* Start value */
    uint8_t b;
    crc ^= b1;
    for (b = 0; b < 8; b++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x131;
      } else {
        crc = crc << 1;
      }
    }
    crc ^= b2;
    for (b = 0; b < 8; b++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x131;
      } else {
        crc = crc << 1;
      }
    }
    return crc;
}

void sht4x_read(struct sht4xdata * d)
{
    uint8_t readbuf[6];
    d->valid = 0; d->tempraw = 0xffff;  d->humraw = 0xffff;
    d->temp = -999.99; d->hum = 200.0;
    int res = i2c_master_read_from_device(sht4xi2cport, SHT4XADDR,
                                          readbuf, sizeof(readbuf),
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    if (res != ESP_OK) {
      ESP_LOGI("sht4x.c", "ERROR: I2C-read from SHT4x failed.");
      return;
    }
    /* Check CRC */
    if (sht4x_crc(readbuf[0], readbuf[1]) != readbuf[2]) {
      ESP_LOGI("sht4x.c", "ERROR: CRC-check for read part 1 failed.");
      return;
    }
    if (sht4x_crc(readbuf[3], readbuf[4]) != readbuf[5]) {
      ESP_LOGI("sht4x.c", "ERROR: CRC-check for read part 2 failed.");
      return;
    }
    /* OK, CRC matches, this is looking good. */
    d->tempraw = (readbuf[0] << 8) | readbuf[1];
    d->humraw = (readbuf[3] << 8) | readbuf[4];
    d->temp = -45.0 + 175.0 * ((float)d->tempraw / 65535.0);
    d->hum = -6.0 + 125.0 * ((float)d->humraw / 65535.0);
    /* Cap values to 0-100 range - the sensor may return values
     * that are slightly outside that range */
    if (d->hum < 0.0) { d->hum = 0.0; }
    if (d->hum > 100.0) { d->hum = 100.0; }
    /* Mark the result as valid. */
    d->valid = 1;
}

void sht4x_heatercycle(void)
{
    uint8_t cmd[1] = { SHT4X_CMD_HEAT_MID_LONG };
    i2c_master_write_to_device(sht4xi2cport, SHT4XADDR,
                               cmd, sizeof(cmd),
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}


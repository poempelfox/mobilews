/* Talking to SEN50 particulate matter sensors */

#include "esp_log.h"
#include "sen50.h"
#include "sdkconfig.h"


#define SEN50ADDR 0x69

#define I2C_MASTER_TIMEOUT_MS 100  /* Timeout for I2C communication */

static i2c_port_t sen50i2cport;

void sen50_init(i2c_port_t port)
{
    sen50i2cport = port;

    /* The default power-on-config of the sensor should
     * be perfectly fine for us, so there is nothing to
     * configure here. */
}

void sen50_startmeas(void)
{
    uint8_t cmd[2] = { 0x00, 0x21 };
    i2c_master_write_to_device(sen50i2cport, SEN50ADDR,
                               cmd, sizeof(cmd),
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    /* We ignore the return value. If that failed, we'll notice
     * soon enough, namely when we try to read the result... */
}

void sen50_stopmeas(void)
{
    uint8_t cmd[2] = { 0x01, 0x04 };
    i2c_master_write_to_device(sen50i2cport, SEN50ADDR,
                               cmd, sizeof(cmd),
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    /* We ignore the return value. If that failed, we'll notice
     * soon enough, namely when we try to read the result... */
}

/* This function is based on Sensirons example code and datasheet
 * for the SHT3x and was written for that. CRC-calculation is
 * exactly the same for the SEN50, so we reuse it. */
static uint8_t sen50_crc(uint8_t b1, uint8_t b2)
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

void sen50_read(struct sen50data * d)
{
    uint8_t readbuf[23];
    uint8_t cmd[2] = { 0x03, 0xc4 };
    i2c_master_write_to_device(sen50i2cport, SEN50ADDR,
                               cmd, sizeof(cmd),
                               I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    d->valid = 0;
    d->pm010raw = 0xffff;  d->pm025raw = 0xffff; d->pm040raw = 0xffff; d->pm100raw = 0xffff;
    d->pm010 = -999.99; d->pm025 = -999.9; d->pm040 = -999.99; d->pm100 = -999.9;
    /* Datasheet says we need to give the sensor at least 20 ms time before
     * we can read the data so that it can fill its internal buffers */
    vTaskDelay(pdMS_TO_TICKS(22));
    int res = i2c_master_read_from_device(sen50i2cport, SEN50ADDR,
                                          readbuf, sizeof(readbuf),
                                          I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (res != ESP_OK) {
      ESP_LOGE("sen50.c", "ERROR: I2C-read from SEN50 failed.");
      return;
    }
    /* Check CRC */
    if (sen50_crc(readbuf[0], readbuf[1]) != readbuf[2]) {
      ESP_LOGE("sen50.c", "ERROR: CRC-check for read part 1 failed.");
      return;
    }
    if (sen50_crc(readbuf[3], readbuf[4]) != readbuf[5]) {
      ESP_LOGE("sen50.c", "ERROR: CRC-check for read part 2 failed.");
      return;
    }
    if (sen50_crc(readbuf[6], readbuf[7]) != readbuf[8]) {
      ESP_LOGE("sen50.c", "ERROR: CRC-check for read part 3 failed.");
      return;
    }
    if (sen50_crc(readbuf[9], readbuf[10]) != readbuf[11]) {
      ESP_LOGE("sen50.c", "ERROR: CRC-check for read part 4 failed.");
      return;
    }
    /* We could also check CRC for temperature / humidity / noxi data, but
     * that doesn't contain valid values anyways (_should_ only contain
     * 0xffff on our sensor because it doesn't measure those) and we
     * throw that away anyways, so why should we care? */
    /* OK, CRC matches, this is looking good. */
    d->pm010raw = (readbuf[0] << 8) | readbuf[1];
    d->pm025raw = (readbuf[3] << 8) | readbuf[4];
    d->pm040raw = (readbuf[6] << 8) | readbuf[7];
    d->pm100raw = (readbuf[9] << 8) | readbuf[10];
    d->pm010 = ((float)d->pm010raw / 10.0);
    d->pm025 = ((float)d->pm025raw / 10.0);
    d->pm040 = ((float)d->pm040raw / 10.0);
    d->pm100 = ((float)d->pm100raw / 10.0);
    /* Mark the result as valid. */
    d->valid = 1;
}


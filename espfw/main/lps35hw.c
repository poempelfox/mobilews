/* Talking to the LPS35HW pressure sensor */

#include "esp_log.h"
#include "lps35hw.h"
#include "sdkconfig.h"


#define LPS35HWADDR 0x5d  /* That is the default address of our breakout board */
#define I2C_MASTER_TIMEOUT_MS 1000  /* Timeout for I2C communication */

static i2c_port_t lps35hwi2cport;

static esp_err_t lps35hw_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(lps35hwi2cport,
                                        LPS35HWADDR, &reg_addr, 1, data, len,
                                        pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static esp_err_t lps35hw_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(lps35hwi2cport,
                                     LPS35HWADDR, write_buf, sizeof(write_buf),
                                     pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));

    return ret;
}

void lps35hw_init(i2c_port_t port)
{
    lps35hwi2cport = port;

    /* Configure the LPS35HW */
    /* Other than the LPS25HB which did NOT support a oneshot-mode
     * and thus had to be configured to do continous measurements
     * at the lowest possible rate, the LPS35HW can do one-shot.
     * So we do not need to configure anything here right now,
     * the default values in all control registers are perfectly
     * fine. */
}

void lps35hw_startmeas(void)
{
    /* CTRL_REG2 0x11: IF_ADD_INC (bit 4), ONE_SHOT (bit 0) */
    lps35hw_register_write_byte(0x11, (0x10 | 0x01));
}

double lps35hw_readpressure(void)
{
    uint8_t prr[3];
    if (lps35hw_register_read(0x80 | 0x28, &prr[0], 3) != ESP_OK) {
      /* There was an I2C read error - return a negative pressure to signal that. */
      return -999999.9;
    }
    double press = (((uint32_t)prr[2]  << 16)
                  + ((uint32_t)prr[1]  <<  8)
                  + ((uint32_t)prr[0]  <<  0)) / 4096.0;
    return press;
}


/* Talking to the LTR390 UV / ambient light sensor */

#include "esp_log.h"
#include "ltr390.h"
#include "sdkconfig.h"


#define LTR390ADDR 0x53
#define I2C_MASTER_TIMEOUT_MS 100  /* Timeout for I2C communication */

#define LTR390_REG_MAINCTRL 0x00
#define LTR390_ALSMODE 0x00  /* Ambient Light mode (=UV bit not set) */
#define LTR390_UVSMODE 0x08  /* UV-mode instead of Ambient Light mode */
#define LTR390_LSENABLE 0x02 /* Enable UV/AL-sensor */

#define LTR390_REG_MEASRATE 0x04
#define LTR390_RES20BIT 0x00
#define LTR390_RES19BIT 0x10
#define LTR390_RES18BIT 0x20 /* this is the poweron default */
#define LTR390_RES17BIT 0x30
#define LTR390_RES16BIT 0x40
#define LTR390_RES13BIT 0x50
#define LTR390_RATE0025MS 0x00
#define LTR390_RATE0050MS 0x01
#define LTR390_RATE0100MS 0x02 /* this is the poweron default */
#define LTR390_RATE0200MS 0x03
#define LTR390_RATE0500MS 0x04
#define LTR390_RATE1000MS 0x05
#define LTR390_RATE2000MS 0x06

#define LTR390_REG_GAIN 0x05
/* Explanation for the GAIN register:
 * For UV measurement, just use GAIN18, everything else makes no sense.
 * For AL measurement however, this determines the range and resolution
 * of your measurement, and from the formula in the datasheet we can
 * generate this helpful table:
 * GAIN  |  max Lux measurable | resolution in Lux
 *    1  |             157286  |  0.1500
 *    3  |              52429  |  0.0500
 *    6  |              26214  |  0.0250
 *    9  |              17476  |  0.0167
 *   18  |               8738  |  0.0083
 */
#define LTR390_GAIN01 0x00
#define LTR390_GAIN03 0x01 /* this is the poweron default */
#define LTR390_GAIN06 0x02
#define LTR390_GAIN09 0x03
#define LTR390_GAIN18 0x04

#define LTR390_REG_MAINSTATUS 0x07
#define LTR390_MSTA_NEWDATA 0x08

#define LTR390_REG_INTCFG 0x19
#define LTR390_INT_USEALS 0x10 /* this is the poweron default */
#define LTR390_INT_USEUVS 0x30
#define LTR390_INT_ENABLE 0x04

#define LTR390_REG_ALSDATAL 0x0d  /* LSB */
#define LTR390_REG_ALSDATAM 0x0e
#define LTR390_REG_ALSDATAH 0x0f  /* MSB */

#define LTR390_REG_UVSDATAL 0x10  /* LSB */
#define LTR390_REG_UVSDATAM 0x11
#define LTR390_REG_UVSDATAH 0x12  /* MSB */

static i2c_port_t ltr390i2cport;
static uint8_t alsgainsetting;
/* Correction factors for glass above the sensor. These are
 * different for Ambient Light and UV because the glass
 * filters different wavelengths differently. */
/* FIXME: these were determined experimentally, but under
 * very suboptimal conditions (artifical lighting in the middle
 * of winter). We should probably redo these in summer. */
const double glassfactoral = 1.070; /* for Ambient light */
const double glassfactoruv = 1.102; /* for UV */

static esp_err_t ltr390_writereg(uint8_t reg, uint8_t val)
{
    uint8_t regandval[2];
    regandval[0] = reg;
    regandval[1] = val;
    return i2c_master_write_to_device(ltr390i2cport, LTR390ADDR,
                                      regandval, 2,
                                      I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

void ltr390_startuvmeas(void)
{
    ltr390_writereg(LTR390_REG_GAIN, LTR390_GAIN18);
    ltr390_writereg(LTR390_REG_MAINCTRL, (LTR390_UVSMODE | LTR390_LSENABLE));
}

void ltr390_startalmeas(void)
{
    uint8_t g = LTR390_GAIN01;
    switch (alsgainsetting) {
    case  3: g = LTR390_GAIN03; break;
    case  6: g = LTR390_GAIN06; break;
    case  9: g = LTR390_GAIN09; break;
    case 18: g = LTR390_GAIN18; break;
    };
    ltr390_writereg(LTR390_REG_GAIN, g);
    ltr390_writereg(LTR390_REG_MAINCTRL, (LTR390_ALSMODE | LTR390_LSENABLE));
}

void ltr390_stopmeas(void)
{
    ltr390_writereg(LTR390_REG_MAINCTRL, 0);
}

void ltr390_init(i2c_port_t port)
{
    ltr390i2cport = port;

    /* Configure the LTR390 */
    ltr390_writereg(LTR390_REG_MEASRATE, (LTR390_RES20BIT | LTR390_RATE2000MS));
    ltr390_startuvmeas();
    
    alsgainsetting = 1;
}

double ltr390_readuv(void)
{
    uint8_t uvsreg[3];
    int isvalid = 1;
    uint8_t repctr = 0;
    uint8_t rtr = LTR390_REG_MAINSTATUS;
    do {
      if (isvalid != 1) {
        /* Sleep a short while before retrying */
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      uint8_t res = i2c_master_write_read_device(ltr390i2cport, LTR390ADDR,
                                                 &rtr, 1, &uvsreg[0], 1,
                                                 I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
      if (res != ESP_OK) {
        isvalid = 0;
      } else {
        if ((uvsreg[0] & LTR390_MSTA_NEWDATA) == LTR390_MSTA_NEWDATA) {
          isvalid = 1;
        } else {
          isvalid = 0;
        }
      }
      repctr++;
    } while ((isvalid != 1) && (repctr < 10));
    if (isvalid != 1) {
      ESP_LOGE("ltr390.c", "ERROR: I2C-read from LTR390 failed (3).");
      return -1.0;
    }
    rtr = LTR390_REG_UVSDATAL;
    if (i2c_master_write_read_device(ltr390i2cport, LTR390ADDR,
        &rtr, 1, &uvsreg[0], 3,
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS) != ESP_OK) {
      isvalid = 0;
    }
    if (isvalid != 1) {
      /* Read error, signal that we received nonsense by returning a negative UV index */
      ESP_LOGE("ltr390.c", "ERROR: I2C-read from LTR390 failed (4).");
      return -1.0;
    }
    uint32_t uvsr32 = ((uint32_t)(uvsreg[2] & 0x0F) << 16)
                    | ((uint32_t)uvsreg[1] << 8)
                    | uvsreg[0];
    /* The datasheet uses "UV sensitivity" in the UV index formula, and that one is
     * only given for gain=18 and resolution=20 bits, so we cannot really use anything
     * else.
     * And it is extremely weird: There seem to be versions "1.2"-"1.4" of the datasheet,
     * that list uvsensitivity as 1400 instead of 2300, but optoelectronics.liteon.com
     * does not list the LTR390 at all anymore, and google only finds the version "1.1"
     * datasheet there. The only source for the "1.4" datasheet is a github repo for
     * an Arduino-library: https://github.com/levkovigor/LTR390 */
    double uvsensitivity = 2300.0;
    double uvind = ((double)uvsr32 / uvsensitivity) * glassfactoruv;
    return uvind;
}

double ltr390_readal(void)
{
    uint8_t alsreg[3];
    int isvalid = 1;
    uint8_t rtr = LTR390_REG_MAINSTATUS;
    uint8_t repctr = 0;
    do {
      if (isvalid != 1) {
        /* Sleep a short while before retrying */
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      uint8_t res = i2c_master_write_read_device(ltr390i2cport, LTR390ADDR,
                                                 &rtr, 1, &alsreg[0], 1,
                                                 I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
      if (res != ESP_OK) {
        isvalid = 0;
      } else {
        if ((alsreg[0] & LTR390_MSTA_NEWDATA) == LTR390_MSTA_NEWDATA) {
          isvalid = 1;
        } else {
          isvalid = 0;
        }
      }
      repctr++;
    } while ((isvalid != 1) && (repctr < 10));
    if (isvalid != 1) {
      ESP_LOGE("ltr390.c", "ERROR: I2C-read from LTR390 failed (1).");
      return -1.0;
    }
    rtr = LTR390_REG_ALSDATAL;
    if (i2c_master_write_read_device(ltr390i2cport, LTR390ADDR,
        &rtr, 1, &alsreg[0], 3,
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS) != ESP_OK) {
      isvalid = 0;
    }
    if (isvalid != 1) {
      /* Read error, signal that we received nonsense by returning a negative UV index */
      ESP_LOGE("ltr390.c", "ERROR: I2C-read from LTR390 failed (2).");
      return -1.0;
    }
    uint32_t alsr32 = ((uint32_t)(alsreg[2] & 0x0F) << 16)
                    | ((uint32_t)alsreg[1] << 8)
                    | alsreg[0];
    double lux = (((double)alsr32 * 0.6) / ((double)alsgainsetting * 4.0)) * glassfactoral;
#if 1
    ESP_LOGI("ltr390.c", "DEBUG: raw ALS values %02x%02x%02x at gain %u -> %.3f lux",
                         alsreg[0], alsreg[1], alsreg[2], alsgainsetting, lux);
#endif
    /* Correct the alsgainsetting for the next measurement, if we're
     * either (almost) overflowing or underflowing. */
    if (alsr32 > 0xd000) {
      if (alsgainsetting != 1) {
        ESP_LOGI("ltr390.c", "switching GAIN for next ambient light measurement to 1");
      }
      alsgainsetting = 1;
    }
    if (alsr32 < 0x800) {
      if (alsgainsetting != 18) {
        ESP_LOGI("ltr390.c", "switching GAIN for next ambient light measurement to 18");
      }
      alsgainsetting = 18;
    }
    return lux;
}


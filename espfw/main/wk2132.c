/* Talking to the dfrobot dfr0627 I2C-to-dual-UART module.
 * That module uses a somewhat exotic "weikai WK2132-ISSG" chip to do all the
 * work. It does not even have an english datasheet, so we're working from
 * dfrobots example-code and the datasheet of a similiar chip (WK2124) that
 * is available in English... Who doesn't love a little challenge? */

#include <esp_log.h>
#include <time.h>
#include "wk2132.h"
#include "sdkconfig.h"

#define I2C_MASTER_TIMEOUT_MS 1000  /* Timeout for I2C communication */

#define WK2132DEBUG 0   /* Logs practically all I2C communication when set to 1 */

/* The following is the BASE address of the chip. Because the chip
 * misuses I2C-address-bits to select different functions, it will
 * always respond to that address AND the 7 following addresses.
 * The base address can be influenced via 2 DIP-switches on the
 * breakout board. If both are in their default '1' position, then
 * the resulting base address is 0x70, and the device will claim 0x70 to
 * 0x77 (inclusive).
 * List of all base addresses selectable by the DIP switches:
 * 0x10 (00), 0x30 (01), 0x50 (10), 0x70 (11) */
#define WK2132BASEADDR 0x70

/* These are added to the I2C base address. */
#define WK2132_CHAN0 0x00
#define WK2132_CHAN1 0x02
/* Not on this chip model: #define WK2132_CHAN2 0x04 */
/* Not on this chip model: #define WK2132_CHAN3 0x06 */
#define WK2132_NUM_CHANS 2  /* We have 2 channels, "0" and "1" */
#define WK2132_REGS  0x00
#define WK2132_FIFO  0x01

/* the following defines are mostly c+p from dfrobot - again,
 * there is no english data sheet we could use instead. */

/* Global registers. These are ONLY accessible through sub-UART 0 register
 * space! */
#define REG_WK2132_GENA   0x00   // Global control register, control sub UART clock enable
#define REG_WK2132_GRST   0x01   // Global sub UART reset register, reset a sub UART independently through software
#define REG_WK2132_GMUT   0x02   // Global main UART control register, and will be used only when the main UART is selected as UART, no need to be set here.
#define REG_WK2132_GIER   0x10   // Global interrupt register, control sub UART total interrupt.
#define REG_WK2132_GIFR   0x11   // Global interrupt flag register, only-read register: indicate if there is a interrupt occuring on a sub UART.

/* sub UART page control register.
 * The registers for the sub UARTs seem to be split into two pages,
 * and we need to select which page we want with that register. */
#define REG_WK2132_SPAGE  0x03

/* sub UART registers in page SPAGE0 */
#define REG_WK2132_SCR    0x04   // Sub UART control register
#define REG_WK2132_LCR    0x05   // Sub UART configuration register
#define REG_WK2132_FCR    0x06   // Sub UART FIFO control register
#define REG_WK2132_SIER   0x07   // Sub UART interrupt enable register
#define REG_WK2132_SIFR   0x08   // Sub UART interrupt flag register 
#define REG_WK2132_TFCNT  0x09   // Sub UART transmit FIFO register, OR register
#define REG_WK2132_RFCNT  0x0A   // Sub UART transmit FIFO register, OR register 
#define REG_WK2132_FSR    0x0B   // Sub UART FIFO register, OR register 
#define REG_WK2132_LSR    0x0C   // Sub UART receive register, OR register 
#define REG_WK2132_FDAT   0x0D   // Sub UART FIFO data register 

/* sub UART registers in page SPAGE1 */
#define REG_WK2132_BAUD1  0x04   // Sub UART band rate configuration register high byte
#define REG_WK2132_BAUD0  0x05   // Sub UART band rate configuration register low byte
#define REG_WK2132_PRES   0x06   // Sub UART band rate configuration register decimal part
#define REG_WK2132_RFTL   0x07   // Sub UART receive FIFO interrupt trigger configuration register
#define REG_WK2132_TFTL   0x08   // Sub UART transmit FIFO interrupt trigger configuration register

/* The frequency of the oscillator on the breakout board */
#define FEXTOSC         14745600L // External cystal frequency 14.7456MHz

static i2c_port_t wk2132i2cport;
static uint8_t lastselectedpage[2] = { 99, 99 };

#define GETI2CAD(type, sub_uart) \
  (WK2132BASEADDR | type | ((sub_uart == 1) ? WK2132_CHAN1 : WK2132_CHAN0))

static esp_err_t wk2132_register_read_byte(uint8_t reg_addr, uint8_t sub_uart, uint8_t page, uint8_t * data)
{
    int ret;
    uint8_t write_buf[2];
    uint8_t i2caddr = GETI2CAD(WK2132_REGS, sub_uart);
    page = page & 0x01; // Only one bit allowed.
    if (sub_uart >= WK2132_NUM_CHANS) {
      ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
    }
    if (page != lastselectedpage[sub_uart]) { /* Switch page */
#if (WK2132DEBUG > 0)
      ESP_LOGI("wk2132.c", "switching page to %u on UART %u", page, sub_uart);
#endif /* WK2132DEBUG */
      write_buf[0] = REG_WK2132_SPAGE;
      write_buf[1] = page;
      ret = i2c_master_write_to_device(wk2132i2cport, i2caddr, write_buf, 2,
                                       pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
      if (ret != ESP_OK) { /* that did not work */
        ESP_LOGE("wk2132.c", "could not select page %02x on WK2132.", page);
        return ret;
      }
      lastselectedpage[sub_uart] = page;
    }
    write_buf[0] = reg_addr;
    ret = i2c_master_write_read_device(wk2132i2cport, i2caddr,
                                       write_buf, 1, /* What we write */
                                       data, 1,      /* What we read */
                                       pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    if (ret != ESP_OK) { /* that did not work */
      ESP_LOGE("wk2132.c", "could not read register %02x on WK2132.", reg_addr);
    } else {
#if (WK2132DEBUG > 0)
      ESP_LOGI("wk2132.c", "read %02x from register %02x on UART %u, I2C %02x", *data, reg_addr, sub_uart, i2caddr);
#endif /* WK2132DEBUG */
    }
    return ret;
}

static esp_err_t wk2132_register_write_byte(uint8_t reg_addr, uint8_t sub_uart, uint8_t page, uint8_t data)
{
    int ret;
    uint8_t write_buf[2];
    uint8_t i2caddr = GETI2CAD(WK2132_REGS, sub_uart);
    page = page & 0x01; // Only one bit allowed.
    if (sub_uart >= WK2132_NUM_CHANS) {
      ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
    }
    if (page != lastselectedpage[sub_uart]) { /* Switch page */
      //ESP_LOGI("wk2132.c", "switching page to %u on UART %u", page, sub_uart);
      write_buf[0] = REG_WK2132_SPAGE;
      write_buf[1] = page;
      ret = i2c_master_write_to_device(wk2132i2cport, i2caddr, write_buf, 2,
                                       pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
      if (ret != ESP_OK) { /* that did not work */
        ESP_LOGE("wk2132.c", "could not select page %02x on WK2132.\n", page);
        return ret;
      }
      lastselectedpage[sub_uart] = page;
    }
#if (WK2132DEBUG > 0)
    ESP_LOGI("wk2132.c", "writing %02x to register %02x on UART %u, I2C %02x", data, reg_addr, sub_uart, i2caddr);
#endif /* WK2132DEBUG */
    write_buf[0] = reg_addr;
    write_buf[1] = data;
    ret = i2c_master_write_to_device(wk2132i2cport, i2caddr, write_buf, 2,
                                       pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    if (ret != ESP_OK) { /* that did not work */
      ESP_LOGE("wk2132.c", "could not write register %02x on WK2132.\n", reg_addr);
    }
    return ret;
}

void wk2132_init(i2c_port_t port)
{
    uint8_t d;
    wk2132i2cport = port;
    /* Configure the WK2132 */
    /* shut down all UARTs */
    wk2132_register_read_byte(REG_WK2132_GENA, 0, 0, &d);
    d = d & 0xF0; /* Clear all enable bits */
    wk2132_register_write_byte(REG_WK2132_GENA, 0, 0, d);
    /* disable interrupts */
    wk2132_register_read_byte(REG_WK2132_GIER, 0, 0, &d);
    d = d & 0xF0; /* Clear all enable bits */
    wk2132_register_write_byte(REG_WK2132_GIER, 0, 0, d);
    /* We do NOT configure the UARTs here - they have specific init functions. */
}

void wk2132_serialportinit(uint8_t sub_uart, long baudrate)
{
    uint8_t d;
    if (sub_uart >= WK2132_NUM_CHANS) {
      ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
    }
    /* First enable the clock for the port. */
    wk2132_register_read_byte(REG_WK2132_GENA, 0, 0, &d); // Global register!
    d = d | (1 << sub_uart);
    wk2132_register_write_byte(REG_WK2132_GENA, 0, 0, d); // Global register!
    /* Now soft-reset and unsleep the port */
    wk2132_register_read_byte(REG_WK2132_GRST, 0, 0, &d); // Global register!
    d = d & ~(1 << (sub_uart + 4)); /* Clear the sleep bit */
    d = d | (1 << sub_uart); /* Set the soft-reset bit */
    wk2132_register_write_byte(REG_WK2132_GRST, 0, 0, d); // Global register!
    int repctr = 1000;
    do { /* Wait for the soft-reset bit we just set to clear. */
      esp_err_t e = wk2132_register_read_byte(REG_WK2132_GRST, 0, 0, &d); // Global register!
      if (e != ESP_OK) { break; }
      if ((d & (1 << sub_uart)) == 0) { break; } /* the bit has cleared! */
      repctr--;
    } while (repctr > 0);
    /* Absolutely no interrupts of any kind on this port. */
    wk2132_register_write_byte(REG_WK2132_SIER, sub_uart, 0, 0x00);
    /* temporarily disable RX and TX on the port because we're about to change
     * baudrate and other settings. */
    wk2132_register_write_byte(REG_WK2132_SCR, sub_uart, 0, 0x00);
    /* Enable and reset FIFOs. Set '[TR]FTRIG' (interrupt trigger level) to
     * the maximum settable here (there are dedicated registers, TFTL and RFTL,
     * that override this and are more flexible). */
    wk2132_register_write_byte(REG_WK2132_FCR, sub_uart, 0, 0xFF);
    /* Calculate baudrate-settings and write them to registers */
    uint32_t brmain = (FEXTOSC / (baudrate * 16)) - 1;
    long brfc1 = (FEXTOSC % (baudrate * 16));
    double brfc2 = (double)brfc1 * 10.0 / ((double)baudrate * 16.0);
    uint8_t brfrac = (uint8_t)brfc2;
    wk2132_register_write_byte(REG_WK2132_BAUD1, sub_uart, 1, ((brmain >> 8) & 0xff));
    wk2132_register_write_byte(REG_WK2132_BAUD0, sub_uart, 1, ((brmain >> 0) & 0xff));
    wk2132_register_write_byte(REG_WK2132_PRES, sub_uart, 1, brfrac);
    /* Configure 8N1 - 8 bits, No parity, 1 stop bit. */
    wk2132_register_write_byte(REG_WK2132_LCR, sub_uart, 0, 0x00);
    /* enable RX and TX on the port */
    wk2132_register_write_byte(REG_WK2132_SCR, sub_uart, 0, 0x03);
}

uint8_t wk2132_get_available_to_read(uint8_t sub_uart)
{
  uint8_t bc;
  if (sub_uart >= WK2132_NUM_CHANS) {
    ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
  }
  if (wk2132_register_read_byte(REG_WK2132_RFCNT, sub_uart, 0, &bc) != ESP_OK) {
    return 0;
  }
  return bc;
}

uint8_t wk2132_read_serial(uint8_t sub_uart, char * buf, uint8_t len)
{
  uint8_t res = 0;
  uint8_t bc;
  uint8_t i2caddr = GETI2CAD(WK2132_FIFO, sub_uart);
  if (sub_uart >= WK2132_NUM_CHANS) {
    ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
  }
  /* Find out how many bytes are available in the RX FIFO, and read at most that. */
  wk2132_register_read_byte(REG_WK2132_RFCNT, sub_uart, 0, &bc);
  if (bc > len) { bc = len; }
  for (int i = 0; i < bc; i++) {
    esp_err_t e;
    e = i2c_master_read_from_device(wk2132i2cport,
                                    i2caddr, (uint8_t *)&buf[i], 1,
                                    pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    if (e != ESP_OK) {
      ESP_LOGE("wk2132.c", "Failed to read from FIFO on sub_uart %02x", sub_uart);
      break;
    } else {
#if WK2132DEBUG > 0
      ESP_LOGI("wk2132.c", "read %02x from FIFO on UART %d, I2C %02x", buf[i], sub_uart, i2caddr);
#endif /* WK2132DEBUG */
    }
    res++;
  }
  return res;
}

uint8_t wk2132_write_serial(uint8_t sub_uart, const char * buf, uint8_t len)
{
  uint8_t res = 0;
  uint8_t bc;
  uint8_t i2caddr = GETI2CAD(WK2132_FIFO, sub_uart);
  if (sub_uart >= WK2132_NUM_CHANS) {
    ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
  }
  /* Find out how much space is available in the TX FIFO, and write at most that. */
  wk2132_register_read_byte(REG_WK2132_TFCNT, sub_uart, 0, &bc);
  bc = 0xff - bc;
  if (bc > len) { bc = len; }
  for (int i = 0; i < bc; i++) {
    esp_err_t e;
    e = i2c_master_write_to_device(wk2132i2cport,
                                   i2caddr, (const uint8_t *)&buf[i], 1,
                                   pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    if (e != ESP_OK) {
      ESP_LOGE("wk2132.c", "Failed to write to FIFO on sub_uart %02x", sub_uart);
      break;
    } else {
#if (WK2132DEBUG > 0)
      ESP_LOGI("wk2132.c", "wrote %02x to FIFO on UART %d, I2C %02x", buf[i], sub_uart, i2caddr);
#endif /* WK2132DEBUG */
    }
    res++;
  }
  return res;
}

void wk2132_flush(uint8_t sub_uart)
{
  esp_err_t e;
  uint8_t d;
  time_t stati = time(NULL);
  if (sub_uart >= WK2132_NUM_CHANS) {
    ESP_LOGE("wk2132.c", "non-existant UART %u addressed", sub_uart);
  }
  do {
    e = wk2132_register_read_byte(REG_WK2132_FSR, sub_uart, 0, &d);
    if (e != ESP_OK) break;
    if ((time(NULL) - stati) > 2) break; /* Timeout, abort */
    /* Bits in FSR-register: Bit 0 - TX Busy, Bit 2 - TX FIFO not empty */
  } while ((d & 0x05) != 0);
}

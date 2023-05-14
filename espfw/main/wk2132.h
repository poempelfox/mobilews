
#ifndef _WK2132_H_
#define _WK2132_H_

#include "driver/i2c.h" /* Needed for i2c_port_t */

/* General initialization of the I2C-to-Serial-adapter */
void wk2132_init(i2c_port_t port);

/* Initialize one of the two sub_uarts.
 * Enables the serial port and sets the baudrate. */
void wk2132_serialportinit(uint8_t sub_uart, long baudrate);

/* Get the number of bytes available to read in the FIFO */
uint8_t wk2132_get_available_to_read(uint8_t sub_uart);

/* Reads data from one of the two serial ports.
 * Note that this does not block - it will only read what is currently
 * available in the FIFO. Neither will it exceed the length specified
 * in len. */
uint8_t wk2132_read_serial(uint8_t sub_uart, char * buf, uint8_t len);

/* Sends data out one of the two serial ports. */
uint8_t wk2132_write_serial(uint8_t sub_uart, const char * buf, uint8_t len);

/* Wait for output to flush */
void wk2132_flush(uint8_t sub_uart);

#endif /* _WK2132_H_ */


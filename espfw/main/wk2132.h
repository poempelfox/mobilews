
#ifndef _WK2132_H_
#define _WK2132_H_

#include "driver/i2c.h" /* Needed for i2c_port_t */

/* General initialization of the I2C-to-Serial-adapter */
void wk2132_init(i2c_port_t port);

/* Initialize one of the two sub_uarts.
 * Enables the serial port and sets the baudrate. */
void wk2132_serialportinit(uint8_t sub_uart, long baudrate);

/* Sends data out one of the two serial ports. */
uint8_t wk2132_write_serial(uint8_t sub_uart, const char * buf, uint8_t len);

#endif /* _WK2132_H_ */


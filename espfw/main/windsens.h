
/* Functions for talking to our wind sensors:
 * DFROBOT SEN0483 Wind speed sensor
 * DFROBOT SEN0482 (V2) Wind direction sensor
 * Both are connected via RS485 */

#ifndef _WINDSENS_H_
#define _WINDSENS_H_

#include <stdint.h> /* For uint8_t */

/* Initializes the wind sensors (mostly the ports they're connected to) */
void windsens_init(uint8_t wsp);

/* Returns the wind direction - in degrees (0.0 - 360.0).
 * Returns <0.0 on error.
 * This communicates with and waits for reply from the sensor, so
 * will run for a while. */
float windsens_getwinddir(void);

/* Returns the wind speed - in meters per second (m/s).
 * Returns <0.0 on error.
 * This communicates with and waits for reply from the sensor, so
 * will run for a while. */
float windsens_getwindspeed(void);

#endif /* _WINDSENS_H_ */


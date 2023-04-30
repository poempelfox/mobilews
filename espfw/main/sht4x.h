
/* Talking to SHT4x (SHT40, SHT41, SHT45) temperature / humidity sensors */

#ifndef _SHT4X_H_
#define _SHT4X_H_

#include "driver/i2c.h" /* Needed for i2c_port_t */

struct sht4xdata {
  uint8_t valid;
  uint16_t tempraw;
  uint16_t humraw;
  float temp;
  float hum;
};

/* Initialize the SHT4x */
void sht4x_init(i2c_port_t port);

/* Request a oneshot-measurement from the SHT4x */
void sht4x_startmeas(void);

/* Read temperature / humidity data from the sensor.
 * You need to request a oneshot-measurement before reading,
 * and you can only read every measurement at most once! */
void sht4x_read(struct sht4xdata * d);

/* Run a long (==1 second) heater cycle at medium power.
 * This should improve accuracy of humidity measurements after
 * the sensor has been exposed to high humidity for a long time
 * in a row. See the sensor documentation for details (relevant
 * keyword: creep mitigation). */
void sht4x_heatercycle(void);

#endif /* _SHT4X_H_ */


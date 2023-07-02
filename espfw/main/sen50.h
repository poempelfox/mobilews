
/* Talking to SEN50 particulate matter sensors */

#ifndef _SEN50_H_
#define _SEN50_H_

#include "driver/i2c.h" /* Needed for i2c_port_t */

struct sen50data {
  uint8_t valid;
  uint16_t pm010raw; /* PM 1 */
  uint16_t pm025raw; /* PM 2.5 */
  uint16_t pm040raw; /* PM 4 */
  uint16_t pm100raw; /* PM10 */
  float pm010; /* PM 1 */
  float pm025; /* PM 2.5 */
  float pm040; /* PM 4 */
  float pm100; /* PM10 */
};

/* Initialize the SEN50 */
void sen50_init(i2c_port_t port);

/* Start measurements on the SEN50. */
void sen50_startmeas(void);
/* Stop measurements */
void sen50_stopmeas(void);

/* Read measurement data (particulate matter)
 * from the sensor. */
void sen50_read(struct sen50data * d);

#endif /* _SEN50_H_ */


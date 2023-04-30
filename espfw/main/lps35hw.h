
#ifndef _LPS35HW_H_
#define _LPS35HW_H_

#include "driver/i2c.h" /* Needed for i2c_port_t */

void lps35hw_init(i2c_port_t port);

/* Starts a one-shot measurement. Unfortunately, it is not
 * documented how long that will take. However, since you can
 * configure the sensor to between 1 and 75 measurements per
 * second in continous mode, it should be safe to assume it
 * won't take longer than a second, probably a lot less. */
void lps35hw_startmeas(void);
/* Read the result of the previous measurement. */
double lps35hw_readpressure(void);

#endif /* _LPS35HW_H_ */


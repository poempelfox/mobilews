
#ifndef _LTR390_H_
#define _LTR390_H_

#include "driver/i2c.h" /* Needed for i2c_port_t */

/* Init needs to be called before anything else.
 * will implicitly start UV measurement! */
void ltr390_init(i2c_port_t port);
/* Start an UV measurement. Results available after 400 ms. */
void ltr390_startuvmeas(void);
/* Start an AL (ambient light) measurement. Results after 400 ms. */
void ltr390_startalmeas(void);
/* Stop measurements
 * Measurements will generally be repeated automatically every
 * 2 seconds until stopped with ltr390_stopmeas().
 * Note that there is no need to stop the previous measurement
 * before calling startXXmeas() again. */
void ltr390_stopmeas(void);
/* Read UV measurement results.
 * You should have started an UV measurement 400 ms before,
 * but if you haven't, this will block and wait for up to 500ms
 * for a result.
 */
double ltr390_readuv(void);
/* Read AmbientLight measurement results.
 * You should have started an AL measurement 400 ms before,
 * but if you haven't, this will block and wait for up to 500ms
 * for a result.
 */
double ltr390_readal(void);

#endif /* _LTR390_H_ */


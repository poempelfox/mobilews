
#ifndef _BATSENS_H_
#define _BATSENS_H_

/* Battery Sensor - a.k.a. an ADC pin, where we measure the voltage of
 * our big battery via an external voltage divider. */

void batsens_init(void);

/* This returns the fully converted value, meaning the
 * battery voltage, not the voltage coming from the voltage divider. */
float batsens_read(void);

#endif /* _BATSENS_H_ */


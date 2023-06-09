
#ifndef _BUTTON_H_
#define _BUTTON_H_

/* There is only an init function here.
 * We will then set a global variable (from main.c) to "do" anything. */
void button_init(void);

/* Get the current state of the button */
int button_getstate(void);

/* Detach GPIO from RTC. Needs to be called after waking up from sleep! */
void button_rtcdetach(void);

#endif /* _BUTTON_H_ */


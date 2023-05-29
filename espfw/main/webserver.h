
/* Builtin Webserver (only answering on WiFi, not LTE) */

#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

/* This struct is used to provide data to us.
 * "ev" as in _E_xported _V_alues */
struct ev {
  time_t lastupd;
  float batvolt;
  float hum;
  float press;
  float temp;
  float windspeed;
  float winddirdeg;
};

/* Initialize and start the Webserver. */
void webserver_start(void);

#endif /* _WEBSERVER_H_ */


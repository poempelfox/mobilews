
/* Builtin Webserver (only answering on WiFi, not LTE) */

#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

/* This struct is used to provide data to us.
 * "ev" as in _E_xported _V_alues */
struct ev {
  time_t lastupd;
  float amblight; /* Ambient Light */
  float batvolt;
  float hum;
  float press;
  float raingc; /* Rain Gauge Counter */
  float pm010;  /* Particulate matter 1.0 */
  float pm025;
  float pm040;
  float pm100;  /* Particulate matter 10.0 */
  float temp;
  float uvind;  /* UV Index */
  float windspeed;
  float winddirdeg;
};

/* Initialize and start the Webserver. */
void webserver_start(void);

#endif /* _WEBSERVER_H_ */


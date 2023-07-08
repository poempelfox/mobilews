
#ifndef _SUBMIT_H_
#define _SUBMIT_H_

/* An array of the following structs is handed to the
 * submit_to_wpd_multi function. */
struct wpd {
  char * sensorid;
  float value;
};

/* Submits multiple values to wetter.poempelfox.de.
 * Returns 0 on success. */
int submit_to_wpd_multi(int arraysize, struct wpd * arrayofwpd);

/* This is a convenience function, calling submit_to_wpd_multi
 * with a size 1 array internally. */
int submit_to_wpd(char * sensorid, float value);

#endif /* _SUBMIT_H_ */


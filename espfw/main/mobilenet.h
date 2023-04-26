
#ifndef _MOBILENET_H_
#define _MOBILENET_H_


/* This wakes the LTE module (by pulling its "power" pin for a short while) */
void mn_wakeltemodule(void);

/* Waits for the LTE module ready message on the serial port. */
void mn_waitforltemoduleready(void);

/* Tries to wait until the mobile network has a data connection, with a timeout.
 * This will also automatically handle the case where the module did a
 * fallback to 2G, in which case we have to send an extra command to
 * enable the GPRS data connection.
 * Timeout is in seconds.
 */
void mn_waitfornetworkconn(int timeout);

/* Tries to wait until the mobile network connection has gotten an IP
 * address, with a timeout.
 * Timeout is in seconds.
 */
void mn_waitforipaddr(int timeout);

/* Asks the LTE module to resolve a hostname.
 * Returns a string (!) with the result in obuf, e.g. "127.0.0.2".
 * timeout is in seconds.
 */
void mn_resolvedns(char * hostname, char * obuf, int obufsize, int timeout);

/* Closes a socket number.
 * This does not care about errors, and it uses the asynchronous version of
 * the command, because we really don't care and don't want to wait for a reply.
 * The reply will come at some later time in the form of an URC and will be
 * ignored anyways.
 */
void mn_closesocket(int socketnr);

/* Opens a TCP connection to hostname on port.
 * returns a socket number (should be between 0 and 6 because the module can
 * only create 7 sockets at a time), or <0 on error. */
int mn_opentcpconn(char * hostname, uint16_t port, int timeout);

/* Attempts to write data to a network socket.
 * Errors are just silently ignored.
 */
void mn_writesock(int socket, char * buf, int bufsize, int timeout);

/* Attempts to read data from a network socket.
 * Note that this is nonblocking, it will not wait for data to become available.
 * Returns: Number of bytes actually read.
 */
int mn_readsock(int socket, char * buf, int bufsize, int timeout);

/* This configures the pins / serial UART for the LTE module.
 * This obviously needs to be called before anything else.
 */
void mn_init(void);

/* sends an AT command to the LTE module, and waits for it to return a reply,
 * whether it be an OK or an ERROR. With timeout.
 * Returns <0 if there is an error, but note that error does not mean the
 * string "ERROR" was returned, it means there was no or no valid reply.
 * This function is obviously NOT useful if you care about the reply at
 * all, because you won't see it. It's mostly useful for firing off
 * initialization sequences. This probably should not be exported at all. */
int sendatcmd(char * cmd, int timeout);

#endif /* _MOBILENET_H_ */


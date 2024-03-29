
#ifndef _MOBILENET_H_
#define _MOBILENET_H_


/* This wakes the LTE module (by pulling its "power" pin for a short while).
 * Note that this is a complete NOOP if the module already is awake. It
 * does not even reset the timeout-before-it-goes-to-sleep. And there is
 * no way to reset the module beforehand to ensure it's not awake already. */
void mn_wakeltemodule(void);

/* Waits for a sign that the LTE module is ready.
 * There are two signs that we accept: Either the module sends its configured
 * powerup-message, or it sends an "OK", which we consider to be a reply to the
 * 'AT' command we try to send on entering this function (if the hardware FIFO
 * has space for it, if it hasn't, then output has probably been blocked for a
 * long time already and everything is broken anyways).
 * Returns "0" if a sign the module might be ready was received,
 * or "1" otherwise. */
int mn_waitforltemoduleready(void);

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
 * Returns 0 on success.
 */
int mn_writesock(int socket, char * buf, int bufsize, int timeout);

/* Attempts to read data from a network socket.
 * Note that this is nonblocking, it will not wait for data to become available.
 * Returns: Number of bytes actually read.
 */
int mn_readsock(int socket, char * buf, int bufsize, int timeout);

/* This causes a hard powercycle of the LTE module. Necessary
 * if that thing crashed yet again and is not software-recoverable,
 * as usual. */
void mn_powercycleltemodem(void);

/* This configures the pins / serial UART for the LTE module.
 * This obviously needs to be called before anything else.
 */
void mn_init(void);

/* This sends setup/configuration commands to the LTE module,
 * including one-time setup if that is enabled during compilation. */
void mn_configureltemodule(void);

/* This repeats the config commands that you seem to need to
 * repeat everytime the module wakes up from sleep. */
void mn_repeatcfgcmds(void);

/* Tells the LTE module to reboot. */
void mn_rebootltemodule(void);

/* gets info about signal strength / used network from the LTE modem */
void mn_getmninfo(char * obuf);

/* Queues a command to be sent to the LTE module later.
 * This is meant to be used by e.g. the webserver to queue a command
 * for selecting a specific mobile network.
 * Returns 0 on success or 1 on error (not enough bufferspace) */
int mn_queuecommand(char * cmd);

/* Sends the commands queued by mn_queuecommand to the LTE module
 * (if none are queued this is just a noop) */
void mn_sendqueuedcommands(void);

/* sends an AT command to the LTE module, and waits for it to return a reply,
 * whether it be an OK or an ERROR. With timeout.
 * Returns <0 if there is an error, but note that error does not mean the
 * string "ERROR" was returned, it means there was no or no valid reply.
 * This function is obviously NOT useful if you care about the reply at
 * all, because you won't see it. It's mostly useful for firing off
 * initialization sequences.
 * This probably should not be exported at all, and will probably be
 * removed at some point. */
int sendatcmd(char * cmd, int timeout);

#endif /* _MOBILENET_H_ */


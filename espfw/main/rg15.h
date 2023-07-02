
#ifndef _RG15_H_
#define _RG15_H_

/* This needs to be called AFTER wk2132_init() */
void rg15_init(void);

/* Requests a read from the rain sensor. It will need
 * some time to actually reply, and there is really no way to
 * tell when a reply is ready. */
void rg15_requestread(void);

/* Reads rain count. */
float rg15_readraincount(void);

#endif /* _RG15_H_ */


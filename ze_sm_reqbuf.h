/*
 * ZeSense CoAP Streaming Server
 * -- fixed length FIFO buffer (non-circular)
 * 	  for incoming requests to Streaming Manager
 * 	  from the CoAP Server
 * 	  thread safe implementation
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#ifndef ZE_SM_REQBUF_H
#define ZE_SM_REQBUF_H

#include <pthread.h>
#include "asynchronous.h"
#include "ze_ticket.h"

/* Buffer size */
#define SM_RBUF_SIZE		20

typedef struct ze_sm_request_t {
	/* Request type */
	int rtype;

	int sensor;

	ticket_t ticket;

	/* Request parameters, NULL when they do not apply */
	int freq;
	/*
	coap_address_t dest;
	int tknlen;
	unsigned char *tkn; //remember to allocate a new one!
	*/
} ze_sm_request_t;

typedef struct ze_sm_request_buf_t {

	ze_sm_request_t rbuf[SM_RBUF_SIZE];

	/* Indexes, wrap around according to %SM_RBUF_SIZE*/
	int gethere, puthere;

	/* Item counter, does not wrap around */
	int counter;

	/* Thread synch (no need of the empty condition) */
	pthread_mutex_t mtx;
	pthread_cond_t notfull;
	//pthread_cond_t notempty;
} ze_sm_request_buf_t;


/**
 * To fit our purposes:
 * - It MUST NOT block on the empty condition. The putter might not
 * feed any more data into it, but we must go on!
 * - It COULD block on the mutex. We're confident that the getter
 * will not starve us.
 *
 * Gets the oldest item in the buffer @p buf. It blocks if the buffer is
 * being used by another thread, it does not block if empty.
 *
 * @param The buffer instance
 *
 * @return The oldest item in the buffer, NULL if buffer empty
 */
ze_sm_request_t get_request_buf_item(ze_sm_request_buf_t *buf);

/*
 * To fit our purposes:
 * - It SHOULD block on the full condition. Were do we put the message from
 * the network once we've fetched it from the socket?  We're confident that
 * the getter will not starve us.
 * - It SHOULD block on the mutex. We're confident that the getter will not
 * starve us.
 *
 * Puts an item in the buffer @p buf. It blocks if the buffer is
 * being used by another thread, and it also blocks indefinitely is
 * the buffer is full.
 *
 * @param The buffer instance
 * @param The item to be inserted, passed by value
 *
 * @return Zero on success
 */
int put_request_buf_item(ze_sm_request_buf_t *buf, int rtype, int sensor, /*coap_address_t dest,*/
		ticket_t reg, int freq/*, int tknlen, unsigned char *tkn*/);

ze_sm_request_buf_t* init_sm_buf();

#endif

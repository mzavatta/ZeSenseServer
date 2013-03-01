/*
 * ZeSense CoAP Streaming Server
 * -- fixed length FIFO buffer (non-circular)
 * 	  for incoming requests to the CoAP Server
 * 	  from the Streaming Manager
 * 	  thread-safe implementation
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#ifndef ZE_COAP_REQBUF_H
#define ZE_COAP_REQBUF_H

#include <pthread.h>
#include <errno.h>
#include "ze_coap_payload.h"
#include "asynchronous.h"

/* Buffer size */
#define COAP_RBUF_SIZE		20

#define ZE_PARAM_UNDEFINED	(-1)

struct ze_coap_request_buf_t;
struct ze_coap_request_t;

typedef struct ze_sm_response_t {

	/* Request header. */
	int rtype;				//Request type
	ticket_t ticket; 	//Request ticket
	int conf;				//Reliability desired (CON or NON)

	/* Request payload, opaque type.
	 * The receiver is assumed to know the payload structure
	 * he is receiving. The buffer is just a medium. */
	unsigned char *pk;
	//ze_payload_t *pyl;

} ze_sm_response_t;

typedef struct ze_sm_response_buf_t {

	ze_sm_response_t rbuf[COAP_RBUF_SIZE];

	/* Indexes, wrap around according to %COAP_RBUF_SIZE*/
	int gethere, puthere;

	/* Item counter, does not wrap around */
	int counter;

	/* Thread synch (no need of the empty condition) */
	pthread_mutex_t mtx;
	pthread_cond_t notfull;
	//pthread_cond_t notempty;
} ze_sm_response_buf_t;

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
ze_sm_response_t get_coap_buf_item(ze_sm_response_buf_t *buf);

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
 * the buffer is full. It DOES NOT make a copy of the parameters
 * passed by pointer;
 *
 * @param The buffer instance
 * @param The item to be inserted, passed by value
 *
 * @return Zero on success
 */
int put_coap_buf_item(ze_sm_response_buf_t *buf, int rtype,
		ticket_t reg, int conf, /*ze_payload_t *pyl*/unsigned char *pk);


ze_sm_response_buf_t* init_coap_buf();


#endif

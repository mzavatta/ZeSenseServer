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
#include "ze_payload.h"
#include "asynchronous.h"

/* Buffer size */
#define COAP_RBUF_SIZE		20

#define ZE_PARAM_UNDEFINED	(-1)

struct ze_coap_request_buf_t;
struct ze_coap_request_t;

typedef struct ze_coap_request_t {
	/* Request type */
	int rtype;

	/* Ticket corresponding to the underlying registration */
	coap_ticket_t ticket;

	/* CON or NON */
	int conf;

	ze_payload_t *pyl;
	/* this could be turned into
	 * unsigned char *pyl;
	 * int pyllength;
	 * keeping the ze_payload_t type to be comfortable
	 * for sizeof(.)
	 * otherwise the server has to convert the structure
	 * into an undistinguished unsigned char*  thus
	 * needing to copy it
	 * plus we save a free, because in pyl there is another
	 * pointer, pyl->data with heap memory attached to it
	 */
} ze_coap_request_t;

typedef struct ze_coap_request_buf_t {

	ze_coap_request_t rbuf[COAP_RBUF_SIZE];

	/* Indexes, wrap around according to %COAP_RBUF_SIZE*/
	int gethere, puthere;

	/* Item counter, does not wrap around */
	int counter;

	/* Thread synch (no need of the empty condition) */
	pthread_mutex_t mtx;
	pthread_cond_t notfull;
	//pthread_cond_t notempty;
} ze_coap_request_buf_t;

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
ze_coap_request_t get_coap_buf_item(ze_coap_request_buf_t *buf);

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
int put_coap_buf_item(ze_coap_request_buf_t *buf, int rtype,
		coap_ticket_t reg, int conf, ze_payload_t *pyl);


ze_coap_request_buf_t* init_coap_buf();


#endif

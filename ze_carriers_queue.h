/*
 * ZeSense CoAP Streaming Server
 * -- unlimited length FIFO buffer
 * 	  for events to Streaming Manager
 * 	  from unknown sampling frequency streams
 * 	  thread safe implementation
 * 	  unblocking get operation
 * 	  put fails if run out of memory
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */
#ifndef ZE_CARRIERS_QUEUE_H
#define ZE_CARRIERS_QUEUE_H

#include <pthread.h>
#include <android/sensor.h>
#include "asynchronous.h"
#include "ze_ticket.h"
#include "ze_log.h"


/* Buffer size */
#define SM_RBUF_SIZE		20

typedef struct ze_cq_elem_t {
	struct ze_cq_elem_t *next;
	ASensorEvent event;
} ze_cq_elem_t;

typedef struct ze_carriers_queue_t {

	ze_cq_elem_t *head;

	/* Indexes, wrap around according to %SM_RBUF_SIZE*/
	//int gethere, puthere;

	/* Item counter, does not wrap around */
	int counter;

	/* Thread synch (no need of the empty condition) */
	pthread_mutex_t mtx;
	//pthread_cond_t notfull;
	//pthread_cond_t notempty;
} ze_carriers_queue_t;

ze_carriers_queue_t* init_carriers_queue();
int put_carrier_event(ze_carriers_queue_t *queue, ASensorEvent event);
int get_carrier_event(ze_carriers_queue_t *queue, ASensorEvent *event);

#endif

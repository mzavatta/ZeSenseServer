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

#include "ze_sm_reqbuf.h"


ze_sm_request_t get_sm_buf_item(ze_sm_request_buf_t *buf) {

	/* Here I have the choice of creating a new item or just pass
	 * it back by value.
	 * I would prefer allocating a new space for it but still..
	 */

	ze_sm_request_t temp;

	pthread_mutex_lock(buf->mtx);
		if (buf->counter <= 0) { //empty (shall never < 0 anyway)
			/*
			 * pthread_cond_wait(buf->notempty, buf->mtx);
			 * do nothing, we must not block!
			 */
			/* Signal that the buffer is empty by returning
			 * an invalid request */
			temp.rtype = SM_REQ_INVALID;
		}
		else {
			/* no need to do checkout + release of the coap_registration_t* pointer
			 * since the copy in the buffer will be forgotten (overwitten)
			 */
			temp = buf->rbuf[buf->gethere];
			//buf->rbuf[buf->gethere].tkn = NULL; //null the token pointer
			buf->gethere = ((buf->gethere)+1) % SM_RBUF_SIZE;
			counter--;
			pthread_cond_signal(buf->notfull); //surely no longer full
		}
	pthread_mutex_unlock(buf->mtx);

	return temp;
}


int put_sm_buf_item(ze_sm_request_buf_t *buf, int rtype, int sensor,
		coap_ticket_t reg, int freq) {

	pthread_mutex_lock(buf->mtx);
		if (buf->counter >= SM_RBUF_SIZE) { //full (greater shall not happen)
			pthread_cond_wait(buf->notfull, buf->mtx);
		}

		/* -copy- contents. */
		buf->rbuf[buf->puthere].rtype = rtype;
		buf->rbuf[buf->puthere].sensor = sensor;
		/* Pass the ticket along. */
		buf->rbuf[buf->puthere].ticket = reg;
		buf->rbuf[buf->puthere].freq = freq;

		buf->puthere = ((buf->puthere)+1) % SM_RBUF_SIZE;
		counter++;
		//pthread_cond_signal(buf->notempty); //surely no longer empty
	pthread_mutex_unlock(buf->mtx);

	return 0;
}

void init_sm_buf(ze_sm_request_buf_t *buf) {

	buf = malloc(sizeof(ze_sm_request_buf_t));
	if (buf == NULL) return;

	memset(buf->rbuf, 0, SM_RBUF_SIZE*sizeof(ze_sm_request_t));

	/* What happens if a thread tries to initialize a mutex or a cond var
	 * that has already been initialized? "POSIX explicitly
	 * states that the behavior is not defined, so avoid
	 * this situation in your programs"
	 */
	int error = pthread_mutex_init(buf->mtx, NULL);
	if (error)
		fprintf(stderr, "Failed to initialize mtx:%s\n", strerror(error));

	error = pthread_cond_init(buf->notfull, NULL);
	if (error)
		fprintf(stderr, "Failed to initialize full cond var:%s\n", strerror(error));

	/*
	 * error = pthread_cond_init(buf->notempty, NULL);
	 * if (error)
	 *	 fprintf(stderr, "Failed to initialize empty cond var:%s\n", strerror(error));
	 */

	/* Reset indexes */
	buf->gethere = 0;
	buf->puthere = 0;
	buf->counter = 0;
}

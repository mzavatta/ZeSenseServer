/*
 * ZeSense CoAP Streaming Server
 * -- fixed length FIFO buffer (non-circular)
 * 	  for incoming requests to the CoAP server
 * 	  from the Streaming Manager
 * 	  thread-safe implementation
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#include "ze_coap_reqbuf.h"


ze_coap_request_t get_coap_buf_item(ze_coap_request_buf_t *buf) {

	ze_coap_request_t temp;

	pthread_mutex_lock(buf->mtx);
		if (buf->counter <= 0) { //empty (shall never < 0 anyway)
			/*
			 * pthread_cond_wait(buf->notempty, buf->mtx);
			 * do nothing, we must not block!
			 */
			/* Signal that the buffer is empty by returning
			 * an invalid request */
			temp.rtype = COAP_SMREQ_INVALID;
		}
		else {
			temp = buf->rbuf[buf->gethere];
			//temp.reg = coap_registration_checkout(temp.reg);
			buf->gethere = ((buf->gethere)+1) % COAP_RBUF_SIZE;
			counter--;
			pthread_cond_signal(buf->notfull); //surely no longer full
		}
	pthread_mutex_unlock(buf->mtx);

	return temp;
}

int put_coap_buf_item(ze_coap_request_buf_t *buf, int rtype,
		coap_ticket_t reg, int conf, ze_payload_t *pyl) {

	pthread_mutex_lock(buf->mtx);
		if (buf->counter >= COAP_RBUF_SIZE) { //full (greater shall not happen)
			pthread_cond_wait(buf->notfull, buf->mtx);
		}
		buf->rbuf[buf->puthere].rtype = rtype;
		buf->rbuf[buf->puthere].reg = reg;
		buf->rbuf[buf->puthere].conf = conf;
		buf->rbuf[buf->puthere].pyl = pyl;

		/*
		buf->rbuf[buf->puthere].str = str;
		buf->rbuf[buf->puthere].dest = dest;
		buf->rbuf[buf->puthere].tknlen = tknlen;
		buf->rbuf[buf->puthere].tkn = tkn;
		*/

		buf->puthere = ((buf->puthere)+1) % COAP_RBUF_SIZE;
		counter++;
		//pthread_cond_signal(buf->notempty); //surely no longer empty
	pthread_mutex_unlock(buf->mtx);

	return 0;
}

void init_coap_buf(ze_coap_request_buf_t *buf) {

	buf = malloc(sizeof(ze_coap_request_buf_t));
	if (buf == NULL) return;

	memset(buf->rbuf, 0, COAP_RBUF_SIZE*sizeof(ze_coap_request_t));

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

	/* Reset pointers */
	buf->gethere = 0;
	buf->puthere = 0;
	buf->counter = 0;
}

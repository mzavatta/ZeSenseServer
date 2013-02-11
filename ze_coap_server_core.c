/*
 * ZeSense CoAP server
 * -- core module
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 *
 * Built using libcoap by
 * Olaf Bergmann <bergmann@tzi.org>
 * http://libcoap.sourceforge.net/
 */
#include "ze_log.h"
#include "ze_coap_server_core.h"
#include "ze_coap_server_root.h"
#include "net.h"
#include "subscribe.h"
#include "resource.h"

void *
ze_coap_server_core_thread(void *args) {

	LOGI("Hello CoAP server thread! pid%d, tid%d", getpid(), gettid());

	struct coap_thread_args* ar = ((struct coap_thread_args*)(args));
	coap_context_t *cctx = ar->cctx;
	ze_sm_request_buf_t *smreqbuf = ar->smreqbuf;
	ze_coap_request_buf_t *notbuf = ar->notbuf;

	/* Switch on, off. */
	//pthread_exit(NULL);


	fd_set readfds;
	struct timeval tv, *timeout;

	ze_coap_request_t req;
	coap_pdu_t *pdu = NULL;
	coap_async_state_t *asy = NULL, *tmp;
	//coap_tid_t asyt;
	coap_resource_t *res;
	coap_queue_t *nextpdu = NULL;
	coap_tick_t now;
	int result;
	int smcount = 0;

	unsigned char *pyl = NULL;
	int pyllength = 0;


	while (!globalexit) { /*---------------------------------------------*/

	/* Linux man pages:
	 * Since select() modifies its file descriptor sets,
	 * if the call is being  used in a loop,
	 * then the sets must be reinitialized before each call.
	 */
	FD_ZERO(&readfds);
	FD_SET( cctx->sockfd, &readfds );

	/*----------------- Consider retransmissions ------------------------*/

	nextpdu = coap_peek_next( cctx );

	coap_ticks(&now);
	while ( nextpdu && nextpdu->t <= now ) {
		coap_retransmit( cctx, coap_pop_next( cctx ) );
		nextpdu = coap_peek_next( cctx );
	}

	//LOGI("Retransmissions done for this round..");

	/*---------------------- Serve network requests -------------------*/

	tv.tv_usec = 10000; //10msec
	tv.tv_sec = 0;
	timeout = &tv;

	/* Interesting actually,
	 * for the moment do the easy way..
	 *
	if ( nextpdu && nextpdu->t <= now + COAP_RESOURCE_CHECK_TIME ) {
		// set timeout if there is a pdu to send before our automatic timeout occurs
		tv.tv_usec = ((nextpdu->t - now) % COAP_TICKS_PER_SECOND) * 1000000 / COAP_TICKS_PER_SECOND;
		tv.tv_sec = (nextpdu->t - now) / COAP_TICKS_PER_SECOND;
		timeout = &tv;
	} else {
		tv.tv_usec = 0;
		tv.tv_sec = COAP_RESOURCE_CHECK_TIME;
		timeout = &tv;
	}
	*
	*/

	result = select( FD_SETSIZE, &readfds, 0, 0, timeout );

	if ( result < 0 ) {	/* error */
		if (errno != EINTR)
			perror("select");
	} else if ( result > 0 ) {	/* read from socket */
		if ( FD_ISSET( cctx->sockfd, &readfds ) ) {
			LOGI("CS Something appeared on the socket, start reading..");
			coap_read( cctx );	/* read received data */
			coap_dispatch( cctx );	/* and dispatch PDUs from receivequeue */
		}
	} else {	/* timeout */
		//LOGI("select timed out..");
		/* coap_check_resource_list( ctx ); */
	}

	/*------------------------- Serve SM requests ---------------------*/

	/*
	 * How many times do we listen to SM requests?
	 */
	while (smcount < SMREQ_RATIO) {

	/* Start by fetching an SM request and dispatch it
	 */
	req = get_coap_buf_item(notbuf);
	/* Recall that the getter does not do any checkout not free
	 * Under this model only the CoAP server manages the ticket
	 */

	if (req.rtype == COAP_STREAM_STOPPED) {
		LOGI("CS Got a STREAM STOPPED command");
		/* We're sure that no other notification will arrive
		 * with that ticket. Release it on behalf of the streaming
		 * manager. If there is no ongoing transaction, the registration
		 * associated to it will be destroyed.
		 */
		res = coap_get_resource_from_key(cctx, req.ticket.reg->reskey);
		coap_registration_release(res, req.ticket.reg);
	}
	else if (req.rtype == COAP_SEND_ASYNCH) {
		LOGI("CS Got a SEND ASYNCH command");
		/* Lookup in the async register using the ticket tid..
		 * it shall find it..
		 */
		asy = coap_find_async(cctx, req.ticket.tid);
		if (asy != NULL) {

			/* Build payload. */
			pyllength = sizeof(int64_t)+sizeof(int)+(req.pyl->length);
			pyl = malloc(pyllength);
			if (pyl == NULL) {
				LOGW("CS cannot malloc for payload in server core thread");
				exit(1);
			}
			memcpy(pyl, &(req.pyl->wts), sizeof(int64_t));
			memcpy(pyl+sizeof(int64_t), &(req.pyl->length), sizeof(int));
			memcpy(pyl+sizeof(int64_t)+sizeof(int), req.pyl->data, req.pyl->length);

			/* Need to add options in order... */
			pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
					coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
			coap_add_option(pdu, COAP_OPTION_TOKEN, asy->tokenlen, asy->token);
			coap_add_data(pdu, pyllength, pyl);

			/* Send message. */
			if (req.conf == COAP_MESSAGE_CON) {
				LOGI("CS Sending confirmable message");
				coap_send_confirmed(cctx, &(asy->peer), pdu);
			}
			else if (req.conf == COAP_MESSAGE_NON) {
				LOGI("CS Sending non confirmable message");
				coap_send(cctx, &(asy->peer), pdu);
				coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE);
				free(pyl);
			}
			else LOGW("CS could not understand message type");

			/* Asynchronous request satisfied, regardless of whether
			 * a CON and an ACK will arrive, remove it. */
			coap_remove_async(cctx, asy->id, &tmp);
		}
		else LOGW("CS Got oneshot sample but no asynch request matches the ticket");

		free(req.pyl->data);
		free(req.pyl);
	}
	else if (req.rtype == COAP_SEND_NOTIF) {
		LOGI("CS Got a SEND NOTIF command");

		/* Here I have to check if the registration
		 * that corresponds to the ticket is still
		 * valid. It may be, indeed, that the failcount
		 * has topped, a stopstream request has been
		 * issued, but some previous send commands
		 * are still in the buffer. So we must protect
		 * and do not send notifications belonging
		 * to a registration that is already invalidated
		 * and in the process of being destroyed.
		 * What do we do with its ticket? As we don't
		 * pass it along to anybody and the official
		 * destruction will arrive later through a
		 * STREAM STOPPED confirmation, just forget
		 * about it.
		 */
		if (req.ticket.reg->fail_cnt <= COAP_OBS_MAX_FAIL) {

			/* Transfer our payload structure into a series of bytes.
			 * Sending only the timestamp and the sensor event for the
			 * moment.
			 * TODO: optimize it so that we don't create another copy
			 * and it need not malloc every loop
			 * could be passed already in this way by the request manager..
			 */
			pyllength = sizeof(int64_t)+sizeof(int)+(req.pyl->length);
			pyl = malloc(pyllength);
			if (pyl == NULL) {
				LOGW("CS cannot malloc for payload in server core thread");
				exit(1);
			}
			memcpy(pyl, &(req.pyl->wts), sizeof(int64_t));
			memcpy(pyl+sizeof(int64_t), &(req.pyl->length), sizeof(int));
			memcpy(pyl+sizeof(int64_t)+sizeof(int), req.pyl->data, req.pyl->length);

			/* Need to add options in order... */
			pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
					coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
			coap_add_option(pdu, COAP_OPTION_SUBSCRIPTION, sizeof(short), (unsigned char*)&(cctx->observe));
			coap_add_option(pdu, COAP_OPTION_TOKEN, req.ticket.reg->token_length, req.ticket.reg->token);
			coap_add_data(pdu, pyllength, pyl);

			if (req.ticket.reg->non_cnt >= COAP_OBS_MAX_NON || req.conf == COAP_MESSAGE_CON) {
				/* Either the max NON have been reached or
				 * we explicitly requested a CON.
				 * Send a CON and clean the NON counter
				 */
				LOGI("CS Sending confirmable notification");
				coap_notify_confirmed(cctx, &(req.ticket.reg->subscriber), pdu,
						coap_registration_checkout(req.ticket.reg) );
				req.ticket.reg->non_cnt = 0;
			}
			else if (req.conf == COAP_MESSAGE_NON) {
				/* send a non-confirmable
				 * and increase the NON counter
				 * no need to keep the transaction state
				 */
				LOGI("CS Sending non confirmable notification");
				coap_send(cctx, &(req.ticket.reg->subscriber), pdu);
				req.ticket.reg->non_cnt++;

				coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE);
				free(pyl);
			}
			else LOGW("CS Could not understand message type.");


			/* Even if pyl is a pointer to char, it does not
			 * free only one byte. The heap manager stores
			 * when doing malloc() the number of bytes it allocated
			 * nearby the allocated block. So free will know
			 * how many bytes to deallocate.
			 * Still, we don't have to free it as it is referenced
			 * in the transaction record in case of confirmable
			 * message. Well, we do have to free if if we're not
			 * sending a confirmable.
			 */
			//coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE);
			//free(pyl);
		}
		else LOGI("Not sending notification, registration already invalid.");

		/* These are different from pyl because we do a copy,
		 * so free them in any case.
		 */
		free(req.pyl->data);
		free(req.pyl);
	}
	else if (req.rtype == COAP_SMREQ_INVALID) {
		/* Buffer's empty, do not loop any more times,
		 * better to go on doing something else.
		 */
		//foundempty = 1;
	}
	else {
		LOGW("CS Cannot interpret SM request type");
		exit(1);
	}

	smcount++;

	/* Sleep for a while, not much actually. */
	struct timespec rqtp;
	rqtp.tv_sec = 0;
	rqtp.tv_nsec = 1000000; //1msec
	nanosleep(&rqtp, NULL);

	}

	smcount=0;
	//foundempty=0;

	} /*-----------------------------------------------------------------*/

	LOGI("CoAP server out of thread loop, returning..");
}

/* token comparison
&& (!token || (token->length == s->token_length
	       && memcmp(token->s, s->token, token->length) == 0)) */

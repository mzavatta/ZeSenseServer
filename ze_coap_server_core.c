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

#include "ze_coap_server_core.h"

ze_coap_server_core_thread(coap_context_t *cctx, ze_sm_request_buf_t *smreqbuf,
		ze_coap_request_buf_t *notbuf) {

	fd_set readfds;
	struct timeval tv, *timeout;

	ze_coap_request_t req;
	coap_address_t dest;
	coap_pdu_t *pdu = NULL;
	coap_async_state_t *asy = NULL, *tmp;
	coap_tid_t asyt;
	coap_resource_t *res;
	coap_subscription_t *sub = NULL, *subt = NULL;

	size_t pdusize = 0;

	unsigned char *pyl = NULL;
	int plylength = 0;


	while (1) { /*----------------------------------------------------*/

	/* Linux man pages:
	 * Since select() modifies its file descriptor sets,
	 * if the call is being  used in a loop,
	 * then the sets must be reinitialized before each call.
	 */
	FD_ZERO(&readfds);
	FD_SET( ctx->sockfd, &readfds );

	/*----------------- Consider retransmissions ------------------------*/

	nextpdu = coap_peek_next( ctx );

	coap_ticks(&now);
	while ( nextpdu && nextpdu->t <= now ) {
		coap_retransmit( ctx, coap_pop_next( ctx ) );
		nextpdu = coap_peek_next( ctx );
	}

	/*---------------------- Serve network requests -------------------*/

	tv.tv_usec = 2000; //2msec
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
		if ( FD_ISSET( ctx->sockfd, &readfds ) ) {
			coap_read( ctx );	/* read received data */
			coap_dispatch( ctx );	/* and dispatch PDUs from receivequeue */
		}
	} else {	/* timeout */
		/* coap_check_resource_list( ctx ); */
	}

	/*------------------------- Serve SM requests ---------------------*/

	/*
	 * How many times do we listen to SM requests?
	 */
	while (smcount < SMREQ_RATIO) {

	/* Start by fetching an SM request and dispatch it
	 */
	req = get_req_buf_item(notbuf);
	/* Recall that the getter does not do any checkout not free
	 * Under this model only the CoAP server manages the ticket
	 */

	if (req.rtpye == COAP_STREAM_STOPPED) {
		/* We're sure that no other notification will arrive
		 * with that ticket. Release it on behalf of the streaming
		 * manager. If there is no ongoing transaction, the registration
		 * associated to it will be destroyed.
		 */
		coap_registration_release(req.ticket.reg);
	}
	else if (req.rtype == COAP_SEND_ASYNCH) {

		/* Lookup in the async register using the ticket tid..
		 * it shall find it..
		 */
		asy = coap_find_async(cctx, req.ticket.tid);
		if (asy != NULL) {

			/* Build payload. */
			pyllength = sizeof(int64_t)+sizeof(int)+(req.pyl->length);
			pyl = malloc(pyllegth);
			if (pyl == NULL) {
				LOGW("cannot malloc for payload in server core thread");
				exit(1);
			}
			memcpy(pyl, &(reg.pyl->wts), sizeof(int64_t));
			memcpy(pyl+sizeof(int64_t), &(req.pyl->length), sizeof(int));
			memcpy(pyl+sizeof(int64_t)+sizeof(int), req.pyl->data, req.pyl->length);

			/* Need to add options in order... */
			pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
					coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
			coap_add_option(pdu, COAP_OPTION_TOKEN, asy->tokenlen, asy->token);
			coap_add_data(pdu, pyllength, pyl);

			/* Send message. */
			if (req.conf == COAP_MESSAGE_CON) {
				coap_send_confirmed(cctx, asy->peer, pdu);
			}
			else if (req.conf == COAP_MESSAGE_NON) {
				coap_send(cctx, asy->peer, pdu);
			}
			else LOGW("could not understand message type");

			coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE);
			free(pyl);

			/* Asynchronous request satisfied, regardless of whether
			 * a CON and an ACK will arrive, remove it. */
			coap_remove_async(cctx, asy->id, &tmp);
		}
		else LOGW("Got oneshot sample but no asynch request matches the ticket");

	}
	else if (req.rtype == COAP_SEND_NOTIF) {

		/* Transfer our payload structure into a series of bytes.
		 * Sending only the timestamp and the sensor event for the
		 * moment.
		 * TODO: optimize it so that we don't create another copy
		 * and it need not malloc every loop
		 * could be passed already in this way by the request manager..
		 */
		pyllength = sizeof(int64_t)+sizeof(int)+(req.pyl->length);
		pyl = malloc(pyllegth);
		if (pyl == NULL) {
			LOGW("cannot malloc for payload in server core thread");
			exit(1);
		}
		memcpy(pyl, &(reg.pyl->wts), sizeof(int64_t));
		memcpy(pyl+sizeof(int64_t), &(req.pyl->length), sizeof(int));
		memcpy(pyl+sizeof(int64_t)+sizeof(int), req.pyl->data, req.pyl->length);

		/* Need to add options in order... */
		pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
				coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
		coap_add_option(pdu, COAP_OPTION_SUBSCRIPTION, sizeof(short), cctx->observe);
		coap_add_option(pdu, COAP_OPTION_TOKEN, req.ticket.reg->token_length, req.ticket.reg->token);
		coap_add_data(pdu, pyllength, pyl);

		if (req.ticket.reg->non_cnt >= COAP_OBS_MAX_NON || req.conf == COAP_MESSAGE_CON) {
			/* Either the max NON have been reached or
			 * we explicitly requested a CON.
			 * Send a CON and clean the NON counter
			 */
			coap_notify_confirmed(cctx, req.ticket.reg->subscriber, pdu,
					coap_registration_checkout(req.ticket.reg) );
			req.ticket.reg->non_cnt = 0;
		}
		else if (req.conf == COAP_MESSAGE_NON) {
			/* send a non-confirmable
			 * and increase the NON counter
			 * no need to keep the transaction state
			 */
			coap_send(cctx, req.ticket.reg->subscriber, pdu);
			req.ticket.reg->non_cnt++;
		}
		else LOGW("Could not understand message type.");

		coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE);
		/* Even if pyl is a pointer to char, it does not
		 * free only one byte. The heap manager stores
		 * when doing malloc() the number of bytes it allocated
		 * nearby the allocated block. So free will know
		 * how many bytes to deallocate.
		 */
		free(pyl);

	}
	else if (req.rtype == COAP_SMREQ_INVALID) {
		/* Buffer's empty, do not loop any more times,
		 * better to go on doing something else.
		 */
		//foundempty = 1;
	}
	else {
		LOGW("Cannot interpret SM request type");
		exit(1);
	}

	smcount++;
	free(req.pyl->data);
	free(req.pyl);

	/* Sleep for a while, not much actually. */
	struct timespec rqtp;
	sleep.tv_sec = 0;
	sleep.tv_nsec = 2000000; //1msec
	nanosleep(rqtp, NULL);

	}

	smcount=0;
	foundempty=0;

	} /*-----------------------------------------------------------------*/
}

/* token comparison
&& (!token || (token->length == s->token_length
	       && memcmp(token->s, s->token, token->length) == 0)) */

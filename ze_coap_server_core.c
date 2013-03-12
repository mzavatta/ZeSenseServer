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
#include "ze_timing.h"
#include "ze_coap_payload.h"
#include "ze_coap_resources.h"
#include "uthash.h"
#include "utlist.h"

ze_payload_t* form_data_payload(ze_sm_packet_t *packet);
ze_payload_t* form_sr_payload(coap_registration_t *reg);
uint64_t htonll(uint64_t value);
coap_tid_t test_socket_send(coap_context_t *context,
	       const coap_address_t *dst,
	       coap_pdu_t *pdu);
size_t s_strscpy(char *dest, const char *src, const size_t len);

void *
ze_coap_server_core_thread(void *args) {

	LOGI("Hello CoAP server thread! pid%d, tid%d", getpid(), gettid());

	struct coap_thread_args* ar = ((struct coap_thread_args*)(args));
	coap_context_t *cctx = ar->cctx;
	ze_sm_request_buf_t *smreqbuf = ar->smreqbuf;
	ze_sm_response_buf_t *notbuf = ar->notbuf;

	/* Not elegant but handy:
	 * Since most of the already made library function calls take
	 * coap_context_t and are unaware of our buffers,
	 * let's put references to our buffers inside coap_context_t
	 * so there's no need to change library function signatures.
	 * (for example, the GET POST etc.. handlers have a signature
	 * coap_context_t  *, struct coap_resource_t *, coap_address_t *,
	 * coap_pdu_t *, str *, coap_pdu_t *
	 * but they do need the SM buffer!)
	 *
	 * The Streaming Manager is tailored for the use of the buffers
	 * so we can give the references in a separate way.
	 */
	cctx->notbuf = notbuf;
	cctx->smreqbuf = smreqbuf;

	/* Switch on, off. */
	//pthread_exit(NULL);


	int firstSRsent = 0;

	fd_set readfds;
	struct timeval tv, *timeout;

	ze_sm_response_t req;
	coap_pdu_t *pdu = NULL;
	coap_async_state_t *asy = NULL, *tmp;
	//coap_tid_t asyt;
	coap_resource_t *res;
	coap_queue_t *nextpdu = NULL;
	coap_tick_t now;
	int result;
	int smcount = 0;

	coap_registration_t *reg;

	ze_payload_t *pyl = NULL, *srpyl = NULL;


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
	while ( nextpdu && nextpdu->t <= now  && !globalexit) {
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
			LOGI("Server layer received data from socket");
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
	while (smcount < SMREQ_RATIO && !globalexit) {

	/* Start by fetching an SM request and dispatch it
	 */
	req = get_coap_buf_item(notbuf);
	ze_sm_packet_t *reqpacket = (ze_sm_packet_t *)req.pk;
	/* Recall that the getter does not do any checkout not free
	 * Under this model only the CoAP server manages the ticket
	 */

	if (req.rtype == STREAM_STOPPED) {
		LOGI("Server layer got a STREAM STOPPED command");

		reg = (coap_registration_t *)(req.ticket);

		/* We're sure that no other notification will arrive
		 * with that ticket. Release it on behalf of the streaming
		 * manager. If there is no ongoing transaction, the registration
		 * associated to it will be destroyed.
		 */
		res = coap_get_resource_from_key(cctx, reg->reskey);
		coap_registration_release(res, reg);
	}
	else if (req.rtype == ONESHOT) {
		LOGI("Server layer got a SEND ASYNCH command");
		/* Lookup in the async register using the ticket tid..
		 * it shall find it..
		 */
		coap_tid_t tid = (coap_tid_t)(req.ticket);
		asy = coap_find_async(cctx, tid);
		if (asy != NULL) {

			pyl = form_data_payload(reqpacket);

			/* Need to add options in order... */
			pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
					coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
			coap_add_option(pdu, COAP_OPTION_TOKEN, asy->tokenlen, asy->token);
			coap_add_data(pdu, pyl->length, pyl->data);

			/* Send message. */
			if (req.conf == COAP_MESSAGE_CON) {
				LOGI("Server layer sending CON simple message");
				coap_send_confirmed(cctx, &(asy->peer), pdu);
			}
			else if (req.conf == COAP_MESSAGE_NON) {
				LOGI("Server layer ending NON simple message");
				coap_send(cctx, &(asy->peer), pdu);
				coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE);
				//free(pyl);
			}
			else LOGW("Server layer could not understand message type");

			/* Asynchronous request satisfied, regardless of whether
			 * a CON and an ACK will arrive, remove it. */
			coap_remove_async(cctx, asy->id, &tmp);
		}
		else LOGW("Server layer got oneshot sample but no asynch request matches the ticket");

		free(pyl->data);
		free(pyl);
	}
	else if (req.rtype == STREAM_NOTIFICATION) {
		LOGI("Server layer got a SEND NOTIF command");

		reg = (coap_registration_t *)(req.ticket);

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
		if (reg->fail_cnt <= COAP_OBS_MAX_FAIL) {

			/* Update timing info. */
			reg->ntptwin = reqpacket->ntpts;
			reg->rtptwin = reqpacket->rtpts;

			pyl = form_data_payload(reqpacket);

			/* Need to add options in order... */
			pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
					coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
			short st = htons(reg->notcnt);
			coap_add_option(pdu, COAP_OPTION_SUBSCRIPTION, sizeof(short), (unsigned char*)&(st));
			coap_add_option(pdu, COAP_OPTION_TOKEN, reg->token_length, reg->token);
			//coap_add_option(pdu, COAP_OPTION_STREAMING, 7, "chunked");
			coap_add_data(pdu, pyl->length, pyl->data);

			int sent = 1;
			if (reg->non_cnt >= COAP_OBS_MAX_NON || req.conf == COAP_MESSAGE_CON) {
				/* Either the max NON have been reached or
				 * we explicitly requested a CON.
				 * Send a CON and clean the NON counter
				 */
				LOGI("Server layer sending CON notification");
				pdu->hdr->type = COAP_MESSAGE_CON;
				coap_notify_confirmed(cctx, &(reg->subscriber), pdu,
						coap_registration_checkout(reg) );

				reg->non_cnt = 0;
			}
			else if (req.conf == COAP_MESSAGE_NON) {
				/* send a non-confirmable
				 * and increase the NON counter
				 * no need to keep the transaction state
				 */
				LOGI("Server layer sending NON notification");
				coap_send(cctx, &(reg->subscriber), pdu);

				reg->non_cnt++;

				// TODO free PDU when the send is a non confirmable one!
				//coap_pdu_clear(pdu, COAP_MAX_PDU_SIZE); //TODO: Might be useless..
				//free(pyl);
			}
			else {
				sent = 0;
				LOGW("Server layer could not understand message type.");
			}

			if(sent) {
				reg->notcnt++; //notcnt and packcount are not the same!, notcnt has a random start!
				reg->datapackcount++;
				reg->octcount+=pyl->length; //following RTP's RFC, only payload octects accounted

				/* May be time to send an RTCP packet..
				 * either bw threshold reached or first notification
				 * (need to send an RTCP asap) */
				if ( reg->octcount > (reg->last_sr_octcount + RTCP_SR_BANDWIDTH_THRESHOLD)
						|| reg->datapackcount==1) {

					srpyl = form_sr_payload(reg);
					if (srpyl==NULL)
						LOGW("form sr payload failed");
					/* Need to add options in order... */
					pdu = coap_pdu_init(req.conf, COAP_RESPONSE_205,
							coap_new_message_id(cctx), COAP_MAX_PDU_SIZE);
					short st = htons(reg->notcnt);
					coap_add_option(pdu, COAP_OPTION_SUBSCRIPTION, sizeof(short),(unsigned char*)&(st));
					coap_add_option(pdu, COAP_OPTION_TOKEN, reg->token_length, reg->token);
					//coap_add_option(pdu, COAP_OPTION_STREAMING, 7, "chunked");
					coap_add_data(pdu, srpyl->length, srpyl->data);

					reg->last_sr_octcount = reg->octcount;
					reg->last_sr_packcount = reg->datapackcount;

					//reg->subscriber->addr->sin->sin_port

					/* For testing purposes, mirror the first sender report
					 * also on another "link": different source and destination
					 * ports
					 */
					if (firstSRsent == 0) {
						coap_address_t tempaddr = reg->subscriber;
						tempaddr.addr.sin.sin_port = htons(DEST_PORT_TEST);
						LOGW("calling test_socket_send");
						test_socket_send(cctx, &(tempaddr), pdu);
						firstSRsent = 1;
					}

					/* Non confirmable. */
					coap_send(cctx, &(reg->subscriber), pdu);
					// TODO free PDU when the send is a non confirmable one!
				}
			}

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

		/* These are different from pyl because the
		 * coap_add_data(pdu, pyl ..) makes a copy,
		 * so free them in any case.
		 */
		free(pyl->data);
		free(pyl);
	}
	else if (req.rtype == INVALID_COMMAND) {
		/* Buffer's empty, do not loop any more times,
		 * better to go on doing something else.
		 */
		//foundempty = 1;
	}
	else {
		LOGW("Server layer cannot interpret upper layer command");
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

	LOGI("Total number of accel RR received:%d", accel_rr_received);

	coap_resource_t *s, *bku;
	coap_registration_t *sub;
	int subi = 1;
	LOGW("Command queue residual size:%d", notbuf->counter);
	HASH_ITER(hh, cctx->resources, s, bku) {
		char t[50];
		//s_strscpy(t, s->uri.s, s->uri.length);
		LOGI("Resource:%s", t);
		if (s->subscribers != NULL) {
			LL_FOREACH(s->subscribers, sub) {
				LOGI("Subscription %d", subi);
				LOGI("Data packet sent:%d", sub->datapackcount);
				subi++;
			}
		}
		subi = 1;
	}

	LOGI("CoAP server out of thread loop, returning..");
}

/* token comparison
&& (!token || (token->length == s->token_length
	       && memcmp(token->s, s->token, token->length) == 0)) */


ze_payload_t *
form_data_payload(ze_sm_packet_t *packet) {

	/*
	 ze_payload_header_t
	 timestamp 32bit (int)
	 packet->data,length
	 */

	ze_payload_t *c = malloc(sizeof(ze_payload_t));
	if (c==NULL) return NULL;

	int totlength = sizeof(ze_payload_header_t)+sizeof(int)+packet->length;
	c->data = malloc(totlength);
	if(c->data == NULL) return NULL;
	c->length = totlength;
	memset(c->data, 0, totlength);

	int offset = 0;
	unsigned char *p = c->data;

	ze_payload_header_t *hdr = (ze_payload_header_t*)(p);
	hdr->packet_type = DATAPOINT;
	hdr->sensor_type = packet->sensor;
	hdr->length = htons(totlength);

	offset = sizeof(ze_payload_header_t);
	p = p + offset;
	int rtpts = htonl(packet->rtpts);
	memcpy(p, &rtpts, sizeof(int));

	offset = sizeof(int);
	p = p + offset;
	memcpy(p, packet->data, packet->length);

	return c;
}

ze_payload_t *
form_sr_payload(coap_registration_t *reg) {

	/* Packet structure:
	 ze_payload_header_t
	 ze_payload_sr_t
	 cname, cnamelength
	 */

	ze_payload_t *c = malloc(sizeof(ze_payload_t));
	if (c==NULL) return NULL;

	int totlength = sizeof(ze_payload_header_t)+ZE_PAYLOAD_SR_LENGTH+CNAME_LENGTH;
	c->data = malloc(totlength);
	if(c->data == NULL) return NULL;
	c->length = totlength;
	memset(c->data, 0, totlength);

	int offset = 0;
	unsigned char *p = c->data;

	ze_payload_header_t *hdr = (ze_payload_header_t*)(p);
	hdr->packet_type = SENDREPORT;
	hdr->sensor_type = 0;
	hdr->length = htons(15);

	uint64_t ntpc = get_ntp();
	uint64_t ntp = htonll(ntpc);
/*
	unsigned char *t = &ntp;
	int i;
	for (i=0; i < 8; i++) 	LOGW("%c", *(t+i));
*/
	int ntpdiff = ntpc - reg->ntptwin;
	double ratio = (double)RTP_TSCLOCK_FREQ/1000000000LL;
	int rtpinc = (ratio)*(ntpdiff);
	LOGI("Sender report ntp=%lld, ntptwin=%lld, ntpdiff=%d, rtpinc=%d", ntpc, reg->ntptwin, ntpdiff, rtpinc);
	int ts = htonl(reg->rtptwin+rtpinc);
	int oc = htonl(reg->octcount);
	int pc = htonl(reg->datapackcount);

	offset = sizeof(ze_payload_header_t);
	p = p + offset;
	memcpy(p, &ntp, sizeof(uint64_t));

	offset = sizeof(uint64_t);
	p = p + offset;
	memcpy(p, &ts, sizeof(int));

	offset = sizeof(int);
	p = p + offset;
	memcpy(p, &pc, sizeof(int));

	offset = sizeof(int);
	p = p + offset;
	memcpy(p, &oc, sizeof(int));

	offset = sizeof(int);
	p = p + offset;
	memcpy(p, CNAME, CNAME_LENGTH);

	return c;
}

uint64_t
htonll(uint64_t value) {
	    int i = 1;
	    char *low = (char*) &i;
	    // if low contains 1, the system is big-endian
	    uint64_t vs = value;
	    uint64_t vd;
	    unsigned char *s = (unsigned char *)&vs;
	    unsigned char *d = (unsigned char *)&vd;
	    if (*low == 1) {
	    	//big endian, need to turn
	    	d[0] = s[7];
	    	d[1] = s[6];
	    	d[2] = s[5];
	    	d[3] = s[4];
	    	d[4] = s[3];
	    	d[5] = s[2];
	    	d[6] = s[1];
	    	d[7] = s[0];
	    	return vd;
	    }
	    else return value;
}


coap_tid_t
test_socket_send(coap_context_t *context,
	       const coap_address_t *dst,
	       coap_pdu_t *pdu) {

  ssize_t bytes_written;
  coap_tid_t id = COAP_INVALID_TID;

  if ( !context || !dst || !pdu )
    return id;

  bytes_written = sendto( context->sockfdtest, pdu->hdr, pdu->length, 0,
			  &dst->addr.sa, dst->size);

  if (bytes_written >= 0) {
	  LOGW("--- Sent packet TEST SOCKET -----------");
	  //printpdu(pdu);
	  LOGW("---------------------------");
    coap_transaction_id(dst, pdu, &id);
  } else {
    LOGW("test socket send problem");
  }

  return id;
}

size_t
s_strscpy(char *dest, const char *src, const size_t len)
{
    /* Copy the contents from src to dest */
    size_t i = 0;
    for(i = 0; i < len; i++)
    *dest++ = *src++;

    /* Null terminate dest */
     *dest++ = '\0';

    return i;
}

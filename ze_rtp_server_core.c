/* ZeSense
 * --  Very simple RTP server main loop
 *
 * Author: Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 */

#include "rtp_net.h"
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "ze_streaming_manager.h"

#define RTP_PAYLOAD_TYPE 80 //unassigned 35-71 and 77-95

#define RTP_TSCLOCK_FREQ 100

#define RTCP_SR_BANDWIDTH_THRESHOLD 1000
/*
 * Create the streams records that we want to send
 * (init function for the stream record here is useful)
 *
 * Start the streams by creating a ticket and requesting
 * the streaming manager to begin.
 *
 * ---loop---
 *
 * Look at the socket.
 * We will mostly receive RTCP's RRs, so update the stream information.
 * Matching of the RR to the stream record is done through SSRC.
 * The CNAME should be the same for each participant.
 *
 * Take samples from the notification buffer and send.
 * Periodically check for the stream status and send RTCP accordingly.
 *
 * ---loop---
 */


int cc(rtp_context_t rctx, ze_coap_request_buf_t *notbuf, ze_sm_request_buf_t *smreqbuf) {

	fd_set readfds;
	struct timeval tv, *timeout;
	int result;

	int smcount = 0;

	int freq = 20;

	rtp_stream_t str;

	/* --- create a stream and start it in the Streaming Manager. */
	str = rtp_stream_init();
	if (str==NULL) return -1;
	LL_APPEND(rctx->streams, str);
	str->lasthdr->ssrc = rand();
	str->lasthdr->seqn = rand();
	str->lasthdr->timestamp = rand();
	put_sm_buf_item(smreqbuf, SM_REQ_START, ASENSOR_TYPE_ACCELEROMETER,
			(coap_ticket_t)str, freq);


	//TODO handle participant timeout


	while(1) {

		FD_ZERO(&readfds);
		FD_SET( rctx->sockfd, &readfds );

		tv.tv_usec = 10000; //10msec
		tv.tv_sec = 0;
		timeout = &tv;

		result = select( FD_SETSIZE, &readfds, 0, 0, timeout );

		if ( result < 0 ) {	/* error */
			if (errno != EINTR)
				perror("select");
		} else if ( result > 0 ) {	/* read from socket */
			if ( FD_ISSET( rctx->sockfd, &readfds ) ) {
				LOGI("RTP Something appeared on the socket, start reading..");
				/* READ AND DO STUFF
				 * We'll likely be reading only RTCP packets. */
			}
		} else {	/* timeout */
			//LOGI("select timed out..");
		}


		while (smcount < SMREQ_RATIO) {

			/* Start by fetching an SM request and dispatch it
			 */
			req = get_coap_buf_item(notbuf);
			/* Recall that the getter does not do any checkout not free
			 * Under this model only the CoAP server manages the ticket
			 */

			if (req.rtype == STREAM_STOPPED) {
				LOGI("CS Got a STREAM STOPPED command");
				/* We're sure that no other notification will arrive
				 * with that ticket.
				 * We destroy the stream record, nobody holds its pointer
				 * anymore. */
				LL_DELETE(rctx->streams, req.ticket.str);
			}
			else if (req.rtype == ONESHOT) {
				LOGI("CS Got a SEND ASYNCH command");
				/* Maybe not meaningful in our case. */
			}
			else if (req.rtype == STREAM_NOTIFICATION) {
				LOGI("CS Got a SEND NOTIF command");

				/* Here I have to check if the registration
				 * that corresponds to the ticket is still
				 * valid. It may be, indeed, that the stream
				 * has stopped but some previous send commands
				 * are still in the buffer.
				 * What do we do with its ticket? As we don't
				 * pass it along to anybody and the official
				 * destruction will arrive later through a
				 * STREAM STOPPED confirmation, just forget
				 * about it.
				 */
				if ( !req.ticket.str->invalid ) {
					/* Here we send the RTP packet and then check the byte count
					 * or the packet count. Every some time we send an
					 */

					/*
					typedef struct ze_payload_t {
						int64_t wts;
						unsigned int rtpts;
						int length;
						unsigned char *data;
					} ze_payload_t;*/

					/* Cook PDU. */
					rtp_pdu_t pdu = rtp_pdu_init(0, 0, req.pyl->length);
					//todo bitwise
					pdu->hdr->marker = 0;
					pdu->hdr->ptype = RTP_PAYLOAD_TYPE;
					pdu->hdr->seqn = req.ticket.str->lasthdr->seqn++;
					pdu->hdr->ssrc = req.ticket.str->lasthdr->ssrc;
					pdu->hdr->timestamp = ; //TODO

					req.ticket.str->lastsamplentp = req.pyl->wts;

					rtp_send_impl(rctx, &req.ticket.str->dest, pdu);

					memcpy(req.ticket.str->lasthdr, pdu->hdr, sizeof(rtp_hdr_t));

					req.ticket.str->octectcount+=req.pyl->length;
					req.ticket.str->packetcount++;

					/*
					 * Here we decide when to report based on the bandwidth. Although Perkins 2003
					 * "The interval varies according to the media format in use and the size
					 * of the session; typically it is on the order of 5 seconds for small sessions,
					 * but it can increase to several minutes for very large groups"
					 */
					if (req.ticket.str->packetcount == 1 ||
							req.ticket.str->octectcount >
								req.ticket.str->last_rtcp_octectcount + RTCP_SR_BANDWIDTH_THRESHOLD) {
						//send RTCP SR
						rtcp_pdu_t *sr = build_minimal_sr(rctx->cname, rctx->cnamel, req.ticket.str->lasthdr->ssrc);
						rtcp_sr_t *srtemp = (rtcp_sr_t)(sr->hdr+srsizeof(rtcp_hdr_t));

						srtemp->ntpts = get_ntp();
						/* The RTP timestamp corresponds to the same instant as the NTP timestamp,
						 * but it is expressed in the units of the RTP media clock.
						 * The value is generally not the same as the RTP timestamp of the
						 * previous data packet, because some time will have elapsed since the
						 * data in that packet was sampled. (Perkins 2003)
						 */
						long ntpdiff = srtemp->ntpts - req.ticket.str->lastsamplentp;
						float freqdiv = (float)RTP_TSCLOCK_FREQ/1000000000LL;
						//TODO, check the rounding that is performed
						srtemp->rtpts = req.ticket.str->lasthdr->timestamp +
								(int)(ntpdiff*freqdiv);

						srtemp->ocount = req.ticket.str->octectcount;
						srtemp->pcount = req.ticket.str->packetcount;

						rtp_send_impl(&req.ticket.str->dest, sr);
					}

				}
				else LOGI("Not sending notification, registration already invalid.");

			}
			else if (req.rtype == INVALID_COMMAND) {
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



	return 0;
}

long get_ntp() {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	long ntp = (t.tv_sec*1000000000LL)+t.tv_nsec;
	return ntp;
}

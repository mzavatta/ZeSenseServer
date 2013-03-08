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
#include <arpa/inet.h>

#include "ze_streaming_manager.h"
#include "ze_sm_resbuf.h"
//#include "ze_coap_server_core.h"
#include "ze_timing.h"
#include "ze_rtp_server_core.h"
#include "ze_coap_server_root.h"
#include "rtp_net.h"
#include "utlist.h"

#define RTP_SESSION_START 1
#define RTP_DEFAULT_VERSION 2

#define RTCP_MAX_LENGTH 100

#define SMREQ_RATIO 5

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

int interpret_sdes(unsigned char *chunk, int length);
int interpret_rr(unsigned char *chunk, int length);
int rtp_read_dispatch(rtp_context_t *rctx);
//uint64_t get_ntp();


void *
ze_rtp_server_core_thread(void *args) {

	struct rtp_thread_args *ar = ((struct rtp_thread_args*)(args));
	rtp_context_t *rctx = ar->rctx;
	ze_sm_request_buf_t *smreqbuf = ar->smreqbuf;
	ze_sm_response_buf_t *notbuf = ar->notbuf;

	fd_set readfds;
	struct timeval tv, *timeout;
	int result;

	int smcount = 0;

	int freq = 20;

	rtp_stream_t *str;

	ze_sm_response_t req;

	int startsession = 0;
	while (!startsession) {

	FD_ZERO(&readfds);
	FD_SET( rctx->sockfd, &readfds );
	result = select( FD_SETSIZE, &readfds, 0, 0, NULL);
	if ( result < 0 ) {	/* error */
		if (errno != EINTR)
			perror("select");
	} else if ( result > 0 ) {	/* read from socket */
		if ( FD_ISSET( rctx->sockfd, &readfds ) ) {
			LOGI("RTP Something appeared on the socket, start reading..");
			int ss = rtp_read_dispatch(rctx);
			if (ss == 2) {
				startsession = 1;
			}
		}
	} else {	/* timeout */
		//LOGI("select timed out..");
	}

	}

	/* --- create a stream and start it in the Streaming Manager.
	 * TODO for the moment the destination can only be one and is stored
	 * in a system wide variable. */
	str = rtp_stream_init();
	if (str==NULL) return -1;
	LL_APPEND(rctx->streams, str);
	str->ssrc = rand();
	str->seqn = rand();
	str->dest = rctx->dst;
	put_sm_buf_item(smreqbuf, SM_REQ_START, ASENSOR_TYPE_ACCELEROMETER,
			(ticket_t)str, freq);


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
			ze_sm_packet_t *reqpacket = (ze_sm_packet_t *)req.pk;

			if (req.rtype == STREAM_STOPPED) {
				LOGI("CS Got a STREAM STOPPED command");
				str = (rtp_stream_t *)req.ticket;
				/* We're sure that no other notification will arrive
				 * with that ticket.
				 * We destroy the stream record, nobody holds its pointer
				 * anymore. */
				LL_DELETE(rctx->streams, str);
			}
			else if (req.rtype == ONESHOT) {
				LOGI("CS Got a SEND ASYNCH command");
				/* Maybe not meaningful in our case. */
			}
			else if (req.rtype == STREAM_NOTIFICATION) {
				LOGI("CS Got a SEND NOTIF command");

				str = (rtp_stream_t *)req.ticket;

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
				if ( !str->invalid ) {
					/* Here we send the RTP packet and then check the byte count
					 * or the packet count. Every some time we send an RTCP packet;
					 */

					str->ntptwin = reqpacket->ntpts;
					str->rtptwin = reqpacket->rtpts;

					/* Cook PDU. */
					rtp_pdu_t *pdu = rtp_pdu_init(0, 0, reqpacket->length+sizeof(rtp_payload_hdr_t));
					pdu->hdr->padding = 0;
					//extension assigned by rtp_pdu_init
					//CSRC count as well
					pdu->hdr->marker = 0;
					pdu->hdr->ptype = RTP_PAYLOAD_TYPE;
					pdu->hdr->seqn = htons(str->seqn++);
					pdu->hdr->timestamp = htonl(reqpacket->rtpts);
					pdu->hdr->ssrc = htonl(str->ssrc);
					rtp_payload_hdr_t *pht = (rtp_payload_hdr_t*)pdu->data;
					pht->sensor = htonl(reqpacket->sensor);
					pht = pht + sizeof(rtp_payload_hdr_t);
					memcpy(pht, reqpacket->data, reqpacket->length);

					rtp_send_impl(rctx, &str->dest, pdu);

					str->octectcount+=reqpacket->length;
					str->packetcount++;

					/*
					 * Here we decide when to report based on the bandwidth. Although Perkins 2003
					 * "The interval varies according to the media format in use and the size
					 * of the session; typically it is on the order of 5 seconds for small sessions,
					 * but it can increase to several minutes for very large groups"
					 */
					if (str->packetcount == 1 ||
							str->octectcount >
								str->last_rtcp_octectcount + RTCP_SR_BANDWIDTH_THRESHOLD) {
						//send RTCP SR
						rtcp_pdu_t *sr = build_minimal_sr(rctx->cname, rctx->cnamel, str->ssrc);
						/* left to add ntp, rtp, oc, pc. */
						rtcp_sr_t *srtemp = (rtcp_sr_t *)(sr->hdr+sizeof(rtcp_hdr_t));

						srtemp->ntpts = get_ntp();
						/* The RTP timestamp corresponds to the same instant as the NTP timestamp,
						 * but it is expressed in the units of the RTP media clock.
						 * The value is generally not the same as the RTP timestamp of the
						 * previous data packet, because some time will have elapsed since the
						 * data in that packet was sampled. (Perkins 2003)
						 */
						uint64_t ntpdiff = srtemp->ntpts - str->ntptwin;
						float freqdiv = (float)RTP_TSCLOCK_FREQ/1000000000LL;
						//TODO, check the rounding that is performed
						srtemp->rtpts = str->rtptwin +
								(int)(ntpdiff*freqdiv);

						srtemp->ocount = str->octectcount;
						srtemp->pcount = str->packetcount;

						rtcp_send_impl(rctx, &str->dest, sr);
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
/*
uint64_t get_ntp() {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	uint64_t ntp = (t.tv_sec*1000000000LL)+t.tv_nsec;
	return ntp;
}*/

/* Big mess of the RTCP parsing.. */
int
rtp_read_dispatch(rtp_context_t *rctx) {

	  static char buf[RTCP_MAX_LENGTH];

	  ssize_t bytes_read = -1;
	  rtp_address_t src, dst;

	  rtp_address_init(&src);

	  bytes_read = recvfrom(rctx->sockfd, buf, sizeof(buf), 0,
				&src.addr.sa, &src.size);

	  if ( bytes_read < 0 ) {
	    LOGW("error in recvfrom");
	    return -1;
	  }

	  if ( (size_t)bytes_read == 0 ) {
	    LOGW("RTP discarded too short frame\n" );
	    return -1;
	  }

	  /* Dispatch. */
	  if(buf[0]==0x0F && bytes_read==1) {
		  rctx->dst = src;
		  return 2;
	  }
	  else {
		  // The received packet will be a reception report, likely..
		  rtcp_hdr_t *rrtemp = (rtcp_hdr_t *)buf;
		  if (rrtemp->version != RTP_DEFAULT_VERSION ||
				  rrtemp->ptype!=201) {
			  LOGW("cannot interpret incoming RTCP or RR, drop");
			  return -1;
		  }
		  int rrlength = (rrtemp->length+1)*4;
		  /* Advance of the amount suggested by rrlength. */
		  rtcp_sdes_t *sdestemp = (rtcp_sdes_t *)(rrtemp + rrlength);
		  if (sdestemp->version != RTP_DEFAULT_VERSION ||
				  sdestemp->ptype!=202)  {
			  LOGW("cannot interpret incoming SDES, drop");
			  return -1;
		  }
		  int sdeslength = (sdestemp->length+1)*4;

		  unsigned char *rr = malloc(rrlength);
		  if (rr==NULL) return -1;
		  memcpy(rr, rrtemp, rrlength);
		  interpret_rr(rr, rrlength);

		  unsigned char *sdes = malloc(sdeslength);
		  if (sdes==NULL) return -1;
		  memcpy(sdes, sdestemp, sdeslength);
		  interpret_sdes(sdes, sdeslength);
	  }
	  return 0;
}

/*
 * Generic parsing is definitely not easy,
 * header validation is described in RFC3550 appendix 2.
 * limit ourselves to a RR1 + SDES-CNAME only.
 */
int
interpret_rr(unsigned char *chunk, int length) {
	rtcp_hdr_t *hdr = (rtcp_hdr_t *)chunk;
	LOGI("v:%d, p:%d, rc:%d, pt:%d, length:%d",
			hdr->version, hdr->padding, hdr->rc, hdr->ptype, hdr->length);
	LOGI("SSRC sender: %d", hdr->ssrc);
	if (hdr->rc>1) {
		LOGW("We got a RR with more than one block");
		return -1;
	}
	rtcp_rr_t *rr = (rtcp_rr_t *)hdr + sizeof(rtcp_hdr_t);
	LOGI("SSRC_1: %d", rr->ssrcn);
	LOGI("frac lost: %d", rr->fraclost);
	LOGI("cumlost: %d", rr->cumlost);
	LOGI("ext highest sn rec: %d", rr->ehsnr);
	LOGI("inter jitter: %d", rr->interjitter);
	LOGI("last SR: %d", rr->lsr);
	LOGI("delay since last SR: %d", rr->dlsr);

	return 0;
}

int
interpret_sdes(unsigned char *chunk, int length) {
	rtcp_sdes_t *hdr = (rtcp_sdes_t *)chunk;
	LOGI("v:%d, p:%d, sc:%d, pt:%d, length:%d",
			hdr->version, hdr->padding, hdr->sc, hdr->ptype, hdr->length);
	if (hdr->sc>1) {
		LOGW("We got a SDES with more than one block");
		return -1;
	}

	int *chunkssrc = (int*)(hdr+sizeof(rtcp_sdes_t));
	LOGI("sdes ssrc:%d", *chunkssrc);

	rtcp_itemhead_t *item = (rtcp_itemhead_t *)(hdr+sizeof(rtcp_sdes_t)+4);
	LOGI("itemcode:%d, length:%d", item->code, item->length);

	unsigned char *cnstr = (unsigned char *)(item + sizeof(rtcp_itemhead_t));
	unsigned char *s = malloc(item->length+1);
	memcpy(s, cnstr, item->length);
	memset(s+item->length, "\0", 1);
	LOGI("CNAME:%s",s);

	return 0;
}

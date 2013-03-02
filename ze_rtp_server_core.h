/* ZeSense
 * --  Very minimal RTP server
 *
 * Author: Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 */

#ifndef ZE_RTP_SERVER_CORE_H
#define ZE_RTP_SERVER_CORE_H

/* Unassigned in IANA RTP register 35-71 and 77-95. */
#define RTP_PAYLOAD_TYPE 80

void *
ze_rtp_server_core_thread(void *args);


#endif /* ZE_RTP_SERVER_CORE_H_ */

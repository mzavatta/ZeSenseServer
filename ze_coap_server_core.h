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

#ifndef ZE_COAP_SERVER_CORE_H
#define ZE_COAP_SERVER_CORE_H

#include "ze_coap_server_root.h"
#include "ze_coap_reqbuf.h"

/* Request codes from the Sensor Manager. */
#define COAP_SEND_NOTIF			50
#define COAP_SEND_ASYNCH		60
#define COAP_STREAM_STOPPED		70
#define COAP_SMREQ_INVALID		40

#endif

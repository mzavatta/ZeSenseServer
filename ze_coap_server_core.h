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



#define SMREQ_RATIO				5

#include "net.h"
#include "ze_sm_resbuf.h"
#include "ze_sm_reqbuf.h"

void *
ze_coap_server_core_thread(void *args);

#endif

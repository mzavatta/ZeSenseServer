/*
 * ZeSense CoAP server
 * -- resource-specific method handlers
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 *
 * Built using libcoap by
 * Olaf Bergmann <bergmann@tzi.org>
 * http://libcoap.sourceforge.net/
 */

// the four request handler methods
// the two observation handler methods

/*
 * init method for registration in the CoAP server
 */


#ifndef ZE_COAP_RESOURCES_H
#define ZE_COAP_RESOURCES_H

#include "coap.h"
#include "ze_sm_reqbuf.h"

void ze_coap_init_resources(coap_context_t *context);


// Accelerometer
coap_resource_t * ze_coap_init_accel();
void accel_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);


#endif

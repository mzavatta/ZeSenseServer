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

#include "pdu.h"
#include "net.h"
#include "resource.h"

void ze_coap_init_resources(coap_context_t *context);

int accel_rr_received;

/*--------- Accelerometer --------------------------------------------------*/
coap_resource_t *
ze_coap_init_accel();

void
accel_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);

void
accel_POST_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);

void
accel_on_unregister(coap_context_t *ctx, coap_registration_t *reg);
/*-------------------------------------------------------------------------*/

/*--------- Location --------------------------------------------------*/
coap_resource_t *
ze_coap_init_location();

void
location_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);

void
location_on_unregister(coap_context_t *ctx, coap_registration_t *reg);
/*-------------------------------------------------------------------------*/

/*--------- Proximity --------------------------------------------------*/
coap_resource_t *
ze_coap_init_proximity();

void
proximity_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);

void
proximity_on_unregister(coap_context_t *ctx, coap_registration_t *reg);
/*-------------------------------------------------------------------------*/

/*--------- Light ---------------------------------------------------------*/
coap_resource_t * ze_coap_init_light();

void light_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);

void light_on_unregister(coap_context_t *ctx, coap_registration_t *reg);
/*---------------------------------------------------------------------------*/
/*--------- Orientation -----------------------------------------------------*/
coap_resource_t *ze_coap_init_orient();

void orient_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);
void orient_on_unregister(coap_context_t *ctx, coap_registration_t *reg);
/*----------------------------------- Gyroscope --------------------------------------------*/
coap_resource_t * ze_coap_init_gyro();

void gyro_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response);
void gyro_on_unregister(coap_context_t *ctx, coap_registration_t *reg);
/*--------- Generics --------------------------------------------------*/
void
generic_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response,
	      int sensor);
void
generic_POST_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response,
	      int sensor);

void
generic_on_unregister(coap_context_t *ctx, coap_registration_t *reg,
		  int sensor);
/*-------------------------------------------------------------------------*/


#endif

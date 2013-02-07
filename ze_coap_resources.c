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
#include "ze_log.h"
#include "ze_coap_resources.h"
#include "ze_sm_reqbuf.h"
#include "ze_streaming_manager.h"
#include "async.h"

void
ze_coap_init_resources(coap_context_t *context) {

	LOGI("Initializing resources..");

	coap_resource_t *r = NULL;

	r = ze_coap_init_accel();
	coap_add_resource(context, r);
	r = NULL;

	/* Other resources to follow... */
}


coap_resource_t *
ze_coap_init_accel() {

	LOGI("Initializing accel..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"accel", 5, 0);
	coap_register_handler(r, COAP_REQUEST_GET, accel_GET_handler);
	//coap_register_handler(r, COAP_REQUEST_PUT, hnd_put_time);
	//coap_register_handler(r, COAP_REQUEST_DELETE, hnd_delete_time);

	/* Need to register on_unregister() handler. */
	r->on_unregister = &accel_on_unregister;

	r->observable = 1;

	/*
	coap_add_attr(r, (unsigned char *)"ct", 2, (unsigned char *)"0", 1, 0);
	coap_add_attr(r, (unsigned char *)"title", 5, (unsigned char *)"\"Internal Clock\"", 16, 0);
	coap_add_attr(r, (unsigned char *)"rt", 2, (unsigned char *)"\"Ticks\"", 7, 0);
	coap_add_attr(r, (unsigned char *)"if", 2, (unsigned char *)"\"clock\"", 7, 0);
	*/

	return r;
}


void
accel_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	LOGI("Recognized accelerometer GET request, entered handler!");

	coap_opt_iterator_t opt_iter;
	coap_opt_t *obopt;
	coap_registration_t *reg;
	coap_async_state_t *asy;

	/* TODO
	 * Instead of setting 20Hz by default
	 * interpret parameters in the request query
	 * string
	 */
	int freq = 20;

	obopt = coap_check_option(request, COAP_OPTION_SUBSCRIPTION, &opt_iter);
	if (obopt != NULL) { //There is an observe option

		if (resource->observable == 1) {

			/* The returned pointer is either a new pointer or an
			 * existing one. The reference counter is not incremented
			 * by coap_add_registration(), not even for the reference
			 * that is held by the sibling in the registration list.
			 * The destructor takes care of unplugging the registration
			 * from the list correctly.
			 */
			reg = coap_add_registration(resource, peer, token);

			/* The ticket gets created by this call. Whether or not a new registration
			 * has been registered or an existing one replaced/updated,
			 * we issue a new ticket. The Streaming Manager commits to tell us its
			 * destruction. If the reasoning is correct, in this latter case the
			 * Streaming Manager will most likely find a stream with the same ticket,
			 * unless he's given it up spontaneously for some strange reason,
			 * in which case there might be a STREAM STOPPED message on the fly,
			 * either still in the other thread's body or in the other queue.. */
			put_sm_buf_item(context->smreqbuf, SM_REQ_START, ASENSOR_TYPE_ACCELEROMETER,
					(coap_ticket_t)coap_registration_checkout(reg), freq);


			if (request->hdr->type == COAP_MESSAGE_CON) {
				/* Prepare response as simple ACK, without observe option
				 * A default ACK has been prepared by the caller of this GET handler
				 * so just don't touch it
				 */
			}
			else if (request->hdr->type == COAP_MESSAGE_NON) {
				/* Should send nothing in reply at the messaging layer
				 * (we'll send the response later)
				 * NULL the proposed response container, so that
				 * the response that is passed back to the caller
				 * is discarded. (my modification to libcoap)
				 */
				response = NULL;
			}

			/*
			 * Not releasing because we haven't checked it out in this
			 * routine. Well we do but it's the checkout for the Streaming
			 * Manager, which shall only be released when it confirms that
			 * its destruction back to us. */
			//coap_registration_release(reg);
		}
		else {
			/* As from draft-coap-observe par4.1 suggestion "unable or unwilling",
			 * ask one-shot representation to SM.
			 */
			asy = coap_register_async(context, peer, request,
					COAP_ASYNC_SEPARATE, NULL);

			put_sm_buf_item(context->smreqbuf, SM_REQ_ONESHOT, ASENSOR_TYPE_ACCELEROMETER,
					(coap_ticket_t)(asy->id), 0);
		}
	}

	else { //There isn't an observe option

		/* Ask a regular oneshot representation. */
		asy = coap_register_async(context, peer, request,
				COAP_ASYNC_SEPARATE, NULL);

		put_sm_buf_item(context->smreqbuf, SM_REQ_ONESHOT, ASENSOR_TYPE_ACCELEROMETER,
				(coap_ticket_t)asy->id, 0);

		/* As per CoAP observer draft, clear this registration.
		 * This must be done through the streaming manager
		 * SM_REQ_STOP, passing the ticket of the registration.
		 * It's the resource-specific on_unregister()'s duty.
		 */
		reg = coap_find_registration(resource, peer);
		if (reg != NULL)
			resource->on_unregister(context, reg);
	}

	return;
}

void
accel_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {

	LOGI("Accelerometer on_unregister entered..");

	/*
	 * Unregistration must be done through the streaming manager
	 * SM_REQ_STOP, passing the ticket of the registration.
	 * The streaming manager will confirm the
	 * cancellation, then the reference count will be decremented
	 * by the server and if zero the registration destroyed.
	 * If the streaming manager does not have any stream
	 * with that ticket (should not happen) it confirms the cancellation anyways.
	 */
	put_sm_buf_item(ctx->smreqbuf, SM_REQ_STOP, ASENSOR_TYPE_ACCELEROMETER,
			(coap_ticket_t)coap_registration_checkout(reg), 0);

}

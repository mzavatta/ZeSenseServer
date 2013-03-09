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

	r = ze_coap_init_location();
	coap_add_resource(context, r);
	r = NULL;

	r = ze_coap_init_proximity();
	coap_add_resource(context, r);
	r = NULL;

	r = ze_coap_init_light();
	coap_add_resource(context, r);
	r = NULL;

	r = ze_coap_init_orient();
	coap_add_resource(context, r);
	r = NULL;

	r = ze_coap_init_gyro();
	coap_add_resource(context, r);
	r = NULL;

	/* Other resources to follow... */
}

/*------------------------------ Accelerometer -------------------------------------------*/
coap_resource_t *
ze_coap_init_accel() {

	LOGI("Initializing accel..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"accel", 5, 0);
	coap_register_handler(r, COAP_REQUEST_GET, accel_GET_handler);
	coap_register_handler(r, COAP_REQUEST_POST, accel_POST_handler);
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
accel_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {


	LOGI("Recognized accelerometer GET request, entered handler!");

	generic_GET_handler(context, resource, peer, request, token, response,
			ASENSOR_TYPE_ACCELEROMETER);

}

void
accel_POST_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	generic_POST_handler(context, resource, peer, request, token, response,
			ASENSOR_TYPE_ACCELEROMETER);

}

void
accel_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {

	LOGI("Proximity on_unregister entered..");

	generic_on_unregister(ctx, reg, ASENSOR_TYPE_ACCELEROMETER);
}

/*-------------------------------- Location ---------------------------------------------*/

coap_resource_t *
ze_coap_init_location() {

	LOGI("Initializing location..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"location", 9, 0);
	coap_register_handler(r, COAP_REQUEST_GET, location_GET_handler);
	//coap_register_handler(r, COAP_REQUEST_PUT, hnd_put_time);
	//coap_register_handler(r, COAP_REQUEST_DELETE, hnd_delete_time);

	/* Need to register on_unregister() handler. */
	r->on_unregister = &location_on_unregister;

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
location_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	LOGI("Recognized location GET request, entered handler!");

	generic_GET_handler(context, resource, peer, request, token, response,
			ZESENSE_SENSOR_TYPE_LOCATION);

}

void
location_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {

	LOGI("Location on_unregister entered..");

	generic_on_unregister(ctx, reg, ZESENSE_SENSOR_TYPE_LOCATION);
}

/*-------------------------------- Proximity ---------------------------------------------*/

coap_resource_t *
ze_coap_init_proximity() {

	LOGI("Initializing proximity..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"proximity", 9, 0);
	coap_register_handler(r, COAP_REQUEST_GET, proximity_GET_handler);
	//coap_register_handler(r, COAP_REQUEST_PUT, hnd_put_time);
	//coap_register_handler(r, COAP_REQUEST_DELETE, hnd_delete_time);

	/* Need to register on_unregister() handler. */
	r->on_unregister = &proximity_on_unregister;

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
proximity_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	LOGI("Recognized proximity GET request, entered handler!");

	generic_GET_handler(context, resource, peer, request, token, response,
			ASENSOR_TYPE_PROXIMITY);

}

void
proximity_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {


	LOGI("Proximity on_unregister entered..");

	generic_on_unregister(ctx, reg, ASENSOR_TYPE_PROXIMITY);
}

/*-------------------------------- Light ---------------------------------------------*/

coap_resource_t *
ze_coap_init_light() {

	LOGI("Initializing light..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"light", 5, 0);
	coap_register_handler(r, COAP_REQUEST_GET, light_GET_handler);
	//coap_register_handler(r, COAP_REQUEST_PUT, hnd_put_time);
	//coap_register_handler(r, COAP_REQUEST_DELETE, hnd_delete_time);

	/* Need to register on_unregister() handler. */
	r->on_unregister = &light_on_unregister;

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
light_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	LOGI("Recognized light GET request, entered handler!");

	generic_GET_handler(context, resource, peer, request, token, response,
			ASENSOR_TYPE_LIGHT);

}

void
light_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {


	LOGI("Light on_unregister entered..");

	generic_on_unregister(ctx, reg, ASENSOR_TYPE_LIGHT);
}

/*----------------------------------- Orientation --------------------------------------------*/
coap_resource_t *
ze_coap_init_orient() {

	LOGI("Initializing orientation..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"orientation", 11, 0);
	coap_register_handler(r, COAP_REQUEST_GET, orient_GET_handler);
	//coap_register_handler(r, COAP_REQUEST_PUT, hnd_put_time);
	//coap_register_handler(r, COAP_REQUEST_DELETE, hnd_delete_time);

	/* Need to register on_unregister() handler. */
	r->on_unregister = &orient_on_unregister;

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
orient_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	LOGI("Recognized orientation GET request, entered handler!");

	generic_GET_handler(context, resource, peer, request, token, response,
			ZESENSE_SENSOR_TYPE_ORIENTATION);

}

void
orient_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {

	LOGI("Orientation on_unregister entered..");

	generic_on_unregister(ctx, reg, ZESENSE_SENSOR_TYPE_ORIENTATION);
}
/*----------------------------------- Gyroscope --------------------------------------------*/
coap_resource_t *
ze_coap_init_gyro() {

	LOGI("Initializing gyroscope..");

	coap_resource_t *r;

	r = coap_resource_init((unsigned char *)"gyroscope", 9, 0);
	coap_register_handler(r, COAP_REQUEST_GET, gyro_GET_handler);
	//coap_register_handler(r, COAP_REQUEST_PUT, hnd_put_time);
	//coap_register_handler(r, COAP_REQUEST_DELETE, hnd_delete_time);

	/* Need to register on_unregister() handler. */
	r->on_unregister = &gyro_on_unregister;

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
gyro_GET_handler(coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response) {

	LOGI("Recognized gyroscope GET request, entered handler!");

	generic_GET_handler(context, resource, peer, request, token, response,
			ASENSOR_TYPE_GYROSCOPE);

}

void
gyro_on_unregister(coap_context_t *ctx, coap_registration_t *reg) {

	LOGI("Gyroscope on_unregister entered..");

	generic_on_unregister(ctx, reg, ASENSOR_TYPE_GYROSCOPE);
}
/*------------------------------- Generics -----------------------------------------------*/
void
generic_GET_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response,
	      int sensor) {

	coap_opt_iterator_t opt_iter;
	coap_opt_t *obopt;
	coap_registration_t *reg;
	coap_async_state_t *asy;

	/* TODO
	 * Instead of setting 5Hz by default
	 * interpret parameters in the request query
	 * string
	 */
	int freq = 5;

	obopt = coap_check_option(request, COAP_OPTION_SUBSCRIPTION, &opt_iter);
	if (obopt != NULL) { //There is an observe option

		if (resource->observable == 1) {

			LOGI("Observe option seen and resource observable.");

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
			put_sm_buf_item(context->smreqbuf, SM_REQ_START, sensor,
					(ticket_t)coap_registration_checkout(reg), freq);


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

			LOGI("Observe option seen but resource unobservable.");

			/* As from draft-coap-observe par4.1 suggestion "unable or unwilling",
			 * ask one-shot representation to SM.
			 */
			asy = coap_register_async(context, peer, request,
					COAP_ASYNC_SEPARATE, NULL);

			put_sm_buf_item(context->smreqbuf, SM_REQ_ONESHOT, sensor,
					(ticket_t)(asy->id), 0);

			/*
			 * Do not unregister since if the resource in not observable
			 * there can be no streams attached to it.
			 */
		}
	}

	else { //There isn't an observe option

		LOGI("No observe option is seen, simple oneshot request..");

		/* Ask a regular oneshot representation. */
		asy = coap_register_async(context, peer, request,
				COAP_ASYNC_SEPARATE, NULL);

		put_sm_buf_item(context->smreqbuf, SM_REQ_ONESHOT, sensor,
				(ticket_t)asy->id, 0);

		/* As per CoAP observer draft, clear this registration.
		 * This must be done through the streaming manager
		 * SM_REQ_STOP, passing the ticket of the registration.
		 * It's the resource-specific on_unregister()'s duty.
		 */
		reg = coap_find_registration(resource, peer);
		if (reg != NULL)
			resource->on_unregister(context, reg);
		else LOGI("No registration found though.");
	}

	return;
}

void
generic_POST_handler (coap_context_t  *context, struct coap_resource_t *resource,
	      coap_address_t *peer, coap_pdu_t *request, str *token,
	      coap_pdu_t *response,
	      int sensor) {
	LOGI("Got POST!!");
}


void
generic_on_unregister(coap_context_t *ctx, coap_registration_t *reg,
		int sensor) {


	/* I think this is the best place to invalidate the registration */
	LOGI("Invalidating");
	reg->invalid = 1;

	/*
	 * Unregistration must be done through the streaming manager
	 * SM_REQ_STOP, passing the ticket of the registration.
	 * The streaming manager will confirm the
	 * cancellation, then the reference count will be decremented
	 * by the server and if zero the registration destroyed.
	 * If the streaming manager does not have any stream
	 * with that ticket (should not happen) it confirms the cancellation anyways.
	 */
	put_sm_buf_item(ctx->smreqbuf, SM_REQ_STOP, sensor,
			(ticket_t)/*coap_registration_checkout(*/reg/*)*/, 0);

}




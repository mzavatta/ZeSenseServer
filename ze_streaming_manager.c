/*
 * ZeSense Streaming Manager
 * -- core streaming module
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#include "ze_streaming_manager.h"
#include "ze_coap_server_core.h"
#include "ze_coap_server_root.h"
#include "ze_log.h"
#include "utlist.h"

stream_context_t *
get_streaming_manager(/*coap_context_t  *cctx*/) {

	stream_context_t *temp;

	temp = malloc(sizeof(stream_context_t));
	if (temp == NULL) {
		LOGW("cannot allocate streaming manager");
		return NULL;
	}

	memset(temp, 0, sizeof(stream_context_t));

	//temp->server = cctx;

	temp->sensorManager = NULL;
	temp->sensorEventQueue = NULL;
	temp->looper = NULL;
	temp->gpsManager = NULL;

	return temp;
}

/* Some forward declarations for functions that are not really interface
 * and therefore not suitable in the header file. */
int put_coap_helper(ze_coap_request_buf_t *notbuf, int rtype,
		coap_ticket_t ticket, int conf, ze_payload_t *pyl,
		ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue);
ze_sm_request_t get_sm_helper(ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue);


void *
ze_coap_streaming_thread(void* args) {

	/* Switch on, off. */
	//pthread_exit(NULL);

	struct sm_thread_args* ar = ((struct sm_thread_args*)(args));
	stream_context_t *mngr = ar->smctx;
	ze_sm_request_buf_t *smreqbuf = ar->smreqbuf;
	ze_coap_request_buf_t *notbuf = ar->notbuf;
	JavaVM *jvm = ar->jvm;
	jobject actx = ar->actx;

	// Hello and current time and date
	LOGI("Hello from Streaming Manager Thread pid%d, tid%d!", getpid(), gettid());
	time_t lt;
	lt = time(NULL);

	// Attach thread to Dalvik
	AttachCurrentThead(jvm, &(mngr->env), NULL);

	/*-------------- Prepare missing components in the mngr context --------------*/

    // Prepare looper
    mngr->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    LOGI("SM, looper prepared");

    // Take the SensorManager (C++ singleton class)
    mngr->sensorManager = ASensorManager_getInstance();
    LOGI("SM, got sensorManager");
    // Create event queue associated with that looper //XXX !!! wtf is 45 ??
    mngr->sensorEventQueue =
    		ASensorManager_createEventQueue(mngr->sensorManager, mngr->looper, 45, NULL, NULL);
    LOGI("SM, got sensorEventQueue");

    // Take the ZeGPSManager class and create its instance and event queue
    mngr->ZeGPSManager = (*mngr->env)->FindClass(mngr->env, "eu/tb/zesense/ZeGPSManager");
    if ( !mngr->ZeGPSManager ) LOGW("ZeGPSManager class not found");

    jmethodID ZeGPSManager_constructor =
    		(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "<init>", "()V");
	if ( !ZeGPSManager_constructor ) LOGW("ZeGPSManager constructor not found");

    mngr->gpsManager = 	(*mngr->env)->NewObject(mngr->env, mngr->ZeGPSManager, ZeGPSManager_constructor);
    if ( !mngr->gpsManager ) LOGW("ZeGPSManager instance cannot be constructed");

	jmethodID ZeGPSManager_init =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "init", "(Landroid/content/Context;)V");
	if ( !ZeGPSManager_init ) LOGW("ZeGPSManager's init() not found");

	(*mngr->env)->CallVoidMethod(mngr->env, mngr->gpsManager, ZeGPSManager_init, actx);

/*
	jmethodID ZeGPSManager_destroy =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "destroy", "()V");
	if ( !ZeGPSManager_destroy ) LOGW("ZeGPSManager's destroy() not found");

	jmethodID ZeGPSManager_start =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "startStream", "()I");
	if ( !ZeGPSManager_start ) LOGW("ZeGPSManager's startStream() not found");

	jmethodID ZeGPSManager_stop =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "stopStream", "()I");
	if ( !ZeGPSManager_stop ) LOGW("ZeGPSManager's stopStream() not found");
*/




	ASensorEvent event;

	ze_payload_t *pyl = NULL;

	int rto, rtc, max_age;

	ze_sm_request_t sm_req;
	ze_coap_request_t server_req;

	ze_oneshot_t *osreq = NULL;
	ze_stream_t *stream = NULL;

	ze_oneshot_t *onescroll = NULL;

	/* To control the time spent on streaming
	 * wrt the time spent on serving requests
	 */
	int queuecount = 0;

	/* Internal miniqueue anti deadlocks. */
	sm_req_internal_t *adqueue = NULL;

	while(!globalexit) { /*thread loop start*/

		/*-------------------------Serve request queue---------------------------------*/

		/* Fetch one request from the buffer, if any.
		 * get_rq_buf_item does not block */
		sm_req = get_sm_helper(smreqbuf, adqueue);

		if (sm_req.rtype == SM_REQ_START) {
			LOGI("SM we got a START STREAM request");
			/* We have to send a COAP_STREAM_STOPPED message even when
			 * we are replacing an already existing stream, not only when
			 * we're not able to start one.
			 * Ok let's make it return NULL in both cases.. */
			if ( sm_start_stream(mngr, sm_req.sensor, sm_req.ticket, sm_req.freq) == NULL)
				put_coap_helper(notbuf, COAP_STREAM_STOPPED, sm_req.ticket,
						ZE_PARAM_UNDEFINED, NULL, smreqbuf, adqueue);
		}
		else if (sm_req.rtype == SM_REQ_STOP) {
			LOGI("SM we got a STOP STREAM request");
			/* Note that sm_stop_stream frees the memory of the stream it deletes. */
			if ( sm_stop_stream(mngr, sm_req.sensor, sm_req.ticket) != SM_ERROR )
				put_coap_helper(notbuf, COAP_STREAM_STOPPED, sm_req.ticket,
						ZE_PARAM_UNDEFINED, NULL, smreqbuf, adqueue);
				/*
				 * Note that we do not COAP_STREAM_STOPPED if no stream with
				 * that ticket number has been found. This is because the
				 * CoAP server does not issue another ticket on COAP_REQ_STOP
				 */
		}
		else if (sm_req.rtype == SM_REQ_ONESHOT) {
			LOGI("SM we got a ONESHOT request");
			if (mngr->sensors[sm_req.sensor].android_handle != NULL) {
				/* Sensor is active, suppose cache is fresh
				 * and serve the oneshot request immediately
				 */

				/* Take sample from cache and form payload */
				event = mngr->sensors[sm_req.sensor].event_cache;
				pyl = malloc(sizeof(ze_payload_t));
				if (pyl == NULL)
					LOGW("malloc failed!");
				pyl->length = sizeof(ASensorEvent);
				pyl->data = malloc(pyl->length);
				if (pyl->data == NULL)
					LOGW("malloc failed!");
				memcpy(pyl->data, &event, pyl->length);
				pyl->wts = event.timestamp;
				pyl->rtpts = 0; //FIXME

				/* Mirror the received request in the sender's interface
				 * attaching the payload. Do not free pyl because not it
				 * is needed by the notbuf.
				 */
				put_coap_helper(notbuf, COAP_SEND_ASYNCH, sm_req.ticket,
						COAP_MESSAGE_NON, pyl, smreqbuf, adqueue);
			}
			else {
				/* Sensor is not active, cache may be old.
				 * Activate the sensor and register oneshot request
				 * to be satisfied by the first matching sample that
				 * emerges from the sample queue
				 */
				android_sensor_activate(mngr, sm_req.sensor, DEFAULT_FREQ);

				osreq = sm_new_oneshot(sm_req.ticket);
				LL_APPEND(mngr->sensors[sm_req.sensor].oneshots, osreq);

				onescroll = mngr->sensors[sm_req.sensor].oneshots;
				while(onescroll!=NULL) {
					LOGI("SM oneshot register this entry tick%d", (int)onescroll->one.tid);
					onescroll = onescroll->next;
				}
			}
		}
		else if (sm_req.rtype == SM_REQ_INVALID) {
			/* Buffer found empty or request invalid. */
		}

		/* Clean temporary variables that are reused in the next phase.
		 * No need to free what they point to so far, they only serve
		 * as temporary pointers for searches or similar */
		osreq = NULL;
		stream = NULL;


		/*-------------------------Send some samples-----------------------------------*/

		while (queuecount < QUEUE_REQ_RATIO) {

		/* Is this blocking? doesn't seem like.. and that's good. */
		if (ASensorEventQueue_getEvents(mngr->sensorEventQueue, &event, 1) > 0) {

			if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            	LOGI("accel: x=%f y=%f z=%f",
						event.acceleration.x, event.acceleration.y,
						event.acceleration.z);
			}

            /* Update cache in any case. */
            mngr->sensors[event.type].event_cache = event;

            /* If we have any oneshot for this sensor,
             * we need their tickets before sending the event,
             * clear each of them and send them the event.
             */
			if (mngr->sensors[event.type].oneshots != NULL) {

				/* Form payload, it will be the same for each oneshot. */
				pyl = malloc(sizeof(ze_payload_t));
				if (pyl == NULL)
					LOGW("malloc failed!");
				pyl->length = sizeof(ASensorEvent);
				pyl->data = malloc(pyl->length);
				if (pyl->data == NULL)
					LOGW("malloc failed!");
				memcpy(pyl->data, &event, pyl->length);
				pyl->wts = event.timestamp;
				pyl->rtpts = 0; //TODO

				/* Empty this list freeing its elements. */
				while (mngr->sensors[event.type].oneshots != NULL) {
					/* Take first element. */
					osreq = mngr->sensors[event.type].oneshots;
					/* Use its ticket to send the sample */
					put_coap_helper(notbuf, COAP_SEND_ASYNCH,
							osreq->one, COAP_MESSAGE_CON, pyl, smreqbuf, adqueue);
					/* Let the head point to the next element before freeing the former. */
					mngr->sensors[event.type].oneshots = osreq->next;
					free(osreq);

					onescroll = mngr->sensors[event.type].oneshots;
					while (onescroll != NULL) {
						LOGI("SM oneshot register this entry tick%d", (int)onescroll->one.tid);
						onescroll = onescroll->next;
					}

				}

				/* Do not free pyl because it
				 * is needed by the notbuf.
				 */

				/* All oneshots cleared, if there are no streams active
				 * we're entitled to turn off the sensor.
				 */
				if (mngr->sensors[event.type].streams == NULL)
					android_sensor_turnoff(mngr, event.type);
			}

			/* Done with the oneshots,
			 * now this sample might also belong to some stream
			 */
			if (mngr->sensors[event.type].streams != NULL) {

				/* Form payload, though it will change for every stream. */
				pyl = malloc(sizeof(ze_payload_t));
				if (pyl == NULL)
					LOGW("malloc failed!");
				pyl->length = sizeof(ASensorEvent);
				pyl->data = malloc(pyl->length);
				if (pyl->data == NULL)
					LOGW("malloc failed!");
				memcpy(pyl->data, &event, pyl->length);
				pyl->wts = event.timestamp;
				//pyl->rtpts = 4567; //TODO

				/* TODO: for the moment notify all the streams regardless of frequency.
				 * wait but like this*/
				stream = mngr->sensors[event.type].streams;
				while (stream != NULL) {
					put_coap_helper(notbuf, COAP_SEND_NOTIF,
							stream->reg, COAP_MESSAGE_NON, pyl, smreqbuf, adqueue);
					/*
					 * we'll likely need to do some operations here,
					 * like increment the sequence number
					 * or stuff like that
					 */
					stream = stream->next;
				}

				/* Do not free pyl because not it
				 * is needed by the notbuf.
				 */
			}
			else {
				/* we have cleared all the oneshots
				 * and there is no stream on that sensor
				 */
				//android_sensor_turnoff(mngr, event.type);
				/* we might have some stream which was stopped
				 * and its sensor turned off at that moment,
				 * but some samples might still be in the queue.
				 * Since we enter this else on a per-sample basis,
				 * we might ed up trying to turn off the sensor
				 * due to samples that were still in the queue when
				 * the stop stream was performed.
				 * Although it is useful for turning off the sensor
				 * that produced a oneshot, in the case the oneshots are
				 * served and there's no stream active. OK then
				 * move it at the end, with a lookahead to the streams.
				 */
			}

		}
		queuecount++;
		}

		//LOGI("SM returning to check for incoming requests");

		queuecount = 0;

		/*----------------------Sleep for a while, not much actually-------------------*/

		struct timespec rqtp;
		rqtp.tv_sec = 0;
		rqtp.tv_nsec = 5000000; //1msec
		nanosleep(&rqtp, NULL);

	} /*thread loop end*/

	/*
	 * TODO: Turn off all sensors that might still be active!
	 * E.g. asked to quit the server while a stream is in
	 * process..
	 */

	LOGI("Streaming Manager out of thread loop, returning..");
}

/*------------------ Anti-deadlock queue wrappers -----------------------------------*/

int put_coap_helper(ze_coap_request_buf_t *notbuf, int rtype,
		coap_ticket_t ticket, int conf, ze_payload_t *pyl,
		ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue) {

	int result;
	sm_req_internal_t *temp = NULL;

	/* Try to put in the buffer. If timeout on full condition,
	 * fetch a request from the sm buffer and internally queue it.
	 * Items are appended, so since it must be a FIFO queue,
	 * we'll fetch from list head.
	 * Loop until the original put succeeds.
	 */
	result = put_coap_buf_item(notbuf, rtype, ticket, conf, pyl);
	while (result == ETIMEDOUT) {

		/* Take some memory for the queue element and copy.
		 * Yes in this way I'm adding one element to the queue
		 * even though the queue is empty, that's a possible
		 * optimization. It's not a problem as the checks on the
		 * validity of the request are made by the caller.
		 * Also, it should not happen frequently to have a timeout
		 * in the coap put and not having the sm queue full..
		 * (although it could happen when the coap thread blocks
		 * for a long time but not on the full condition, but
		 * there's no other real place where the coap thread would
		 * block for a long time)
		 */
		temp = malloc(sizeof(sm_req_internal_t));
		if (temp == NULL) return -1;
		temp->req = get_sm_buf_item(smreqbuf);
		LL_APPEND(adqueue, temp);

		result = put_coap_buf_item(notbuf, rtype, ticket, conf, pyl);
	}
	return 1;
}

/* It frees the memory, do not need to free anything outside!! */
ze_sm_request_t get_sm_helper(ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue) {

	sm_req_internal_t *temp = NULL;
	ze_sm_request_t ret;

	/* If there is anything in the internal queue, return it.
	 * Fetch from list head since it has to be FIFO and items
	 * get appended during insertion.
	 * Otherwise, consume some of the real queue.
	 */
	if (adqueue != NULL) {
		temp = adqueue; //save pointer
		adqueue = temp->next; //unplug
		ret = temp->req; //save value
		free(temp); //free
		return ret;
	}
	/* Otherwise very normal get... */
	return get_sm_buf_item(smreqbuf);
}



/*-------------------- Stream helpers -----------------------------------------------*/
ze_stream_t *sm_start_stream(stream_context_t *mngr, int sensor_id,
		coap_ticket_t reg, int freq) {

	LOGI("SM starting stream");
	//CHECK_OUT_RANGE(sensor_id);

	ze_stream_t *sub, *newstream;

	newstream = sm_new_stream(reg, freq);
	if (newstream == NULL) return NULL;
	/* TODO assign proper values to newstream:
	 * last_wts
	 * randomize rtpts since its first assignment
	 * frequency divider to be considered based on the current sampling frequency
	 */

	if ( mngr->sensors[sensor_id].android_handle == NULL ) {
		/* Sensor is not active, activate in any case
		 * Put a check anyway, if a sensor is not active
		 * the streams should be empty
		 */
		if (mngr->sensors[sensor_id].streams != NULL)
			LOGW("SM inconsistent state");

		android_sensor_activate(mngr, sensor_id, freq);
	}
	/* Sensor is already active.
	 * Re-evaluate its frequency based on the new request
	 */
	else if (freq > mngr->sensors[sensor_id].freq) {
		mngr->sensors[sensor_id].freq = freq;
		android_sensor_changef(mngr, sensor_id, freq);
	}

	/* Replace if there is a stream with the same ticket
	 * otherwise just append..
	 */
	sub = sm_find_stream(mngr, sensor_id, reg);
	if (sub != NULL) {
		LOGI("SM replacing stream");
		/* TODO
		 * Do I have to transfer anything to newstream before
		 * sub gets deleted? Maybe some local status variables.
		 * We'll see when we implement the timestamping..
		 */
		LL_DELETE(mngr->sensors[sensor_id].streams, sub);
		free(sub);
		LL_APPEND(mngr->sensors[sensor_id].streams, newstream);
		return NULL; /* So that the caller can know that we've not added
		 	 	 	   * a brand new stream. */
	}
	LL_APPEND(mngr->sensors[sensor_id].streams, newstream);

	return newstream;
}

int sm_stop_stream(stream_context_t *mngr, int sensor_id, coap_ticket_t reg) {

	//CHECK_OUT_RANGE(sensor_id);
	LOGI("SM Stopping stream");

	ze_stream_t *del, *temp;

	del = sm_find_stream(mngr, sensor_id, reg);

	if (del == NULL) {
		/* Stream not found.. its very strange but anyway.
		 * Important to return this value, it is checked by the caller.
		 * Indeed if the Streaming Manager does not find any stream
		 * with that ticket number, it should not send a COAP_STREAM_STOPPED
		 * because the CoAP server does not issue a new ticket
		 * when asking for SM_REQ_STOP.
		 */
		LOGW("SM inconsistent state in streaming manager, asked to stop"
				"stream but no streams are active");
		return SM_ERROR;
	}

	/* Delete it from this list and free its memory */
	LL_DELETE(mngr->sensors[sensor_id].streams, del);
	free(del);
	del = NULL;

	/* Turn off the sensor if it was the last one and there is no
	 * oneshot request to be served, otherwise reconsider
	 * the output frequency of the sensor, as we might
	 * have taken out the fastest demanding one. */
	if (mngr->sensors[sensor_id].streams == NULL &&
			mngr->sensors[sensor_id].oneshots == NULL) {
		android_sensor_turnoff(mngr, sensor_id);
		mngr->sensors[sensor_id].freq = 0;
	}
	else {
		/* reconsider the maximum frequency, maybe we just stopped
		 * the stream with the highest one.
		 * just take the maximum of those left
		 * (TODO: it's not optimal, first of all the deleted stream
		 * might not have been the maximum, and also the maximum
		 * of those left might been equal to the previous one)
		 */
		temp = mngr->sensors[sensor_id].streams;
		mngr->sensors[sensor_id].freq = temp->freq;
		while (temp != NULL) {
			if (temp->freq > mngr->sensors[sensor_id].freq)
				mngr->sensors[sensor_id].freq = temp->freq;
			temp = temp->next;
		}
		android_sensor_changef(mngr, sensor_id, mngr->sensors[sensor_id].freq);
	}

	/* send confirm cancellation message to the CoAP server layers
	 * meaning that we don't hold the ticket anymore
	 * let's do it outside this function..
	 */
	return 0;
}

/**
 * Checks if a stream from @p sensor_id with ticket @p reg
 * is currently running.
 *
 * @param mngr		The Streaming Manager context
 * @param sensor_id	The sensor source of data
 * @param reg		The ticket given by the lower layer
 *
 * @return The reference to the item if found, NULL otherwise
 */
ze_stream_t *sm_find_stream(stream_context_t *mngr, int sensor_id, coap_ticket_t reg) {

	ze_stream_t *temp = mngr->sensors[sensor_id].streams;
	while (temp != NULL) {
		if (temp->reg.reg == reg.reg) break;
		temp = temp->next;
	}
	return temp;
}

ze_stream_t *sm_new_stream(coap_ticket_t reg, int freq) {

	ze_stream_t *new;
	new = (ze_stream_t *) malloc(sizeof(ze_stream_t));
	if (new == NULL) {
		LOGW("new stream malloc failed");
		return NULL;
	}
	memset(new, 0, sizeof(ze_stream_t));

	new->next = NULL;
	new->reg = reg;
	new->freq = freq;
	new->last_rtpts = RTP_TS_START;

	return new;
}

ze_oneshot_t *sm_new_oneshot(coap_ticket_t one) {

	ze_oneshot_t *new;
	new = (ze_oneshot_t *) malloc(sizeof(ze_oneshot_t));
	if (new == NULL) {
		LOGW("new oneshot malloc failed");
		return NULL;
	}
	memset(new, 0, sizeof(ze_oneshot_t));

	new->next = NULL;
	new->one = one;

	return new;
}

ze_oneshot_t *sm_find_oneshot(stream_context_t *mngr, int sensor_id, coap_ticket_t one) {

	ze_oneshot_t *temp = mngr->sensors[sensor_id].oneshots;
	while (temp != NULL) {
		if (temp->one.tid == one.tid) break;
		temp = temp->next;
	}
	return temp;
}


/*---------------------Sensors helpers--------------------------------------------*/

int android_sensor_activate(stream_context_t *mngr, int sensor, int freq) {

	LOGI("SM Turning on android sensor%d", sensor);

	//CHECK_OUT_RANGE(sensor);

	/* Grab reference from Android */
	mngr->sensors[sensor].android_handle =
			ASensorManager_getDefaultSensor(mngr->sensorManager, sensor);

	/* Enable sensor at the specified frequency */
	if (mngr->sensors[sensor].android_handle != NULL) {
		ASensorEventQueue_enableSensor(mngr->sensorEventQueue,
				mngr->sensors[sensor].android_handle);
		ASensorEventQueue_setEventRate(mngr->sensorEventQueue,
				mngr->sensors[sensor].android_handle, (1000L/freq)*1000);

		return 0;
	}

	LOGW("cannot get sensor %d", sensor);
	return SM_ERROR;
}

int android_sensor_changef(stream_context_t *mngr, int sensor, int freq) {

	LOGI("SM Changing frequency to android sensor%d", sensor);

	//CHECK_OUT_RANGE(sensor);

	if (mngr->sensors[sensor].android_handle == NULL) {
		LOGW("SM inconsistent state in streaming manager, asked changef"
				"but sensor not initialized in Android");
		return SM_ERROR;
	}

	ASensorEventQueue_setEventRate(mngr->sensorEventQueue,
			mngr->sensors[sensor].android_handle, (1000L/freq)*1000);
	return 0;
}


int android_sensor_turnoff(stream_context_t *mngr, int sensor) {

	LOGI("SM Turning off android sensor%d", sensor);

	//CHECK_OUT_RANGE(sensor);

	if (mngr->sensors[sensor].android_handle == NULL) {
		LOGW("SM inconsistent state in streaming manager, asked turnoff"
				"but sensor not initialized in Android");
		return SM_ERROR;
	}

	ASensorEventQueue_disableSensor(mngr->sensorEventQueue,
			mngr->sensors[sensor].android_handle);
	mngr->sensors[sensor].android_handle = NULL; //Not clear how Android leaves this pointer
	return 0;
}

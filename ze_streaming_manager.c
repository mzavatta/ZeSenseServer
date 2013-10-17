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
#include "ze_coap_payload.h"
#include "ze_timing.h"
#include "ze_sm_resbuf.h"
#include "ze_sm_reqbuf.h"
#include "ze_carrier.h"

typedef struct sm_req_internal_t {
	struct sm_req_internal_t *next;
	ze_sm_request_t req;
} sm_req_internal_t;

//ze_sm_packet_t* form_sm_packet(ASensorEvent event);
ze_sm_packet_t *
encode(ASensorEvent *event, int *rtpts, int num);


struct generic_carr_thread_args pcargs; //proximity carrier thread args
struct generic_carr_thread_args ocargs; //orientation carrier thread args

stream_context_t *
get_streaming_manager(/*coap_context_t  *cctx*/) {

	stream_context_t *temp;

	temp = malloc(sizeof(stream_context_t));
	if (temp == NULL) {
		LOGW("cannot allocate streaming manager");
		return NULL;
	}
	memset(temp, 0, sizeof(stream_context_t));

	temp->sensorManager = NULL;
	temp->sensorEventQueue = NULL;
	temp->looper = NULL;
	temp->carrq = NULL;

	return temp;
}

#define SAMPLES_QUEUE 1
#define CARRIERS_QUEUE 2


/* Some forward declarations for functions that are not really interface
 * and therefore not suitable in the header file. */
int put_coap_helper(ze_sm_response_buf_t *notbuf, int rtype,
		ticket_t ticket, int conf, /*ze_payload_t *pyl*/ze_sm_packet_t *pk,
		ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue);
ze_sm_request_t get_sm_helper(ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue);

//void proximity_carrier(int signum); //signal handler for deprecated implementation of carriers

#define NUM_SAMPLES 5000
struct zs_ASensorEvent {
	int64_t gents; //as it is fed into the event interface
	int64_t collts; //when we collect it
};
struct zs_ASensorEvent accel_events_list[NUM_SAMPLES];
int64_t accel_periods[NUM_SAMPLES];
struct zs_ASensorEvent prox_events_list[NUM_SAMPLES];
int64_t prox_periods[NUM_SAMPLES];
struct zs_ASensorEvent light_events_list[NUM_SAMPLES];
int64_t light_periods[NUM_SAMPLES];
struct zs_ASensorEvent orient_events_list[NUM_SAMPLES];
int64_t orient_periods[NUM_SAMPLES];
struct zs_ASensorEvent gyro_events_list[NUM_SAMPLES];
int64_t gyro_periods[NUM_SAMPLES];



void *
ze_coap_streaming_thread(void* args) {

	/* Switch on, off. */
	//pthread_exit(NULL);

	struct sm_thread_args* ar = ((struct sm_thread_args*)(args));
	stream_context_t *mngr = ar->smctx;
	ze_sm_request_buf_t *smreqbuf = ar->smreqbuf;
	ze_sm_response_buf_t *notbuf = ar->notbuf;
	JavaVM *jvm = ar->jvm;
	jobject actx = ar->actx;

	// Hello and current time and date
	LOGI("Hello from Streaming Manager Thread pid%d, tid%d!", getpid(), gettid());
	time_t lt;
	lt = time(NULL);

	// Attach thread to Dalvik
	jint failattach = (*jvm)->AttachCurrentThread(jvm, &(mngr->env), NULL);
	if ( failattach ) LOGW("SM cannot attach thread");

	// Register in context the ZeGPSManager class
	mngr->ZeGPSManager = ar->ZeGPSManager;

	// Create the Carriers Queue
	mngr->carrq = init_carriers_queue();
	if(mngr->carrq == NULL) {
		LOGW("Cannot initialize Carriers Queue");
		exit(1);
	}


	/*-------------------------- Prepare the mngr context -----------------------*/

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

	/* TODO Initialize properly the .sensors array in all its fields. */
	int i;
	for (i=0;i<ZE_NUMSENSORS;i++) {
		mngr->sensors[i].sensor = i;
		if (i == ASENSOR_TYPE_PROXIMITY) {// Initialize mutex for the proximity sensor's carrier
			int error = pthread_mutex_init(&(mngr->sensors[i].carrthrmtx), NULL);
			if (error) {
				LOGW("Failed to initialize proxmtx:%s\n", strerror(error));
				return NULL;
			}
		}
		else if (i == ZESENSE_SENSOR_TYPE_ORIENTATION) {// Initialize mutex for the (optional) orientation carrier
			int error = pthread_mutex_init(&(mngr->sensors[i].carrthrmtx), NULL);
			if (error) {
				LOGW("Failed to initialize orientmtx:%s\n", strerror(error));
				return NULL;
			}
		}
	}

    /* Create ZeGPSManager instance and initialize it
     * Note that it is placed inside a ze_sensor_t's
     * gpsManager attribute, to be consistent with the
     * android_handle attribute that we create for
     * each sensor. */
    jmethodID ZeGPSManager_constructor =
    		(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "<init>", "()V");
	jmethodID ZeGPSManager_init =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "init", "(Landroid/content/Context;)V");
	if ( !ZeGPSManager_constructor || !ZeGPSManager_init ) LOGW("ZeGPSManager's methods not found");
    mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager =
    		(*mngr->env)->NewObject(mngr->env, mngr->ZeGPSManager, ZeGPSManager_constructor);
    if ( !mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager)
    	LOGW("ZeGPSManager instance cannot be constructed");
	(*mngr->env)->CallVoidMethod(mngr->env, mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager,
			ZeGPSManager_init, actx);



	ASensorEvent event;
	jobject location;

	ze_payload_t *pyl = NULL;

	ze_sm_packet_t *pk;

	int rto, rtc, max_age;

	ze_sm_request_t sm_req;
	//ze_sm_response_t server_req;

	ze_oneshot_t *osreq = NULL;
	ze_stream_t *stream = NULL;

	ze_oneshot_t *onescroll = NULL;

	jmethodID ZeGPSManager_getSample =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "getSample", "()Landroid/location/Location;");
	if ( !ZeGPSManager_getSample ) LOGW("ZeGPSManager's getSample() not found");

	jclass Location = (*mngr->env)->FindClass(mngr->env, "android/location/Location");
	if ( !Location ) LOGW("Class Location not found");

	jmethodID Location_getLatitude =
			(*mngr->env)->GetMethodID(mngr->env, Location, "getLatitude", "()D");
	if ( !Location_getLatitude ) LOGW("Method getLatitude not found");

	jmethodID Location_getTime =
			(*mngr->env)->GetMethodID(mngr->env, Location, "getTime", "()J");
	if ( !Location_getTime ) LOGW("Method getTime not found");


	/* Flush sensor queue. */
	ASensorEvent evfl;
	int gotfl = 1;
	do {
		gotfl = ASensorEventQueue_getEvents(mngr->sensorEventQueue, &evfl, 1);
	}
	while (gotfl > 0);


	/* To control the time spent on streaming
	 * wrt the time spent on serving requests
	 */
	int queuecount = 0;

	/* Internal anti-deadlock mini queue. */
	sm_req_internal_t *adqueue = NULL;

	/* Fake timestamp to pass to encoder when sending oneshots. */
	int fakets = 0;

	int accel_counter = 0, accel_samples_taken = 0;
	int orient_counter = 0, orient_samples_taken = 0;
	int light_counter = 0, light_samples_taken = 0;
	int prox_counter = 0, prox_samples_taken = 0;
	int gyro_counter = 0, gyro_samples_taken = 0;

	while(!globalexit) { /*------- Thread loop start ---------------------------------*/

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
				put_coap_helper(notbuf, STREAM_STOPPED, sm_req.ticket,
						ZE_PARAM_UNDEFINED, NULL, smreqbuf, adqueue);
		}
		else if (sm_req.rtype == SM_REQ_STOP) {
			LOGI("SM we got a STOP STREAM request");
			/* Note that sm_stop_stream frees the memory of the stream it deletes. */
			if ( sm_stop_stream(mngr, sm_req.sensor, sm_req.ticket) != SM_ERROR )
				put_coap_helper(notbuf, STREAM_STOPPED, sm_req.ticket,
						ZE_PARAM_UNDEFINED, NULL, smreqbuf, adqueue);
				/*
				 * Note that we do not COAP_STREAM_STOPPED if no stream with
				 * that ticket number has been found. This is because the
				 * CoAP server does not issue another ticket on COAP_REQ_STOP
				 */
		}
		else if (sm_req.rtype == SM_REQ_ONESHOT) {
			LOGI("SM we got a ONESHOT request");
			if (mngr->sensors[sm_req.sensor].cache_valid == 1) {
				/* Cache is fresh. */

				LOGI("SM serving oneshot request from cache");

				event = mngr->sensors[sm_req.sensor].event_cache;
				//pyl = form_data_payload(event);
				//pk = form_sm_packet(event);
				pk = encode(&event, &fakets, 1);

				/* Mirror the received request in the sender's interface
				 * attaching the payload. Do not free pyl because not it
				 * is needed by the notbuf. */
				put_coap_helper(notbuf, ONESHOT, sm_req.ticket,
						COAP_MESSAGE_NON, pk, smreqbuf, adqueue);
			}
			else {
				/* Cache may be old.
				 * Activate the sensor and register oneshot request
				 * to be satisfied by the first matching sample that
				 * emerges from the sample queue.
				 * Android's interface does not allow to ask for only
				 * one sample from a sensor.
				 */
				android_sensor_activate(mngr, sm_req.sensor, DEFAULT_FREQ);

				osreq = sm_new_oneshot(sm_req.ticket);
				LL_APPEND(mngr->sensors[sm_req.sensor].oneshots, osreq);

				onescroll = mngr->sensors[sm_req.sensor].oneshots;
				while(onescroll!=NULL) {
					LOGI("SM oneshot register this entry tick%d", (int)onescroll->one);
					onescroll = onescroll->next;
				}
			}
		}
		else if (sm_req.rtype == SM_REQ_INVALID) {
			/* Buffer found empty or request invalid. */
		}

		/* Clean temporary variables that are reused in the next phase.
		 * No need to free what they point to so far, they only serve
		 * as temporary pointers for searches or similar. */
		osreq = NULL;
		stream = NULL;


		/*-------------------------Send some samples-----------------------------------*/
		int qselect = SAMPLES_QUEUE;
		while (queuecount < QUEUE_REQ_RATIO) {

		/*
		 * This way of scheduling pickup from the two queues is a bit
		 * unfair though.. favors the sensors that use the queue used
		 * by less sensors..
		 */
		int have_events = 0;
		if (qselect == SAMPLES_QUEUE) {
			have_events = ASensorEventQueue_getEvents(mngr->sensorEventQueue, &event, 1);
			/* Only write the cache for the carrier to produce the stream..
			 * The stream to the receiver, for the event based sensors, is made only
			 * of the carrier samples and not of real samples.
			 * This method greatly simplifies the assignment of timestamps
			 * to the real samples, paying the price that real samples will be desynch
			 * with the other streams of at most tau_carrier. */
			while (have_events>0 &&
					((event.type == ASENSOR_TYPE_PROXIMITY) ||
					(event.type == ZESENSE_SENSOR_TYPE_ORIENTATION)) ) { // or any other event based sensor
					LOGI("Real value detected sensor type:%d p=%f", event.type, event.distance);
					write_last_event_SYN(&(mngr->sensors[event.type]), event);
		            //mngr->sensors[event.type].event_cache = event;
		            mngr->sensors[event.type].cache_valid = 1;
		            have_events = ASensorEventQueue_getEvents(mngr->sensorEventQueue, &event, 1);
		            //exit(1);
			}
		}
		else if (qselect == CARRIERS_QUEUE) {
			have_events = get_carrier_event(mngr->carrq, &event);
		}

		/*
		// Is this blocking? doesn't seem like.. and that's good.
		if (ASensorEventQueue_getEvents(mngr->sensorEventQueue, &event, 1) > 0) { */
		if (have_events > 0) {

			if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            	accel_samples_taken++;
            	LOGI("accel: x=%f y=%f z=%f",
						event.acceleration.x, event.acceleration.y,
						event.acceleration.z);
            	accel_events_list[accel_counter].gents = event.timestamp;
            	accel_events_list[accel_counter].collts = get_ntp();
            	accel_counter++;
			}
			else if (event.type == ASENSOR_TYPE_PROXIMITY) {
            	prox_samples_taken++;
				LOGI("Fake proximity value prox: p=%f", event.distance);
            	prox_events_list[prox_counter].gents = event.timestamp;
            	prox_events_list[prox_counter].collts = get_ntp();
            	prox_counter++;
			}
			else if (event.type == ASENSOR_TYPE_LIGHT) {
            	light_samples_taken++;
				LOGI("Light value: L=%f", event.light);
            	light_events_list[light_counter].gents = event.timestamp;
            	light_events_list[light_counter].collts = get_ntp();
            	light_counter++;
			}
			else if (event.type == ZESENSE_SENSOR_TYPE_ORIENTATION) {
				orient_samples_taken++;
				LOGI("event orient (may be fake mode): azi=%f pitch=%f roll=%f",
        			event.vector.azimuth, event.vector.pitch,
        			event.vector.roll);
            	orient_events_list[orient_counter].gents = event.timestamp;
            	orient_events_list[orient_counter].collts = get_ntp();
            	orient_counter++;
			}
			else if (event.type == ASENSOR_TYPE_GYROSCOPE) {
				gyro_samples_taken++;
				LOGI("event gyro: x=%f y=%f z=%f",
					event.vector.x, event.vector.y,
					event.vector.z);
            	gyro_events_list[gyro_counter].gents = event.timestamp;
            	gyro_events_list[gyro_counter].collts = get_ntp();
            	gyro_counter++;
			}

            /* Update cache if the event is not a fake produced by
             * a carrier. */
			if (qselect == SAMPLES_QUEUE) {
				mngr->sensors[event.type].event_cache = event;
				mngr->sensors[event.type].cache_valid = 1;
			}

            /* If we have any oneshot for this sensor,
             * we need their tickets before sending the event,
             * clear each of them and send them the event.
             */
			if (mngr->sensors[event.type].oneshots != NULL) {

				/* Empty this list freeing its elements. */
				while (mngr->sensors[event.type].oneshots != NULL) {

					/* Allocate a new payload. */
					//pyl = form_data_payload(event);
					//pk = form_sm_packet(event);
					pk = encode(&event, &fakets, 1);

					/* Take first element. */
					osreq = mngr->sensors[event.type].oneshots;
					/* Use its ticket to send the sample */
					put_coap_helper(notbuf, ONESHOT,
							osreq->one, COAP_MESSAGE_CON, pk, smreqbuf, adqueue);
					/* Unplug before freeing. */
					mngr->sensors[event.type].oneshots = osreq->next;
					free(osreq);

					/* Do not free pks because they are carried downwards. */
				}

				/* All oneshots cleared, if there are no streams active
				 * we're entitled to turn off the sensor.
				 */
				if (mngr->sensors[event.type].streams == NULL)
					android_sensor_turnoff(mngr, event.type);
			}

			/* Done with oneshots,
			 * now this sample might also belong to some stream.
			 * Here we have to add the timestamp.
			 * The sequence number is at the "packet" layer,
			 * not at the sample layer..
			 */
			if (mngr->sensors[event.type].streams != NULL) {

				/* TODO: for the moment notify all the streams regardless of frequency. */
				stream = mngr->sensors[event.type].streams;
				while (stream != NULL) {

if (stream->repeat == REPETITION_OFF) {
					if (stream->event_buffer_level < SOURCE_BUFFER_SIZE) {

						/* Save the sample in the buffer. */
						stream->event_buffer[stream->event_buffer_level] = event;

						/* Compute the timestamp of the sample and save it in the buffer. */
						int correction_factor = 0;
						int tsa = (stream->last_rtpts+(RTP_TSCLOCK_FREQ / stream->freq))
								+correction_factor;
						stream->event_rtpts_buffer[stream->event_buffer_level] = tsa;

						/* Update the timestamp references. */
						stream->last_rtpts = tsa;
						stream->last_wts = event.timestamp;

						/* Moved to the protocol level (packet based, not sample based). */
						//stream->last_sn++;

						/* A new sample has been added, increase index. */
						stream->event_buffer_level++;
					}

					/* When the buffer is full, send the bundle. */
					if (stream->event_buffer_level == SOURCE_BUFFER_SIZE)  {

						LOGW("Send buffer full at:%d", stream->event_buffer_level);

						/* Encode packet bundle. */
						pk = encode(stream->event_buffer, stream->event_rtpts_buffer, SOURCE_BUFFER_SIZE);

						/* Deliver command to the protocol layer. */
						put_coap_helper(notbuf, STREAM_UPDATE,
								stream->reg, stream->retransmit, pk, smreqbuf, adqueue);

						/* We sent as many samples as there were in the buffer. */
						stream->samples_sent += SOURCE_BUFFER_SIZE;

						/* Buffer contents have been sent, empty it. */
						stream->event_buffer_level = 0;
					}
}
else { //stream->repeat == REPETITION_ON
/* ATTENTION IT WORKS ONLY WITH SOURCE_BUFFER_SIZE = 2 i.e. REPETITION OF ONE SAMPLE. */

					/* Compute the timestamp of the sample. */
					int correction_factor = 0;
					int tsa = (stream->last_rtpts+(RTP_TSCLOCK_FREQ / stream->freq))
								+correction_factor;

					if (stream->event_buffer_level == 0) {

						/* Save the sample in the buffer's first position. */
						stream->event_buffer[0] = event;
						stream->event_rtpts_buffer[0] = tsa;

						/* At least a sample has been added, flag this fact. */
						stream->event_buffer_level = 1;
					}

					/* Save the sample in the buffer's second position. */
					stream->event_buffer[1] = event;
					stream->event_rtpts_buffer[1] = tsa;

					/* Update the timestamp references. */
					stream->last_rtpts = tsa;
					stream->last_wts = event.timestamp;

					/* Encode packet bundle. */
					pk = encode(stream->event_buffer, stream->event_rtpts_buffer, SOURCE_BUFFER_SIZE);

					/* Deliver command to the protocol layer. */
					put_coap_helper(notbuf, STREAM_UPDATE,
								stream->reg, stream->retransmit, pk, smreqbuf, adqueue);

					/* We sent one sample. */
					stream->samples_sent++;

					/* Backup current sample for repetition in the next round. */
					stream->event_buffer[0] = event;
					stream->event_rtpts_buffer[0] = tsa;
}

					stream = stream->next;
				}

				/* Do not free pks because not it
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
				 * move it at the end of the oneshot section,
				 * with a lookahead to the emptyness of the streams.
				 */
			}

		}

		if (qselect == SAMPLES_QUEUE) qselect = CARRIERS_QUEUE;
		else qselect = SAMPLES_QUEUE;

		queuecount++;
		}

		queuecount = 0;


		/*------- Now a check at the GPS.. ------------------------------------------ */
		/* This can be done infrequently because the frequency of GPS updates is
		 * definitely not so high as an accelerometer can be for instance. Therefore
		 * we can keep it outside the QUEUE_REQ_RATIO scope, and still a lot of
		 * checks will end up empty handed. */

		location = 	(*mngr->env)->CallObjectMethod(mngr->env,
				mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager, ZeGPSManager_getSample);
		if ( location ) { //Queue not empty, it'd be NULL otherwise

			jdouble loc_lat = (*mngr->env)->
					CallDoubleMethod(mngr->env, location, Location_getLatitude);
			jlong loc_time = (*mngr->env)->
					CallLongMethod(mngr->env, location, Location_getTime);

			LOGI("Location in native lat%f time%lld", loc_lat, loc_time);
		}

		/*----------------------Sleep for a while, not much actually-------------------*/

		struct timespec rqtp;
		rqtp.tv_sec = 0;
		rqtp.tv_nsec = 1000000; //1msec
		nanosleep(&rqtp, NULL);

	} /*thread loop end*/


	int k;
	int accel_max_period = 0, accel_min_period = 0;
	for (k=0; k<accel_samples_taken-1; k++) {
		accel_periods[k] = accel_events_list[k+1].gents - accel_events_list[k].gents;
		if (k==0) accel_min_period = accel_periods[k];
		if (accel_periods[k]>accel_max_period) accel_max_period = accel_periods[k];
		if (accel_periods[k]<accel_min_period) accel_min_period = accel_periods[k];
	}
	int64_t accel_incsum = 0;
	for(k=0; k<accel_samples_taken-1; k++) {
		accel_incsum+=accel_periods[k];
	}
	double accel_average = ((double)accel_incsum)/(accel_samples_taken-1);

	int orient_max_period = 0, orient_min_period = 0;
	for (k=0; k<orient_samples_taken-1; k++) {
		orient_periods[k] = orient_events_list[k+1].gents - orient_events_list[k].gents;
		if (k==0) orient_min_period = orient_periods[k];
		if (orient_periods[k]>orient_max_period) orient_max_period = orient_periods[k];
		if (orient_periods[k]<orient_min_period) orient_min_period = orient_periods[k];
	}
	int64_t orient_incsum = 0;
	for(k=0; k<orient_samples_taken-1; k++) {
		orient_incsum+=orient_periods[k];
	}
	double orient_average = ((double)orient_incsum)/(orient_samples_taken-1);

	int light_max_period = 0, light_min_period = 0;
	for (k=0; k<light_samples_taken-1; k++) {
		light_periods[k] = light_events_list[k+1].gents - light_events_list[k].gents;
		if (k==0) light_min_period = light_periods[k];
		if (light_periods[k]>light_max_period) light_max_period = light_periods[k];
		if (light_periods[k]<light_min_period) light_min_period = light_periods[k];
	}
	int64_t light_incsum = 0;
	for(k=0; k<light_samples_taken-1; k++) {
		light_incsum+=light_periods[k];
	}
	double light_average = ((double)light_incsum)/(light_samples_taken-1);

	int prox_max_period = 0, prox_min_period = 0;
	for (k=0; k<prox_samples_taken-1; k++) {
		prox_periods[k] = prox_events_list[k+1].gents - prox_events_list[k].gents;
		if (k==0) prox_min_period = prox_periods[k];
		if (prox_periods[k]>prox_max_period) prox_max_period = prox_periods[k];
		if (prox_periods[k]<prox_min_period) prox_min_period = prox_periods[k];
	}
	int64_t prox_incsum = 0;
	for(k=0; k<prox_samples_taken-1; k++) {
		prox_incsum+=prox_periods[k];
	}
	double prox_average = ((double)prox_incsum)/(prox_samples_taken-1);

	int gyro_max_period = 0, gyro_min_period = 0;
	for (k=0; k<gyro_samples_taken-1; k++) {
		gyro_periods[k] = gyro_events_list[k+1].gents - gyro_events_list[k].gents;
		if (k==0) gyro_min_period = gyro_periods[k];
		if (gyro_periods[k]>gyro_max_period) gyro_max_period = gyro_periods[k];
		if (gyro_periods[k]<gyro_min_period) gyro_min_period = gyro_periods[k];
	}
	int64_t gyro_incsum = 0;
	for(k=0; k<gyro_samples_taken-1; k++) {
		gyro_incsum+=gyro_periods[k];
	}
	double gyro_average = ((double)gyro_incsum)/(gyro_samples_taken-1);

pthread_mutex_lock(&lmtx);

	LOGW("-- Streaming Manager stats start ----");
	sprintf(logstr, "-- Streaming Manager stats start ----\n"); FWRITE

	LOGW("Accel periods average %e, max:%e, min:%e", accel_average,
			(double)accel_max_period, (double)accel_min_period);
	LOGW("Orient periods average %e, max:%e, min:%e", orient_average,
			(double)orient_max_period, (double)orient_min_period);
	LOGW("Light periods average %e, max:%e, min:%e", light_average,
			(double)light_max_period, (double)light_min_period);
	LOGW("Prox periods average %e, max:%e, min:%e", prox_average,
			(double)prox_max_period, (double)prox_min_period);
	LOGW("Gyro periods average %e, max:%e, min:%e", gyro_average,
			(double)gyro_max_period, (double)gyro_min_period);

	sprintf(logstr, "Accel periods average %e, max:%e, min:%e\n", accel_average,
			(double)accel_max_period, (double)accel_min_period); FWRITE
	sprintf(logstr, "Orient periods average %e, max:%e, min:%e\n", orient_average,
			(double)orient_max_period, (double)orient_min_period); FWRITE
	sprintf(logstr, "Light periods average %e, max:%e, min:%e\n", light_average,
			(double)light_max_period, (double)light_min_period); FWRITE
	sprintf(logstr, "Prox periods average %e, max:%e, min:%e\n", prox_average,
			(double)prox_max_period, (double)prox_min_period); FWRITE
	sprintf(logstr, "Gyro periods average %e, max:%e, min:%e\n", gyro_average,
			(double)gyro_max_period, (double)gyro_min_period); FWRITE


	ze_stream_t *sf;
	int si = 1;
	LOGW("-- Start Accelerometer");
	LL_FOREACH(mngr->sensors[ASENSOR_TYPE_ACCELEROMETER].streams, sf) {
		LOGW("Accelerometer stream %d, samples sent:%d", si, sf->samples_sent);
		sprintf(logstr, "Accelerometer stream %d, samples sent:%d\n", si, sf->samples_sent); FWRITE
		si++;
	}
	LOGW("-- End Accelerometer");
	si = 1;
	LOGW("-- Start Proximity");
	LL_FOREACH(mngr->sensors[ASENSOR_TYPE_PROXIMITY].streams, sf) {
		LOGW("Proximity stream %d, samples sent:%d", si, sf->samples_sent);
		sprintf(logstr, "Proximity stream %d, samples sent:%d\n", si, sf->samples_sent); FWRITE
		si++;
	}
	si = 1;
	LOGW("-- End Proximity");
	LOGW("-- Start Gyroscope");
	LL_FOREACH(mngr->sensors[ASENSOR_TYPE_GYROSCOPE].streams, sf) {
		LOGW("Gyroscope stream %d, samples sent:%d", si, sf->samples_sent);
		sprintf(logstr, "Gyroscope stream %d, samples sent:%d\n", si, sf->samples_sent); FWRITE
		si++;
	}
	si = 1;
	LOGW("-- End Gyroscope");
	LOGW("-- Start Light");
	LL_FOREACH(mngr->sensors[ASENSOR_TYPE_LIGHT].streams, sf) {
		LOGW("Light stream %d, samples sent:%d", si, sf->samples_sent);
		sprintf(logstr, "Light stream %d, samples sent:%d\n", si, sf->samples_sent); FWRITE
		si++;
	}
	LOGW("-- End Light");

	LOGW("-- Streaming Manager stats end -----");
	sprintf(logstr, "-- Streaming Manager stats end -----\n\n"); FWRITE

pthread_mutex_unlock(&lmtx);

	/*
	 * TODO: Turn off all sensors that might still be active!
	 * E.g. asked to quit the server while a stream is in
	 * process.. done this for GPS but not yet for the other
	 * sensors..
	 */
	jmethodID ZeGPSManager_destroy =
			(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "destroy", "()V");
	if ( !ZeGPSManager_destroy ) LOGW("ZeGPSManager's destroy() not found");
	(*mngr->env)->CallIntMethod(mngr->env,
			mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager, ZeGPSManager_destroy);
	/*
	 * TODO: do the same for the proximity carrier thead!!!
	 */

	int *exitcode;
	pthread_join(mngr->sensors[ASENSOR_TYPE_PROXIMITY].carrier_thread, &exitcode);
	pthread_join(mngr->sensors[ZESENSE_SENSOR_TYPE_ORIENTATION].carrier_thread, &exitcode);

	/* Detach this thread from JVM. */
	(*jvm)->DetachCurrentThread(jvm);


	LOGI("Streaming Manager out of thread loop, returning..");
}

/*------------------ Anti-deadlock queue wrappers -----------------------------------*/

int put_coap_helper(ze_sm_response_buf_t *notbuf, int rtype,
		ticket_t ticket, int conf, /*ze_payload_t *pyl,*/ ze_sm_packet_t *pk,
		ze_sm_request_buf_t *smreqbuf, sm_req_internal_t *adqueue) {

	int result;
	sm_req_internal_t *temp = NULL;

	/* Try to put in the buffer. If timeout on full condition,
	 * fetch a request from the sm buffer and internally queue it.
	 * Items are appended, so since it must be a FIFO queue,
	 * we'll fetch from list head.
	 * Loop until the original put succeeds.
	 */
	result = put_response_buf_item(notbuf, rtype, ticket, conf, /*pyl*/(unsigned char*)pk);
	while (result == ETIMEDOUT) {
		LOGW("Deadlock resolution mechanism kicked in!");
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
		temp->next = NULL;
		temp->req = get_request_buf_item(smreqbuf);
		LL_APPEND(adqueue, temp);

		result = put_response_buf_item(notbuf, rtype, ticket, conf, /*pyl*/(unsigned char*)pk);
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
		LOGW("Miniqueue not empty, picking a deferred request..");
		temp = adqueue; //save pointer
		adqueue = temp->next; //unplug
		ret = temp->req; //save value
		free(temp); //free
		return ret;
	}
	/* Otherwise very normal get... */
	return get_request_buf_item(smreqbuf);
}



/*-------------------- Stream helpers -----------------------------------------------*/
ze_stream_t *sm_start_stream(stream_context_t *mngr, int sensor_id,
		ticket_t reg, int freq) {

	LOGI("SM starting stream");
	//CHECK_OUT_RANGE(sensor_id);

	ze_stream_t *sub, *newstream;

	newstream = sm_new_stream();
	if (newstream == NULL) return NULL;
	newstream->reg = reg;
	newstream->freq = freq;
	/* Randomized initial time stamp as recommended by standards. */
	newstream->last_rtpts = (rand() % 100)+400;
	newstream->last_wts = 0;
	newstream->samples_sent = 0;

	/* FIXME reliability policies hardcoded for the moment. */
	newstream->retransmit = COAP_MESSAGE_CON;
	newstream->repeat = REPETITION_ON;

	//if ( mngr->sensors[sensor_id].android_handle == NULL ) {
	if ( !(mngr->sensors[sensor_id].is_active) ) {
		/* Sensor is not active, activate in any case
		 * Put a check anyway, if a sensor is not active
		 * the streams should be empty
		 */
		if (mngr->sensors[sensor_id].streams != NULL)
			LOGW("SM inconsistent state, sensor is not active but there"
					"are streams active on it");

		mngr->sensors[sensor_id].freq = freq;

		android_sensor_activate(mngr, sensor_id, freq);
	}
	/* Sensor is already active.
	 * Re-evaluate its frequency based on the new request
	 */
	else if ( freq > mngr->sensors[sensor_id].freq ) {
		mngr->sensors[sensor_id].freq = freq;
		android_sensor_changef(mngr, sensor_id, freq);
	}
	else LOGI("SM sensor is already active and no need to change f");

	/* Replace if there is a stream with the same ticket
	 * otherwise just append..
	 */
	sub = sm_find_stream(mngr, sensor_id, reg);
	if (sub != NULL) {
		LOGI("SM replacing stream");
		/* TODO
		 * Do I have to transfer anything to newstream before
		 * sub gets deleted? Maybe some local status variables.
		 * Why not just modify the existing stream?
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

int sm_stop_stream(stream_context_t *mngr, int sensor_id, ticket_t reg) {

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
		 * of those left might be equal to the previous one)
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
ze_stream_t *sm_find_stream(stream_context_t *mngr, int sensor_id, ticket_t reg) {

	ze_stream_t *temp = mngr->sensors[sensor_id].streams;
	while (temp != NULL) {
		if (temp->reg == reg) break;
		temp = temp->next;
	}
	return temp;
}

ze_stream_t *sm_new_stream() {

	ze_stream_t *new;
	new = (ze_stream_t *) malloc(sizeof(ze_stream_t));
	if (new == NULL) {
		LOGW("new stream malloc failed");
		return NULL;
	}
	memset(new, 0, sizeof(ze_stream_t));

	new->next = NULL;

	new->event_buffer_level = 0;

	return new;
}

ze_oneshot_t *sm_new_oneshot(ticket_t one) {

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

ze_oneshot_t *sm_find_oneshot(stream_context_t *mngr, int sensor_id, ticket_t one) {

	ze_oneshot_t *temp = mngr->sensors[sensor_id].oneshots;
	while (temp != NULL) {
		if (temp->one == one) break;
		temp = temp->next;
	}
	return temp;
}


/*---------------------Sensors helpers--------------------------------------------*/

/*
 * They do not check the sensor state,
 * they merely execute assuming a sensor
 * state suitable for their work.
 */

// REMARK
/* We model all the sensors, GPS included, with the ze_sensor_t
 * struct, but the interface to act on the GPS is different
 * between all the other sensors. As a consequence of this modeling
 * only one of ze_sensor_t's android_handle or gpsManager, which
 * are the handles to a particular sensor interface instance,
 * will be valid for each instance of the struct.
 * android_handle will be the valid one for all sensors in
 * ZE_NUMSENSORS scope (1-14) except for sensor 14 (location)
 * for which
 * TODO: to be consistent to this modeling we should also
 * store the sensor_id in the ze_sensor_t struct so that each
 * instance is self-descriptive; at the moment the mapping
 * between ze_sensor_t instance and the sensor it represents is
 * memorized with outer data.
 */
int android_sensor_activate(stream_context_t *mngr, int sensor, int freq) {

	LOGI("SM Turning on android sensor%d freq%d", sensor, freq);

	//CHECK_OUT_RANGE(sensor);

	if (sensor == ZESENSE_SENSOR_TYPE_LOCATION) {
		/* Goes through our manager. Different from the other
		 * sensors, the reference to the GPS we take it only
		 * once at the beginning, even if the client never
		 * asks for GPS. Here we do only the equivalent of
		 * ASensorEventQueue_enableSensor. The equivalent
		 * of ASensorManager_getDefaultSensor is done at
		 * thread start only once.
		 */
		jmethodID ZeGPSManager_start =
				(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "startStream", "()I");
		if ( !ZeGPSManager_start ) LOGW("ZeGPSManager's startStream() not found");
		jint started = (*mngr->env)->CallIntMethod(mngr->env,
				mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager, ZeGPSManager_start);

		if (started) {

			/* Properly flag it. */
			mngr->sensors[sensor].is_active = 1;

			return 0;
		}
	}
		/*
		double f = 1/freq;
		int sec = (int)f; //isolate integer part
		double decpart = f - sec; //isolate decimal part
		long nsec = decpart * 1000000000LL; //enlarge to 10^9 (need nsec) and cut the rest
		LOGW("Activating proximity period sec:%d, nsec:%d", sec, nsec);

		signal(SIGALRM, (void (*)(int)) proximity_carrier);
		timer_create(CLOCK_REALTIME, NULL, &mngr->sensors[sensor].timerid);
		struct itimerspec value;
		value.it_interval.tv_sec = sec; //subsequent shots. if 0 only first shot
		value.it_interval.tv_nsec = nsec;
		value.it_value.tv_sec = sec; //first shot
		value.it_value.tv_nsec = nsec;
		timer_settime(mngr->sensors[sensor].timerid, 0, &value, NULL);
		*/

	else {

		/* Grab reference from Android */
		mngr->sensors[sensor].android_handle =
				ASensorManager_getDefaultSensor(mngr->sensorManager, sensor);

		/* Enable sensor at the specified frequency */
		if ( mngr->sensors[sensor].android_handle != NULL ) {
			ASensorEventQueue_enableSensor(mngr->sensorEventQueue,
					mngr->sensors[sensor].android_handle);
			ASensorEventQueue_setEventRate(mngr->sensorEventQueue,
					mngr->sensors[sensor].android_handle, (1000L/freq)*1000);

			/* Properly flag it. */
			mngr->sensors[sensor].is_active = 1;

			/* Important to start the thread after the is_active is set,
			 * the thread checks this flag to know when to exit, so if we
			 * start the thread before setting the flag and the scheduler
			 * immediately runs some instruction from the thread,
			 * it will exit immediately.
			 */
			if (sensor == ASENSOR_TYPE_PROXIMITY //or any other event based sensor
					&& mngr->sensors[sensor].carrier_thread_started == 0) {
				/* Besides starting the real sample delivery,
				 * for this class of sensors start also the carrier stream.
	             * (recall that the carrier stream
	             * confirms the sample that is in the cache)
	             *
	             * It's ok to pass these pointers to another thread,
				 * Streaming Manager and its sensor array will never move.
	             */
				pcargs.carrq = mngr->carrq;
				pcargs.sensor = &(mngr->sensors[sensor]);
				int carrerr = pthread_create(&(mngr->sensors[sensor].carrier_thread), NULL,
						ze_carrier_thread, &pcargs);
				if (carrerr != 0) return SM_ERROR;
				mngr->sensors[sensor].carrier_thread_started = 1;
			}
			else if (sensor == ZESENSE_SENSOR_TYPE_ORIENTATION //or any other event based sensor
					&& mngr->sensors[sensor].carrier_thread_started == 0) {
				ocargs.carrq = mngr->carrq;
				ocargs.sensor = &(mngr->sensors[sensor]);
				int carrerr = pthread_create(&(mngr->sensors[sensor].carrier_thread), NULL,
						ze_carrier_thread, &ocargs);
				if (carrerr != 0) return SM_ERROR;
				mngr->sensors[sensor].carrier_thread_started = 1;
			}

			return 0;
		}
	}

	LOGW("SM cannot activate sensor %d", sensor);
	return SM_ERROR;
}

int android_sensor_changef(stream_context_t *mngr, int sensor, int freq) {

	LOGI("SM Changing frequency to android sensor%d", sensor);

	//CHECK_OUT_RANGE(sensor);

	if (sensor == ZESENSE_SENSOR_TYPE_LOCATION) {

		jmethodID ZeGPSManager_changefrequency =
				(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "changeFrequency", "()I");
		if ( !ZeGPSManager_changefrequency ) LOGW("ZeGPSManager's changeFrequency() not found");

		jint changed = (*mngr->env)->CallIntMethod(mngr->env,
				mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager, ZeGPSManager_changefrequency);

		// TODO parameters to changeFrequency()

		if (!changed) return SM_ERROR;
	}
	else {

		/*if (sensor == ASENSOR_TYPE_PROXIMITY) { //or any sensor of this class
			do nothing because the carrier stream
			reads at every cycle the value of sensors[].freq
			to adjust its cycle period
		}*/

		int error = ASensorEventQueue_setEventRate(mngr->sensorEventQueue,
				mngr->sensors[sensor].android_handle, (1000L/freq)*1000);

		if (error < 0) return SM_ERROR;
	}
	return 0;
}


int android_sensor_turnoff(stream_context_t *mngr, int sensor) {

	LOGI("SM Turning off Android sensor %d", sensor);

	//CHECK_OUT_RANGE(sensor);

	if (sensor == ZESENSE_SENSOR_TYPE_LOCATION) {

		jmethodID ZeGPSManager_stop =
				(*mngr->env)->GetMethodID(mngr->env, mngr->ZeGPSManager, "stopStream", "()I");
		if ( !ZeGPSManager_stop ) LOGW("ZeGPSManager's stopStream() not found");

		jint stopped = (*mngr->env)->CallIntMethod(mngr->env,
				mngr->sensors[ZESENSE_SENSOR_TYPE_LOCATION].gpsManager, ZeGPSManager_stop);

		if (!stopped) return SM_ERROR;

		/*
		 * Remark: can't NULL gpsManager because we don't get
		 * this reference every time we activate a sensor but
		 * only once at thread start!
		 */
	}
		/*
		struct itimerspec value;
		value.it_interval.tv_sec = 0; //subsequent shots. if 0 only first shot
		value.it_interval.tv_nsec = 0;
		value.it_value.tv_sec = 0; //first shot, if 0 timer disarmed
		value.it_value.tv_nsec = 0;
		timer_settime(mngr->sensors[sensor].timerid, 0, &value, NULL);
		*/
	else {

		int error = ASensorEventQueue_disableSensor(mngr->sensorEventQueue,
				mngr->sensors[sensor].android_handle);

		if (error < 0) return SM_ERROR;

		mngr->sensors[sensor].android_handle = NULL;
		/* Not clear how Android leaves this pointer,
		 * though it is important because we look at
		 * the NULLity of this pointer to see whether
		 * a sensor is active or not.
		 * (changed!! now there's the is_active flag)
		 */
	}

	mngr->sensors[sensor].is_active = 0;
	mngr->sensors[sensor].cache_valid = 0;

	return 0;
}

/*
 * A signal handler can interrupt the program at any point.
 * This means, unless you've taken care to ensure this
 * can't happen, it could run with various data objects
 * it wants to work with in inconsistent state. For instance
 * the buffer pointers inside a FILE might be only partially
 * updated, or the linked list of open FILEs might have a
 * halfway-inserted or halfway-removed item partly linked
 * to it. Or the internal malloc data structures might
 * have halfway-freed memory referenced... If the signal
 * handler calls functions that depend on that state being
 * consistent, very bad things will happen!
 */
//DEPRECATED IMPLEMENTATION IN FAVOR OR A THREAD
/*void proximity_carrier(int signum) {
	LOGI("Proximity carrier timer fired");
	//how to not prioritize it wrt other sensor samples?
}*/


/* Event and rtpts are assumed to be arrays of size num,
 * events in the array are expected to come from the same sensor. */
ze_sm_packet_t *
encode(ASensorEvent *event, int *rtpts, int num) {

	ze_sm_packet_t *c = malloc(sizeof(ze_sm_packet_t));
	if (c==NULL) return NULL;
	memset(c, 0, sizeof(ze_sm_packet_t));

	c->rtpts = rtpts[0];
	c->ntpts = event[0].timestamp;

	int totlength = 0;
	int offset = 0;
	int k = 0;
	int ts = 0;

	if (event[0].type == ASENSOR_TYPE_ACCELEROMETER) {
		c->sensor = ASENSOR_TYPE_ACCELEROMETER;

		totlength = sizeof(ze_payload_header_t)+
				num*(sizeof(int)+sizeof(ze_accel_vector_t));
		ze_payload_header_t *temp = malloc(totlength);
		if (temp==NULL) return NULL;
		memset(temp, 0, totlength);

		temp->packet_type = DATAPOINT;
		temp->sensor_type = event[0].type;
		temp->length = htons(totlength);

		offset += sizeof(ze_payload_header_t);
		unsigned char *p = (unsigned char *)temp;

		for (k=0; k<num; k++) {
			ts = htonl(rtpts[k]);
			memcpy(p+offset, &ts, sizeof(int));
			offset += sizeof(int);
			sprintf(p+offset, "%e", event[k].acceleration.x);
			offset += CHARLEN;
			sprintf(p+offset, "%e", event[k].acceleration.y);
			offset += CHARLEN;
			sprintf(p+offset, "%e", event[k].acceleration.z);
			offset += CHARLEN;
		}

		c->data = temp;
		c->length = totlength;
	}
	if (event[0].type == ASENSOR_TYPE_PROXIMITY) {
		c->sensor = ASENSOR_TYPE_PROXIMITY;

		totlength = sizeof(ze_payload_header_t)+
				num*(sizeof(int)+sizeof(ze_prox_vector_t));
		ze_payload_header_t *temp = malloc(totlength);
		if (temp==NULL) return NULL;
		memset(temp, 0, totlength);

		temp->packet_type = DATAPOINT;
		temp->sensor_type = event[0].type;
		temp->length = htons(totlength);

		offset += sizeof(ze_payload_header_t);
		unsigned char *p = (unsigned char *)temp;

		for (k=0; k<num; k++) {
			ts = htonl(rtpts[k]);
			memcpy(p+offset, &ts, sizeof(int));
			offset += sizeof(int);
			sprintf(p+offset, "%e", event[k].distance);
			offset += CHARLEN;
		}

		c->data = temp;
		c->length = totlength;
	}
	if (event[0].type == ASENSOR_TYPE_LIGHT) {
		c->sensor = ASENSOR_TYPE_LIGHT;

		totlength = sizeof(ze_payload_header_t)+
				num*(sizeof(int)+sizeof(ze_light_vector_t));
		ze_payload_header_t *temp = malloc(totlength);
		if (temp==NULL) return NULL;
		memset(temp, 0, totlength);

		temp->packet_type = DATAPOINT;
		temp->sensor_type = event[0].type;
		temp->length = htons(totlength);

		offset += sizeof(ze_payload_header_t);
		unsigned char *p = (unsigned char *)temp;

		for (k=0; k<num; k++) {
			ts = htonl(rtpts[k]);
			memcpy(p+offset, &ts, sizeof(int));
			offset += sizeof(int);
			sprintf(p+offset, "%e", event[k].light);
			offset += CHARLEN;
		}

		c->data = temp;
		c->length = totlength;
	}
	if (event[0].type == ZESENSE_SENSOR_TYPE_ORIENTATION) {
		c->sensor = ZESENSE_SENSOR_TYPE_ORIENTATION;

		totlength = sizeof(ze_payload_header_t)+
				num*(sizeof(int)+sizeof(ze_orient_vector_t));
		ze_payload_header_t *temp = malloc(totlength);
		if (temp==NULL) return NULL;
		memset(temp, 0, totlength);

		temp->packet_type = DATAPOINT;
		temp->sensor_type = event[0].type;
		temp->length = htons(totlength);

		offset += sizeof(ze_payload_header_t);
		unsigned char *p = (unsigned char *)temp;

		for (k=0; k<num; k++) {
			ts = htonl(rtpts[k]);
			memcpy(p+offset, &ts, sizeof(int));
			offset += sizeof(int);
			sprintf(p+offset, "%e", event[k].vector.azimuth);
			offset += CHARLEN;
			sprintf(p+offset, "%e", event[k].vector.pitch);
			offset += CHARLEN;
			sprintf(p+offset, "%e", event[k].vector.roll);
			offset += CHARLEN;
		}

		c->data = temp;
		c->length = totlength;
	}
	if (event[0].type == ASENSOR_TYPE_GYROSCOPE) {
		c->sensor = ASENSOR_TYPE_GYROSCOPE;

		totlength = sizeof(ze_payload_header_t)+
				num*(sizeof(int)+sizeof(ze_gyro_vector_t));
		ze_payload_header_t *temp = malloc(totlength);
		if (temp==NULL) return NULL;
		memset(temp, 0, totlength);

		temp->packet_type = DATAPOINT;
		temp->sensor_type = event[0].type;
		temp->length = htons(totlength);

		offset += sizeof(ze_payload_header_t);
		unsigned char *p = (unsigned char *)temp;

		for (k=0; k<num; k++) {
			ts = htonl(rtpts[k]);
			memcpy(p+offset, &ts, sizeof(int));
			offset += sizeof(int);
			sprintf(p+offset, "%e", event[k].vector.x);
			offset += CHARLEN;
			sprintf(p+offset, "%e", event[k].vector.y);
			offset += CHARLEN;
			sprintf(p+offset, "%e", event[k].vector.z);
			offset += CHARLEN;
		}

		c->data = temp;
		c->length = totlength;
	}


	return c;


}








	/*
	 ze_payload_header_t
	 timestamp 32bit (int)
	 packet->data,length
	 */
/*
	ze_payload_t *c = malloc(sizeof(ze_payload_t));
	if (c==NULL) return NULL;

	int totlength = sizeof(ze_payload_header_t)+sizeof(int)+packet->length;
	c->data = malloc(totlength);
	if(c->data == NULL) return NULL;
	c->length = totlength;
	memset(c->data, 0, totlength);

	int offset = 0;
	unsigned char *p = c->data;

	ze_payload_header_t *hdr = (ze_payload_header_t*)(p);
	hdr->packet_type = DATAPOINT;
	hdr->sensor_type = packet->sensor;
	hdr->length = htons(totlength);

	offset = sizeof(ze_payload_header_t);
	p = p + offset;
	int rtpts = htonl(packet->rtpts);
	memcpy(p, &rtpts, sizeof(int));

	offset = sizeof(int);
	p = p + offset;
	memcpy(p, packet->data, packet->length);

	return c;
}*/

/*
ze_sm_packet_t *
form_sm_packet(ASensorEvent event) {

	ze_sm_packet_t *c = malloc(sizeof(ze_sm_packet_t));
	if (c==NULL) return NULL;
	memset(c, 0, sizeof(ze_sm_packet_t));

	c->rtpts = 0;
	c->ntpts = event.timestamp;

	if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
		c->sensor = ASENSOR_TYPE_ACCELEROMETER;
		ze_accel_vector_t *temp = malloc(sizeof(ze_accel_vector_t));
		if (temp==NULL) return NULL;
		memset(temp, 0, sizeof(ze_accel_vector_t));
		sprintf(temp->x, "%e", event.acceleration.x);
		sprintf(temp->y, "%e", event.acceleration.y);
		sprintf(temp->z, "%e", event.acceleration.z);
		c->data = temp;
		c->length = sizeof(ze_accel_vector_t);
	}
	else if (event.type == ASENSOR_TYPE_PROXIMITY) {
		c->sensor = ASENSOR_TYPE_PROXIMITY;
		ze_prox_vector_t *temp = malloc(sizeof(ze_prox_vector_t));
		if (temp==NULL) return NULL;
		memset(temp, 0, sizeof(ze_prox_vector_t));
		sprintf(temp->distance, "%e", event.distance);
		c->data = temp;
		c->length = sizeof(ze_prox_vector_t);
	}
	else if (event.type == ASENSOR_TYPE_LIGHT) {
		c->sensor = ASENSOR_TYPE_LIGHT;
		ze_light_vector_t *temp = malloc(sizeof(ze_light_vector_t));
		if (temp==NULL) return NULL;
		memset(temp, 0, sizeof(ze_light_vector_t));
		sprintf(temp->light, "%e", event.light);
		c->data = temp;
		c->length = sizeof(ze_light_vector_t);
	}
	else if (event.type == ZESENSE_SENSOR_TYPE_ORIENTATION) {
		c->sensor = ZESENSE_SENSOR_TYPE_ORIENTATION;
		ze_orient_vector_t *temp = malloc(sizeof(ze_orient_vector_t));
		if (temp==NULL) return NULL;
		memset(temp, 0, sizeof(ze_orient_vector_t));
		sprintf(temp->azimuth, "%e", event.vector.azimuth);
		sprintf(temp->pitch, "%e", event.vector.pitch);
		sprintf(temp->roll, "%e", event.vector.roll);
		c->data = temp;
		c->length = sizeof(ze_orient_vector_t);
	}
	else if (event.type == ASENSOR_TYPE_GYROSCOPE) {
		c->sensor = ASENSOR_TYPE_GYROSCOPE;
		ze_gyro_vector_t *temp = malloc(sizeof(ze_gyro_vector_t));
		if (temp==NULL) return NULL;
		memset(temp, 0, sizeof(ze_gyro_vector_t));
		sprintf(temp->x, "%e", event.vector.x);
		sprintf(temp->y, "%e", event.vector.y);
		sprintf(temp->z, "%e", event.vector.z);
		c->data = temp;
		c->length = sizeof(ze_gyro_vector_t);
	}

	return c;
}*/


/* We synch access to event_cache and freq because they are
 * not natural data types "int" for any platform so operations
 * on them will most likely not be atomic.
 */
ASensorEvent read_last_event_SYN(ze_sensor_t *sensor) {
	//LOGW("Reading last event..");
	pthread_mutex_lock(&(sensor->carrthrmtx));
		ASensorEvent ev = sensor->event_cache;
	pthread_mutex_unlock(&(sensor->carrthrmtx));
	//LOGW("Mutex released");
	return ev;
}
int write_last_event_SYN(ze_sensor_t *sensor, ASensorEvent ev) {
	//LOGW("Writing last event..");
	pthread_mutex_lock(&(sensor->carrthrmtx));
		sensor->event_cache = ev;
	pthread_mutex_unlock(&(sensor->carrthrmtx));
	//LOGW("Mutex released");
	return 1;
}

/*
double read_stream_freq_SYN(ze_sensor_t *sensor) {
	pthread_mutex_lock(&(sensor->carrthrmtx));
		double freq = sensor->freq;
	pthread_mutex_unlock(&(sensor->carrthrmtx));
	return freq;
}
int write_stream_freq_SYN(ze_sensor_t *sensor, double freq) {
	pthread_mutex_lock(&(sensor->carrthrmtx));
		sensor->freq = freq;
	pthread_mutex_unlock(&(sensor->carrthrmtx));
	return 1;
}
*/


/*ze_payload_t *
form_data_payload(ASensorEvent event) {

	ze_payload_t *c = malloc(sizeof(ze_payload_t));
	if (c==NULL) return NULL;
	c->data = 0;
	c->length = 0;

	if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
		ze_accel_paydata_t *temp = malloc(sizeof(ze_accel_paydata_t));
		if (temp==NULL) return NULL;
		memset(temp, 0, sizeof(ze_accel_paydata_t));
		temp->phdr.packet_type = htonl(DATAPOINT);
		temp->pdhdr.sensor_type = htonl(ASENSOR_TYPE_ACCELEROMETER);
		//temp->pdhdr.sn = 0;
		temp->pdhdr.ts = 0;
		sprintf(temp->x, "%e", event.acceleration.x);
		sprintf(temp->y, "%e", event.acceleration.y);
		sprintf(temp->z, "%e", event.acceleration.z);
		c->data = temp;
		c->length = sizeof(ze_accel_paydata_t);
	}
	else {

	}
	return c;
}*/

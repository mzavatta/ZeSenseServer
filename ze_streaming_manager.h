/*
 * ZeSense Streaming Manager
 * -- core streaming module
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */
#ifndef ZE_STREAMING_MANAGER_H
#define ZE_STREAMING_MANAGER_H

#include <android/sensor.h>
#include <jni.h>
//#include "coap.h"

#include "pdu.h"
#include "net.h"
#include "address.h"
#include "asynchronous.h"
#include "ze_log.h"
#include "ze_ticket.h"
#include "ze_carriers_queue.h"

/*
 * <source>/hardware/libhardware/include/hardware/sensors.h
#define SENSOR_TYPE_ACCELEROMETER       1
#define SENSOR_TYPE_MAGNETIC_FIELD      2
#define SENSOR_TYPE_ORIENTATION         3
#define SENSOR_TYPE_GYROSCOPE           4
#define SENSOR_TYPE_LIGHT               5
#define SENSOR_TYPE_PRESSURE            6
#define SENSOR_TYPE_TEMPERATURE         7   // deprecated
#define SENSOR_TYPE_PROXIMITY           8
#define SENSOR_TYPE_GRAVITY             9
#define SENSOR_TYPE_LINEAR_ACCELERATION 10
#define SENSOR_TYPE_ROTATION_VECTOR     11
#define SENSOR_TYPE_RELATIVE_HUMIDITY   12
#define SENSOR_TYPE_AMBIENT_TEMPERATURE 13

 * NDK mirrors in synch with above
enum {
    ASENSOR_TYPE_ACCELEROMETER      = 1,
    ASENSOR_TYPE_MAGNETIC_FIELD     = 2,
    ASENSOR_TYPE_GYROSCOPE          = 4,
    ASENSOR_TYPE_LIGHT              = 5,
    ASENSOR_TYPE_PROXIMITY          = 8
};
 */

// Definitions for missing NDK sensor types, as we include only the NDK interface
#define ZESENSE_SENSOR_TYPE_ORIENTATION		3
#define ZESENSE_SENSOR_TYPE_PRESSURE		6
#define ZESENSE_SENSOR_TYPE_LOCATION		14

/* Carrier frequency definitions. */
#define PROX_CARRIER_FREQ	5 //Hz

/* Error conditions */
#define SM_ERROR			(-1)
#define SM_URI_REPLACED 	1
#define SM_STREAM_REPLACED	2
#define SM_NEGATIVE			3
#define SM_EMPTY			4
#define SM_OUT_RANGE		5

/* Request codes
 * mirroring start_stream() and
 * stop_stream() calls */
#define SM_REQ_START		10
#define SM_REQ_STOP			20
#define SM_REQ_ONESHOT		30
#define SM_REQ_INVALID		(-1)

/* Sensor settings */
#define DEFAULT_FREQ		10
#define ACCEL_MAX_FREQ		100
#define GYRO_MAX_FREQ		200
#define LIGHT_MAX_FREQ		200

/* Streaming Manager settings */
#define QUEUE_REQ_RATIO		5

/* Other settings, to be moved */
#define ZE_NUMSENSORS		(14+1) /* +1 in order to use sensor types
									* as array indexes */

/* Request codes for the lower layer. */
#define STREAM_NOTIFICATION			50
#define ONESHOT						60
#define STREAM_STOPPED				70
#define INVALID_COMMAND				40

/* Utilities */
#define TRUE 	0
#define FALSE 	1

//typedef union coap_ticket_u ticket_t;
/*
union coap_ticket_u {
	// Ticket corresponding to the underlying registration
	coap_registration_t *reg;

	// Ticket corresponding to the underlying asynchronous request.
	coap_tid_t tid;
};
*/


struct stream_context_t;

typedef struct ze_oneshot_t {
	struct ze_oneshot_t *next;
	/* Ticket that identifies the oneshot request
	 * when interacting with the CoAP server. */
	ticket_t one;
} ze_oneshot_t;

typedef struct ze_stream_t {
	struct ze_stream_t *next;

	/* Ticket that identifies the stream
	 * when interacting with the CoAP server.
	 */
	ticket_t reg;

	/* In some way this is the lookup key,
	 * no two elements with the same dest will be present in the list
	 * as mandated by draft-coap-observe-7
	 * not anymore, it's managed differently now
	 */
	//coap_address_t dest;

	/* Client specified stream frequency */
	int freq;

	/* Streaming Manager local status variables. */
	uint64_t last_wts;	//Last wallclock timestamp
	int last_rtpts;	//Last RTP timestamp
	int freq_div;	//Frequency divider
} ze_stream_t;

typedef struct ze_sensor_t {
	/* Association sensor-resource.
	 * (deprecated, use ticketing
	 * mechanism instead) */
	//str uri;

	/* Useless if we use an array
	 * whose index is mirrored to sensor types
	 * but still useful to make an instance of
	 * this struct self-descriptive. */
	int sensor;


	/* Fields of this group will be meaningful
	 * or meaningless depending on the sensor
	 * type that is represented.
	 */
	/* XXX: does const make sense? */
	ASensor* android_handle;
	jobject gpsManager; //an instance of ZeGPSManager
	pthread_t carrier_thread;
	/* Avoid to start carrier_thread more than once
	 * at the same time. This flag is to be set
	 * when we start carrier_thread. carrier_thread
	 * clears it when it finishes! */
	int carrier_thread_started;
	/* Mutex to concert access to this element with its carrier thred. */
	pthread_mutex_t carrthrmtx;


	/* Consider an "is_active" flag,
	 * indeed the NDK sensor API does not offer
	 * a way to know whether a ASensor* sensor
	 * is active or not. Thus we do not offer it
	 * in our GPSManager either.
	 *
	 * Either we do it keeping android_handle and
	 * gpsManager NULL when not active
	 * or we create and manage a flag.
	 *
	 * The NULL approach might work with android_handle
	 * because we get this pointer every time we
	 * turn on a sensor, but it does not work with
	 * gpsManager because we get it once and reuse
	 * the same reference (if we NULL it when the sensor
	 * is turned off and the client later requests
	 * again this sensor, we don't have the reference
	 * any longer).
	 *
	 * To make it uniform the is_active flag
	 * is probably the best solution.
	 *
	 * This solution is also useful for the carrier stream thread
	 * to know when to exit.
	 */
	int is_active; // 1 if active, 0 if not.

	/* Quick access to last known sensor value */
	ASensorEvent event_cache;

	/* List of streams registered on this sensor */
	ze_stream_t *streams;

	/* List of one-shot requests registered on this sensor */
	ze_oneshot_t *oneshots;

	/* This is the current frequency at which the sensor
	 * is delivering data, be it an Android sensor or the
	 * GPS. For unknown frequency Android sensors this
	 * is the frequency of the carrier stream.
	 * Note the similarity between the management in terms of
	 * frequency of an actual known-frequency sensor and a
	 * carrier stream.
	 * Since there is only one source for each sensor,
	 * if there is more than one stream subscribed to this sensor
	 * this frequency will represent the maximum of the frequencies
	 * requested by the streams on the sensor.
	 * The sensor will always push at the maximum frequency, it is the
	 * application jobs to distinguish if a sample should be
	 * sent upon some stream or not. (every sample that is pushed
	 * will be sent upon the highest frequency stream, but some
	 * samples will not be sent upon streams of lower frequeccy).
	 */
	int freq;

	/* Local status variables */
	int last_wts;
	int last_rtpts;
} ze_sensor_t;

/**
 * Streaming Manager's global context
 * Array indexes mirror Android-defined sensor types
 */
typedef struct stream_context_t {
	/* Sensors sources available for streaming */
	ze_sensor_t sensors[ZE_NUMSENSORS];

	/* The server we're sending the streams through.
	 * Don't think we need it.. we need the two buffers,
	 * yes, but function parameters are designed to
	 * take them directly (unlike libcoap functions
	 * that came without the knowledge of the buffers
	 * and the Streaming Manager thus taking
	 * only coap_context_t as parameter..) */
	//coap_context_t *server;

	/* Here we could register the two queues that talk
	 * to the protocol implementation.. it's
	 * logical, isn't it? We have all the components that
	 * the SM needs in here..
	 */

	/* Android  sensor infrastructure */
	ASensorManager* sensorManager;
	ASensorEventQueue* sensorEventQueue;
	ALooper* looper;

	/* Location sensor infrastructure. */
	JNIEnv* env;
	jclass ZeGPSManager; //the Java class object

	/* Global carriers infrastructure. */
	ze_carriers_queue_t *carrq;

} stream_context_t;


typedef struct {
	int sensor;
	uint64_t ntpts;
	int rtpts;
	unsigned char *data;
	int length; //length of the *data field
} ze_sm_packet_t;


struct generic_carr_thread_args {
	ze_carriers_queue_t *carrq;
	ze_sensor_t *sensor;
};

/*
inline int CHECK_OUT_RANGE(int sensor) {
if (sensor<0 || sensor>=ZE_NUMSENSORS) {
	LOGW("sensor type out of range");
	return SM_OUT_RANGE;
}	return 0; }
*/

/*
 *
 */

void *
ze_coap_streaming_thread(void* args);

/**
 * Binds a sensor source @p sensor_id to a specific @p URI.
 * Only one URI can be associated to a sensor source;
 * If an association is already in place, it will be overwritten.
 *
 * @param mngr		The Streaming Manager context
 * @param sensor_id	One of the available sensor sources
 * @param uri		String variable to associate to @p sensor_id
 *
 * @return Zero on success and first association,
 * (TODO @c SM_URI_REPLACED if successful and association overwritten)
 * @c SM_OUT_RANGE if @p sensor_id is out of bound, @c SM_ERROR on failure
 */
int sm_bind_source(stream_context_t *mngr, int sensor_id, str uri);

/**
 * TODO
 * Binds the given @p server to Streaming Manager @p mngr
 * in order to relay notifications to him.
 */
//int sm_bind_server(stream_context_t *mngr, coap_context_t *server);


ze_stream_t *sm_new_stream();

ze_stream_t *
sm_find_stream(stream_context_t *mngr, int sensor_id, ticket_t reg);

/**
 * Starts a stream of notifications of samples
 * collected by @p sensor_id to destination @p dest.
 * The client will be notified at the given frequency @p freq.
 * Only one specific destination is allowed for a sensor source.
 * If already existing, it will be replaced.
 * The requested @p freq will be rounded to multiples of 10Hz
 * and capped to the maximum running frequency
 * of the sensor source.
 *
 * TODO: many other parameters may be added in the future
 * for example reliability policy, the deadlines or
 * the timestamps policy (for now they are fixed and hard-coded)
 * maybe even in the form of some query language
 *
 * TODO: do we let the sender specify the timestamp clock rate or
 * we decide it as a "profile" like the RTP/AVP?
 *
 * @param mngr		The Streaming Manager context
 * @param sensor_id	The sensor source of data
 * @param dest		The IP/port coordinates of the destination
 * @param freq		The frequency of notifications
 *
 * @return Zero on success, @c SM_STREAM_REPLACED if the new stream
 * replaced an existing one, @c SM_OUT_RANGE if @p sensor_id is out of bound,
 * @c SM_ERROR on failure
 */
ze_stream_t *sm_start_stream(stream_context_t *mngr, int sensor_id, ticket_t reg, int freq);

/**
 * Stops the stream of notifications from @p sensor_id
 * to destination @p dest.
 *
 * @param mngr		The Streaming Manager context
 * @param sensor_id	The sensor source of data
 * @param dest		The IP/port coordinates of the destination
 *
 * @return Zero on success, @c SM_ERROR on failure
 * (e.g. the stream does not exist)
 * @c SM_OUT_RANGE if @p sensor_id is out of bound
 */
int sm_stop_stream(stream_context_t *mngr, int sensor_id, ticket_t reg);

/**
 * Checks if a stream from @p sensor_id to destination @p dest
 * is currently running.
 *
 * @param mngr		The Streaming Manager context
 * @param sensor_id	The sensor source of data
 * @param dest		The IP/port coordinates of the destination
 *
 * @return Zero if positive answer, @c SM_NEGATIVE otherwise
 * @c SM_OUT_RANGE if @p sensor_id is out of bound
 */
int sm_is_streaming(stream_context_t *mngr, int sensor_id, coap_address_t dest);


/**
 * !! DEPRECATED !!
 * Returns a single sample of @p sensor_id
 * Is is put in the container @p data of length @p length
 * The memory allocated for @p data will not be freed.
 *
 * @param cache		The event cache
 * @param sensor_id	The sensor source of data
 * @param data		Data bytes returned
 * @param length	Length of the @p data field returned
 *
 * @return Zero on success, @c SM_OUT_RANGE if @p sensor_id is out of bound,
 * @c SM_ERROR on failure
 */
// XXX isn't this now part of the sample cache?! well if we want to isolate the
// coap GET handler from all the payload formatting we should do it here
// it will be anyway the receiver thread that does it but conceptually it's
// a streaming manager job (because of the payload formatting work)..
// or the sample cache's job because of the cache access work
//int sm_get_single_sample(ze_sample_cache_t *cache, int sensor_id,
//		unsigned char *data, int length);

/**
 * Since there can be more than one one-shot request for a given destination
 * we'll pass the destination and the token to identify it.
 * This function requests a one-shot reading of the @p sensor_id . It is ensured the
 * reading to be fresh i.e. if the sensor has been disabled for a while
 * (sample cache not fresh) it will enable it, do the one-shot reading
 * and disable it afterwards.
 *
 * @param mngr		The Streaming Manager
 * @param sensor_id	The sensor source of data
 * @param dest		The IP/port coordinates of the destination
 * @param tokenlen	The length of the token field
 * @param token		The token itself
 *
 * @return Zero on success, @c SM_ERROR on failure
 */
/*int
sm_new_oneshot(stream_context_t *mngr, int sensor_id, coap_address_t dest,
		int tokenlen, unsigned char *token);*/


ze_oneshot_t *
sm_new_oneshot(ticket_t one);

stream_context_t *
get_streaming_manager(/*coap_context_t  *cctx*/);

int
android_sensor_activate(stream_context_t *mngr, int sensor, int freq);

int
android_sensor_changef(stream_context_t *mngr, int sensor, int freq);

int
android_sensor_turnoff(stream_context_t *mngr, int sensor);

#endif

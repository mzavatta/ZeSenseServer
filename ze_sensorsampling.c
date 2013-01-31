/*
 * ZeSense
 *
 * Author: Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#include <jni.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/resource.h>

#include <android/log.h>
#include <android/sensor.h>

// Logging macros
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "zeSenseSensorSampling", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "zeSenseSensorSampling", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "zeSenseSensorSampling", __VA_ARGS__))

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

// Definitions for missing NDK sensor types
#define ZESENSE_SENSOR_TYPE_ORIENTATION		3
#define ZESENSE_SENSOR_TYPE_PRESSURE		6

// Global experiment settings
#define ZS_EXPERIMENT_DURATION 360 //Seconds
//#define ZS_EXPERIMENT_MULTIPLE 1
#define ZS_EXPERIMENT_PRIORITY (0) //Nice values. More negative means more processor time.
#define LOGPATH "/sdcard/zesenselog.txt"

// Accelerometer settings ASENSOR_TYPE_ACCELEROMETER
//#define ACCEL_ON 1
#define ACCEL_HZ 100 //Hz
#define NUM_ACCEL_SAMPLES (ACCEL_HZ*ZS_EXPERIMENT_DURATION)

// Gyro settings ASENSOR_TYPE_GYROSCOPE
#define GYRO_ON 1
#define GYRO_HZ 20 //Hz
#define NUM_GYRO_SAMPLES (GYRO_HZ*ZS_EXPERIMENT_DURATION)

// Light settings ASENSOR_TYPE_LIGHT
//#define LIGHT_ON 1
#define LIGHT_HZ 20 //Hz
#define NUM_LIGHT_SAMPLES (LIGHT_HZ*ZS_EXPERIMENT_DURATION)

// Magnetic sensor ASENSOR_TYPE_MAGNETIC_FIELD
//#define MAG_ON 1
#define MAG_HZ 20 //Hz
#define NUM_MAG_SAMPLES (MAG_HZ*ZS_EXPERIMENT_DURATION)

// Proximity sensor ASENSOR_TYPE_PROXIMITY
//#define PROX_ON 1
#define PROX_HZ 20 //Hz		//XXX: value broadcasted only when it changes!!
#define NUM_PROX_SAMPLES (PROX_HZ*ZS_EXPERIMENT_DURATION)

// Orientation sensor ZESENSE_SENSOR_TYPE_ORIENTATION
//#define ORIENT_ON 1
#define ORIENT_HZ 100 //Hz
#define NUM_ORIENT_SAMPLES (ORIENT_HZ*ZS_EXPERIMENT_DURATION)

// Pressure sensor ZESENSE_SENSOR_TYPE_PRESSURE
//#define PRES_ON 1
#define PRES_HZ 20 //Hz
#define NUM_PRES_SAMPLES (PRES_HZ*ZS_EXPERIMENT_DURATION)

#ifndef ZS_EXPERIMENT_MULTIPLE
#define NUM_SAMPLES NUM_GYRO_SAMPLES
#else
#define NUM_SAMPLES 3000
#endif

struct zs_ASensorEvent {
	ASensorEvent event; //as it is fed into the event interface
	struct timespec collectionTimestamp; //when we collect it
};

#ifndef ZS_EXPERIMENT_MULTIPLE
	struct zs_experiment_single {
		struct zs_ASensorEvent events_list[NUM_SAMPLES];
		int64_t collection_periods[NUM_SAMPLES-1];
		int64_t generation_periods[NUM_SAMPLES-1];
		int64_t coll_gen_delay[NUM_SAMPLES];
	};
	struct zs_experiment_single experiment; // Experiment state, using a global is more handy
#else
	//TODO: structure for experiment_multiple
	struct zs_ASensorEvent events_list[NUM_SAMPLES];
	int experimenting; // Flag to set at startup and reset when the experiment timer expires
#endif

	// Global event counter
	int event_counter = 0;

	// Global file handle for logging
	FILE *logfd;

void zs_statistics();
void experiment_multi_timeout(int signum);

void Java_eu_tb_zesense_ZeJNIHub_ze_1samplingnative(JNIEnv* env, jobject thiz) {

	// Hello and current time and date
	LOGI("Hello from zs_SamplingNative");
	time_t lt;
	lt = time(NULL);

	// Set up sensor sampling infrastructure:
	// the looper monitors a set of fds and feeds events to a sensor event queue
	// we only deal with the sensor event queue, it then handles the looper by itself
	// the sensor manager knows which fd corresponds to a given sensor
    ASensorManager* sensorManager;
    ASensorEventQueue* sensorEventQueue;
    ALooper* looper;

    // Prepare looper
    looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    LOGI("looper prepared");

    // Take the SensorManager (C++ singleton class)
    sensorManager = ASensorManager_getInstance();
    LOGI("got sensorManager");

    // Create event queue associated with that looper
    sensorEventQueue = ASensorManager_createEventQueue(sensorManager, looper, 45, NULL, NULL);
    LOGI("got sensorEventQueue");

    //TODO: possibility of assigning a different looper-queue to each sensor in different threads!?
    //in this way we're assigning the same looper to many fds. It will probably poll them in a predefined order
    //instead of leaving this decision to the scheduler

    // Set priority
    LOGI("Old priority %d", getpriority(PRIO_PROCESS, 0));
    if (getpriority(PRIO_PROCESS, 0) != ZS_EXPERIMENT_PRIORITY) {
    	int setpriority_success = setpriority(PRIO_PROCESS, 0, ZS_EXPERIMENT_PRIORITY);
    	if (setpriority_success == 0) {
    		LOGI("New priority %d", getpriority(PRIO_PROCESS, 0));
    	}
    	else {
    		LOGW("Could not change priority to %d", ZS_EXPERIMENT_PRIORITY);
    		exit(1);
    	}
    } else LOGI("New priority unchanged");

    // Open log file
	char *logpath = LOGPATH;
	logfd = fopen(logpath,"ab");
	if(logfd == NULL) {
		LOGW("unable to open %s", logpath);
		exit(1);
	}
	else LOGI("success opening %s", logpath);

	// Log experiment start time
	if (fputs(ctime(&lt), logfd)<0) LOGW("write failed");

    // Wait some time before starting
    sleep(3);

#ifdef ACCEL_ON
    // Grab the sensor description
    const ASensor* accelerometerSensor;
    accelerometerSensor = ASensorManager_getDefaultSensor(sensorManager, ASENSOR_TYPE_ACCELEROMETER);

    // Start monitoring the sensor
    if (accelerometerSensor != NULL) {
    	LOGI("got accelerometer sensor");
        ASensorEventQueue_enableSensor(sensorEventQueue, accelerometerSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, accelerometerSensor, (1000L/ACCEL_HZ)*1000);
    }
    else {
    	LOGW("accelerometer sensor NULL");
    	exit(1);
    }
#endif

#ifdef GYRO_ON
    // Grab the sensor description
    const ASensor* gyroSensor;
    gyroSensor = ASensorManager_getDefaultSensor(sensorManager, ASENSOR_TYPE_GYROSCOPE);

    // Start monitoring the sensor
    if (gyroSensor != NULL) {
    	LOGI("got gyro sensor");
        ASensorEventQueue_enableSensor(sensorEventQueue, gyroSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, gyroSensor, (1000L/GYRO_HZ)*1000);
    }
    else {
    	LOGW("gyro sensor NULL");
    	exit(1);
    }
#endif

#ifdef LIGHT_ON
    // Grab the sensor description
    const ASensor* lightSensor;
    lightSensor = ASensorManager_getDefaultSensor(sensorManager, ASENSOR_TYPE_LIGHT);

    // Start monitoring the sensor
    if (lightSensor != NULL) {
    	LOGI("got light sensor");
        ASensorEventQueue_enableSensor(sensorEventQueue, lightSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, lightSensor, (1000L/LIGHT_HZ)*1000);
    }
    else {
    	LOGW("light sensor NULL");
    	exit(1);
    }
#endif

#ifdef MAG_ON
    // Grab the sensor description
    const ASensor* magSensor;
    magSensor = ASensorManager_getDefaultSensor(sensorManager, ASENSOR_TYPE_MAGNETIC_FIELD);

    // Start monitoring the sensor
    if (magSensor != NULL) {
    	LOGI("got magnetic sensor");
        ASensorEventQueue_enableSensor(sensorEventQueue, magSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, magSensor, (1000L/MAG_HZ)*1000);
    }
    else {
    	LOGW("magnetic sensor NULL");
    	exit(1);
    }
#endif

#ifdef PROX_ON //TODO: value broadcasted only when it changes!!
    // Grab the sensor description
    const ASensor* proxSensor;
    proxSensor = ASensorManager_getDefaultSensor(sensorManager, ASENSOR_TYPE_PROXIMITY);

    // Start monitoring the sensor
    if (proxSensor != NULL) {
    	LOGI("got proximity sensor");
        ASensorEventQueue_enableSensor(sensorEventQueue, proxSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, proxSensor, (1000L/PROX_HZ)*1000);
    }
    else {
    	LOGW("proximity sensor NULL");
    	exit(1);
    }
#endif

#ifdef PRES_ON
    // Grab the sensor description
    const ASensor* presSensor;
    presSensor = ASensorManager_getDefaultSensor(sensorManager, ZESENSE_SENSOR_TYPE_PRESSURE);

    // Start monitoring the sensor
    if (presSensor != NULL) {
    	LOGI("got pressure sensor");
        ASensorEventQueue_enableSensor(sensorEventQueue, presSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, presSensor, (1000L/PRES_HZ)*1000);
    }
    else {
    	LOGW("pressure sensor NULL");
    	exit(1);
    }
#endif

#ifdef ORIENT_ON
    // Grab the sensor description
    const ASensor* orientSensor;
    orientSensor = ASensorManager_getDefaultSensor(sensorManager, ZESENSE_SENSOR_TYPE_ORIENTATION);

    // Start monitoring the sensor
    if (orientSensor != NULL) {
    	LOGI("got orientation sensor (fused)");
        ASensorEventQueue_enableSensor(sensorEventQueue, orientSensor);
        ASensorEventQueue_setEventRate(sensorEventQueue, orientSensor, (1000L/ORIENT_HZ)*1000);
    }
    else {
    	LOGW("orientation sensor NULL");
    	exit(1);
    }
#endif

#ifndef ZS_EXPERIMENT_MULTIPLE

    ASensorEvent event;
    struct timeval start_time, stop_time, diff;
    int64_t duration_time;
    double average_frequency;

    event_counter = 0;

    // Experiment start time
	if(gettimeofday(&start_time, NULL)!=0) LOGW("Failed start_time gettimeofday()");

    while (event_counter < NUM_SAMPLES) {
    	if (ASensorEventQueue_getEvents(sensorEventQueue, &event, 1) > 0) {
			if (event.type == ASENSOR_TYPE_GYROSCOPE) { //TODO: redundant check
				// place event into the list
            	experiment.events_list[event_counter].event = event;
            	// get and assign the collection timestamp
            	clock_gettime(CLOCK_MONOTONIC, &experiment.events_list[event_counter].collectionTimestamp);
            	uint64_t h = (experiment.events_list[event_counter].collectionTimestamp.tv_sec*1000000000LL)
            			+experiment.events_list[event_counter].collectionTimestamp.tv_nsec;
            	//LOGI("event light: l=%f", event.light);
            	/*LOGI("accel: x=%f y=%f z=%f",
						event.acceleration.x, event.acceleration.y,
						event.acceleration.z);*/
            	//LOGI("sensor%d, type%d", event.sensor, event.type);
            	/*LOGI("orient: azi=%f pitch=%f roll=%f",
            			event.vector.azimuth, event.vector.pitch,
            			event.vector.roll);*/
            	LOGI("event gyro: x=%f y=%f z=%f",
            							event.vector.x, event.vector.y,
            							event.vector.z);
			}
        	event_counter++;
    	}
    }

    // Experiment end time
	if(gettimeofday(&stop_time, NULL)!=0) LOGW("Failed stop_time gettimeofday()");

    // Calculate duration and average frequency
	timeval_subtract(&diff, &stop_time, &start_time);

	duration_time = (diff.tv_sec*1000000LL)+diff.tv_usec; //microsecs
	average_frequency = (event_counter)/((double) duration_time/1000000LL);
	//Rappel: this is not the frequency of generation of events, which is more accurately
	//estimated by looking at the generation timestamp of the events

	LOGI("Experiment lasted %lld, avg freq %g", duration_time, average_frequency);

	// Now start the statistics
	zs_statistics();

#else // Take samples for ZS_EXPERIMENT_DURATION time. Use a timeout to stop.

	//TODO: To cope with the storage of an unknown number of samples, allocate a very big array
	// statically and exit as soon as the timer expires or the array spots run out
	event_counter = 0;

	// Register timer, handler is experiment_multi_timeout()
	signal(SIGALRM, (void (*)(int)) experiment_multi_timeout);
	alarm(ZS_EXPERIMENT_DURATION);

	// Set start experiment flag
	experimenting = 1;

    ASensorEvent event;
    while(experimenting==1 && event_counter<NUM_SAMPLES) {
		if (ASensorEventQueue_getEvents(sensorEventQueue, &event, 1) > 0) {

			LOGI("sensor event from sensor%d, type%d", event.sensor, event.type);

			if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
				LOGI("event accelerometer: x=%f y=%f z=%f",
						event.acceleration.x, event.acceleration.y,
						event.acceleration.z);
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else if (event.type == ASENSOR_TYPE_GYROSCOPE) {
				LOGI("event gyro: x=%f y=%f z=%f",
						event.vector.x, event.vector.y,
						event.vector.z);
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else if (event.type == ASENSOR_TYPE_LIGHT) {
				LOGI("event light: l=%f", event.light);
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else if (event.type == ASENSOR_TYPE_MAGNETIC_FIELD) {
				LOGI("event mag: x=%f y=%f z=%f",
						event.magnetic.x, event.magnetic.y,
						event.magnetic.z);
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else if (event.type == ASENSOR_TYPE_PROXIMITY) {
				LOGI("event prox: p=%f", event.distance);
				//ASensorEventQueue_enableSensor(sensorEventQueue, proxSensor); //shouldn't be necessary
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else if (event.type == ZESENSE_SENSOR_TYPE_PRESSURE) {
				LOGI("event pressure: p=%f", event.pressure);
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else if (event.type = ZESENSE_SENSOR_TYPE_ORIENTATION) {
            	LOGI("event orient: azi=%f pitch=%f roll=%f",
            			event.vector.azimuth, event.vector.pitch,
            			event.vector.roll);
				events_list[event_counter].event = event;
            	clock_gettime(CLOCK_MONOTONIC, &events_list[event_counter].collectionTimestamp);
			}

			else {
				LOGW("SENSOR EVENT NOT RECOGNIZED!");
				exit(1);
			}

			// Increment event counter
			event_counter++;
		}
    }

    if (event_counter==NUM_SAMPLES) {
    	LOGI("alarm not fired but ran out of array spots");
    	alarm(0); //deactivate the alarm, which has not yet fired
    	experimenting = 0;
    }

	// Now start the statistics
	zs_statistics();


#endif
    //return (*env)->NewStringUTF(env, "Hello from JNI !");

    // Close log file
	fclose(logfd);
}

#ifdef ZS_EXPERIMENT_MULTIPLE
void experiment_multi_timeout(int signum) {
	signal(SIGALRM, SIG_DFL); //back to default action for SIGALRM
	if (experimenting == 1) {
		experimenting = 0;
		LOGI("alarm fired, event_counter=%d, NUM_SAMPLES=%d", event_counter, NUM_SAMPLES);
	}
	else {
		LOGW("error, alarm experiment fired, but not experimenting");
		exit(1);
	}
}
#endif

// Copied from http://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html
int timeval_subtract (result, x, y)	struct timeval *result, *x, *y; {
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < y->tv_usec) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
	    y->tv_usec -= 1000000 * nsec;
	    y->tv_sec += nsec;
	}
	if (x->tv_usec - y->tv_usec > 1000000) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000;
	    y->tv_usec += 1000000 * nsec;
	    y->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait.
	     tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}


/*
 * Take periods of the series array and place them in periods
 * s0, s1... sn -> p0, p1... pn-1
 */
void periods(int64_t* series, int seriesLength, int64_t* periods) {

	//Debug
	int seriesSize = sizeof(series);
	int periodsSize = sizeof(periods);
	int seriesElementSize = sizeof(series[0]);
	int periodsElementSize = sizeof(periods[0]);
	LOGI("seriesSize=%d, seriesElementSize=%d, seriesLength=%d, periodsLength=%d",
			seriesSize, seriesElementSize, seriesSize/seriesElementSize, periodsSize/periodsElementSize);

	int i;
	for (i=0; i<(seriesLength-1); i++) {
		periods[i]=series[i+1]-series[i];
		if (periods[i]<0) {
			LOGW("error negative period");
			exit(1);
		}
	}
	LOGI("periods loop ended, i=%d", i);
}
/*
 * Variance (type=1) or standard deviation (type=2) calculator with two-pass algorithm
 */
double dispersion_twopass(int type, int64_t average, int64_t* array, int length) {
	int i = 0;
	int64_t incSum = 0;
	int64_t scarto, scartoq;
	LOGI("average %ld", average);
	for (i=0; i<length; i++) {
		scarto = average-array[i];
		scartoq = pow(scarto, 2);
		incSum = incSum + scartoq;
	}
	if (type==1) return (incSum/length); //return variance
	else if (type==2) return sqrt(incSum/length); //return stddev
	else return -1;
}

/*
 * Standard deviation calculator with Welford's algorithm
 */
double dispersion_welford(int64_t* array, int length) {
	int i=0;
	int64_t delta;
	int64_t mean = 0;
	int64_t m2 = 0;
	for (i=1; i<length+1; i++) {
		delta = array[i-1] - mean;
		mean = mean + (delta/i);
		m2 = m2 + (delta*(array[i-1]-mean));
	}
	return sqrt(m2/(i-1));
	//return (m2/(i-1));
}


/*
 * Statistics routine computes:
 * - Average, standard deviation of periods among sensor samples from generation timestamp
 * - Average, standard deviation of periods among sensor samples from collection timestamp
 * - Average, standard deviation of delay between generation and collection
 * - Better to do in Excel: drift over time (like, 10 mins) of the average sampling rate computed
 * for example in such a way: take 10 mins of samples, take the average sampling rate
 * (cannot assume it as even if you specify 10Hz the system might give you more or less,
 * take the first sample time and project where the others should have been according to
 * that rate average, finally get the difference between the projected and the actual samples.
 * (wait but isn't the average affected by this drift itself? maybe take the average on the first half
 * of the samples and see the behavior of the other half? is there already a statistical definition for
 * that?)
 * - even more interesting this drift for the propagation delay of the event from HW to UserSpace
 * Do this for all the sensors that we have, in isolation and together, for different requested
 * sampling frequencies.
 * TO CALCULATE THIS DRIFT WE CAN SIMPLY TAKE THE MOVING AVERAGE, NO? Nope, not at all, moving avg
 * on the difference will only give you the relative amount, it will tell you the drift in the
 * rate, not on the absolute time of the sample!
 * - in-order delivery
 *
 * Note: the actual experiment duration might be different then what specified in EXPERIMENT_DURATION
 * since we stop when we reach HZ*EXPERIMENT_DURATION samples taken, and Hz might be different
 * than what specified
 * in particular, as it seems from first experiments, the accelerometer sampling rate is capped at 100Hz
 * if I define Hz as 200, the actual experiment duration will be roughly double
 *
 */

//TODO: moving average!

void zs_statistics() {
#ifndef ZS_EXPERIMENT_MULTIPLE
	int j = 0; //iterator for the arrays
	char logstr[100];


	/*
	 * EVENT GENERATION FREQUENCY (input subsystem timestamps)
	 * engine.accelEvents[].event.timestamp of type int64_t
	 * static void periods(int64_t* series, int seriesLength, int64_t* periods)
	 * sum the periods together
	 * divide by (NUM_SAMPLES)-1
	 */
	LOGI("performing now stats on generation timestamp..");
	int64_t genAccelSeries[NUM_SAMPLES];
	int64_t genAccelPeriods[NUM_SAMPLES-1];
	int64_t genAccelIncSum = 0;
	double genAccelAvgPeriods = 0;
	double genAccelStddev = 0;

	for (j=0; j<NUM_SAMPLES; j++) {
		genAccelSeries[j]=experiment.events_list[j].event.timestamp;

		sprintf(logstr, "%llu\n", genAccelSeries[j]);
		if (fputs(logstr, logfd)<0) LOGW("write file failed");
	}
	periods(genAccelSeries, NUM_SAMPLES, genAccelPeriods);
	for (j=0; j<NUM_SAMPLES-1; j++) {
		//sprintf(logstr, "%d\n",  genAccelPeriods[j]);
		//if (fputs(logstr, logfd)<0) LOGW("write file failed");
		genAccelIncSum = genAccelIncSum + genAccelPeriods[j];
	}
	// get average and stddev
	genAccelAvgPeriods = (double)genAccelIncSum/(NUM_SAMPLES-1);

	//int k;
	//for (k=0; k<(NUM_SAMPLES-1); k++) LOGI("%llu", genAccelPeriods[k]);

	//genAccelStddev = dispersion_twopass(2, genAccelAvgPeriods, genAccelPeriods, (NUM_SAMPLES-1));
	genAccelStddev = dispersion_welford(genAccelPeriods, (NUM_SAMPLES-1));
	sprintf(logstr, "%e %e ",  genAccelAvgPeriods/1000000, genAccelStddev/1000000);
	if (fputs(logstr, logfd)<0) LOGW("write file failed");
	LOGI("Generation Average = %e, Stddev = %e",  genAccelAvgPeriods/1000000, genAccelStddev/1000000);


	/*
	 * EVENT CONSUMPTION FREQUENCY (timestamp we assign when we get the sample)
	 */
	int64_t collAccelSeries[NUM_SAMPLES];
	int64_t collAccelPeriods[NUM_SAMPLES-1];
	int64_t collAccelIncSum = 0;
	double collAccelAvgPeriods = 0;
	double collAccelStddev = 0;
	uint64_t h;
	for (j=0; j<NUM_SAMPLES; j++) {
    	h = (experiment.events_list[j].collectionTimestamp.tv_sec*1000000000LL)
    			+experiment.events_list[j].collectionTimestamp.tv_nsec;
    	collAccelSeries[j]=h;
	}
	periods(collAccelSeries, NUM_SAMPLES, collAccelPeriods);
	for (j=0; j<NUM_SAMPLES-1; j++) {
		collAccelIncSum = collAccelIncSum + collAccelPeriods[j];
	}
	collAccelAvgPeriods = (double)collAccelIncSum/(NUM_SAMPLES-1);
	//collAccelStddev = dispersion_twopass(2, collAccelAvgPeriods, collAccelPeriods, (NUM_SAMPLES-1));
	collAccelStddev = dispersion_welford(collAccelPeriods, (NUM_SAMPLES-1));
	sprintf(logstr, "%e %e ",  collAccelAvgPeriods/1000000, collAccelStddev/1000000);
	if (fputs(logstr, logfd)<0) LOGW("write file failed");
	LOGI("Consumption Average = %e, Stddev = %e",  collAccelAvgPeriods/1000000, collAccelStddev/1000000);

	/*
	 * EVENT PROPAGATION DELAY
	 */
	//int64_t collAccelSeries[NUM_SAMPLES];
	int64_t travelAccelSeries[NUM_SAMPLES];
	int64_t travelAccelIncSum = 0;
	double travelAccelAvg = 0;
	double travelAccelStddev = 0;

	for (j=0; j<NUM_SAMPLES; j++) {
		if (collAccelSeries[j]<genAccelSeries[j]) {
			LOGW("Error: collection less than generation timestamp");
			exit(1);
		}
    	travelAccelSeries[j] = collAccelSeries[j] - genAccelSeries[j];
    	//LOGI("Gen: %llu, Coll %llu, Travel %llu", genAccelSeries[j], collAccelSeries[j], travelAccelSeries[j]);
	}
	for (j=0; j<NUM_SAMPLES; j++) {
    	travelAccelIncSum = travelAccelIncSum + travelAccelSeries[j];
	}

	travelAccelAvg = (double)travelAccelIncSum/(NUM_SAMPLES);
	//travelAccelStddev = dispersion_twopass(2, travelAccelAvg, travelAccelSeries, (NUM_SAMPLES));
	travelAccelStddev = dispersion_welford(travelAccelSeries, NUM_SAMPLES);
	sprintf(logstr, "%e %e\n", travelAccelAvg, travelAccelStddev);
	if (fputs(logstr, logfd)<0) LOGW("write file failed");
	LOGI("Travel Average = %e, Stddev %e", travelAccelAvg, travelAccelStddev);

#else

	int accel_count = 0;
	double accelAvg, accelStddev;
	int64_t genAccelPeriod, genAccelIncSum = 0;
	struct zs_ASensorEvent accel_prev_event;

	int gyro_count = 0;
	double gyroAvg, gyroStddev;
	int64_t genGyroPeriod, genGyroIncSum = 0;
	struct zs_ASensorEvent gyro_prev_event;

	int orient_count = 0;
	double orientAvg, orientStddev;
	int64_t genOrientPeriod, genOrientIncSum = 0;
	struct zs_ASensorEvent orient_prev_event;

	// Event by event, compound the periods and correct the standard deviation
	int j=0;
	for (j=0; j<event_counter; j++) {

		if (events_list[j].event.type == ASENSOR_TYPE_ACCELEROMETER) {
			if (accel_count != 0) {
				genAccelPeriod = events_list[j].event.timestamp - accel_prev_event.event.timestamp;
				genAccelIncSum = genAccelIncSum + genAccelPeriod;
			}
			accel_prev_event.event = events_list[j].event;
			accel_count++;
		}

		if (events_list[j].event.type == ASENSOR_TYPE_GYROSCOPE) {
			if (gyro_count != 0) {
				genGyroPeriod = events_list[j].event.timestamp - gyro_prev_event.event.timestamp;
				genGyroIncSum = genGyroIncSum + genGyroPeriod;
			}
			gyro_prev_event.event = events_list[j].event;
			gyro_count++;
		}

		if (events_list[j].event.type == ZESENSE_SENSOR_TYPE_ORIENTATION) {
			if (orient_count != 0) {
				genOrientPeriod = events_list[j].event.timestamp - orient_prev_event.event.timestamp;
				genOrientIncSum = genOrientIncSum + genOrientPeriod;
			}
			orient_prev_event.event = events_list[j].event;
			orient_count++;
		}

	}

	// Ensure we got and recognized all events
	if (accel_count+gyro_count+orient_count!=event_counter) {
		LOGW("Sum of single events does not add up to total events");
		exit(1);
	}

	accelAvg = (double)genAccelIncSum/accel_count;
	LOGI("Accel average %e", accelAvg);

	gyroAvg = (double)genGyroIncSum/gyro_count;
	LOGI("Gyro average %e", gyroAvg);

	orientAvg = (double)genOrientIncSum/orient_count;
	LOGI("Orient average %e", orientAvg);

#endif
}

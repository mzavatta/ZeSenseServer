#include "ze_streaming_manager.h"
#include "ze_coap_server_root.h"
#include "ze_carriers_queue.h"

/*
 * Alternative implementation of this thread,
 * rather than killing it at restarting it at
 * every stream stop or start, we could use a condition
 * variable on prox_carrier_stop;
 */

ASensorEvent get_last_event_SYN(ze_sensor_t *sensor) {
	LOGW("Getting last event..");
	pthread_mutex_lock(&(sensor->carrthrmtx));
		ASensorEvent ev = sensor->event_cache;
	pthread_mutex_unlock(&(sensor->carrthrmtx));
	LOGW("Mutex released");
	return ev;
}


double get_stream_freq_SYN(ze_sensor_t *sensor) {
	pthread_mutex_lock(&(sensor->carrthrmtx));
		double freq = sensor->freq;
	pthread_mutex_unlock(&(sensor->carrthrmtx));
	return freq;
}

void *
ze_carrier_thread(void* args) {

	struct generic_carr_thread_args* ar = ((struct generic_carr_thread_args*)(args));
	ze_carriers_queue_t *carrq = ar->carrq;
	ze_sensor_t *sensor = ar->sensor;

	LOGI("Hello from a carrier thread pid%d, tid%d!", getpid(), gettid());
	pthread_setname_np(pthread_self(), "aCarrierThread");

	/* !! Access to is_active, likewise globalexit, shall be synched.
	 * Neglect for the moment, save a little time, operations on natural-size
	 * integers should be atomic.
	 */
	while (!globalexit && sensor->is_active) { //either a global exit or a stream stop

		ASensorEvent etp;
		etp = get_last_event_SYN(sensor);

		put_carrier_event(carrq, etp);

		/* Evaluate, based on the highest frequency requested
		 * for the carrier stream on that sensor, how long should
		 * be the cycle period.
		 */
		double f = 1/get_stream_freq_SYN(sensor);
		int sec = (int)f; //isolate integer part
		double decpart = f - sec; //isolate decimal part
		long nsec = decpart * 1000000000LL; //enlarge to 10^9 (need nsec) and cut the rest
		LOGW("Activating carrier on sensor:%d period sec:%d, nsec:%d", sensor->sensor, sec, nsec);
		struct timespec sleep_time;
		sleep_time.tv_sec = sec;
		sleep_time.tv_nsec = nsec;
		nanosleep(&sleep_time, NULL);
	}

	LOGW("Carrier thread out of main loop");

	/* Again here we'd need synch.. */
	sensor->carrier_thread_started = 0;
}

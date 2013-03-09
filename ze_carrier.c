#include "ze_streaming_manager.h"
#include "ze_coap_server_root.h"
#include "ze_carriers_queue.h"
#include "ze_timing.h"

/*
 * Alternative implementation of this thread,
 * rather than killing it at restarting it at
 * every stream stop or start, we could use a condition
 * variable that waits on is_active
 */

void *
ze_carrier_thread(void* args) {

	struct generic_carr_thread_args* ar = ((struct generic_carr_thread_args*)(args));
	ze_carriers_queue_t *carrq = ar->carrq;
	ze_sensor_t *sensor = ar->sensor;

	LOGI("Hello from sensor%d carrier thread pid%d, tid%d!", sensor->sensor, getpid(), gettid());
	pthread_setname_np(pthread_self(), "aCarrierThread");

	/* !! Access to is_active, likewise globalexit, shall be synched.
	 * Neglect for the moment, save a little time, operations on natural-size
	 * integers should be atomic. */
	while (!globalexit && sensor->is_active) { //either a global exit or a stream stop

		// Again, it shall be synched
		if (sensor->cache_valid==1) {
			ASensorEvent etp;
			etp = read_last_event_SYN(sensor);
			etp.timestamp = get_ntp();
			put_carrier_event(carrq, etp);
		}
		/* Evaluate, based on the highest frequency requested
		 * for the carrier stream on that sensor, how long should
		 * be the cycle period.
		 */
		int fr = sensor->freq;
		double p = (double)1/fr; //read_stream_freq_SYN(sensor);
		int sec = (int)p; //isolate integer part
		double decpart = p - sec; //isolate decimal part
		long nsec = decpart * 1000000000LL; //enlarge to 10^9 (need nsec) and cut the rest
		LOGW("Activating carrier on sensor:%d period p:%f decpart:%f sec:%d, nsec:%ld",
				sensor->sensor, p, decpart, sec, nsec);
		//exit(1);
		struct timespec sleep_time;
		sleep_time.tv_sec = sec;
		sleep_time.tv_nsec = nsec;
		nanosleep(&sleep_time, NULL);
	}

	LOGW("Carrier thread out of main loop");

	/* Again here we'd need synch.. */
	sensor->carrier_thread_started = 0;
}

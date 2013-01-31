/*
 * ZeSense Streaming Manager
 * -- last known sample cache
 * (deprecated)
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

typedef struct ze_sample_cache_t {

	/* Array of the latest sensor events. */
	ze_cache_element_t events[ZE_NUMSENSORS];

	/* One mutex for each array slot, avoid locking it all. */
	pthread_mutex_t mtx[ZE_NUMSENSORS];
};

typedef struct ze_cache_element_t {
	ASensorEvent event;
	int empty;
};

/**
 * Gets the sample corresponding to @p sensor from the given
 * @p cache, returning a pointer @p sample to newly allocated
 * memory space, which is obviously not freed.
 * Blocks if the resource is owned by another thread.
 *
 * @param cache		The sensor cache
 * @param sensor	The sensor identifier
 * @param sample	The sample returned, newly allocated
 * 					on the heap
 *
 * @return Zero if successful, SM_EMPTY if no sample for that sensor
 * is available, SM_ERROR on error.
 */
int get_sample_cache(ze_sample_cache_t *cache, int sensor, ASensorEvent *sample);

/**
 * Replaces the old value of @p sensor with the updated one @p sample
 * in the given @p cache. Blocks if the resource is owned by another
 * thread.
 *
 * @param cache		The sensor cache
 * @param sensor	The sensor identifier
 * @param sample	The sample to be cached
 *
 * @return Zero if successful, @c SM_EMPTY if no sample for that sensor
 * is available, @c SM_ERROR on error.
 */
int update_sample_cache(ze_sample_cache_t *cache, int sensor, ASensorEvent sample);


/**
 * Initializes the cache. Elements of the event array will be NULLed
 * and mutex . Note that this function must be called -only once- for the
 * whole program execution because of the mutex initialization,
 * unless you call !TODO! reset_cache() in between.
 *
 * @param cache		The sensor cache
 *
 * @return Zero if successful, @c SM_ERROR on error.
 */
int init_cache(ze_sample_cache_t *cache);

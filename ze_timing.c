#include "ze_timing.h"
#include <time.h>

int64_t get_ntp() {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	int64_t ntp = (t.tv_sec*1000000000LL)+t.tv_nsec;
	return ntp;
}

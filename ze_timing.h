/*
 * ZeSense Streaming Manager
 * -- timing parameters
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#ifndef ZE_TIMING_H
#define ZE_TIMING_H

#include <stdlib.h>

#define RTP_TS_START					450	//debug value
#define RTP_TSCLOCK_FREQ 				1000
#define RTCP_SR_BANDWIDTH_THRESHOLD		1000
#define NTP_TS_FREQ						100000000LL

int64_t get_ntp();

#endif

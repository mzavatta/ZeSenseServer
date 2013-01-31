/*
 * ZeSense
 * -- logging utilities
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#ifndef ZE_LOG_H
#define ZE_LOG_H

#include <android/log.h>

#define ZELOGPATH "/sdcard/ze_coap_server.txt"

/* Variadic macros, new in C99 standard. */
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ze_coap_server", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "ze_coap_server", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "ze_coap_server", __VA_ARGS__))

#define ZELOGI(...) LOGI(__VA_ARGS__)
#define ZELOGW(...) LOGW(__VA_ARGS__)
#define ZELOGE(...) LOGE(__VA_ARGS__)

#endif

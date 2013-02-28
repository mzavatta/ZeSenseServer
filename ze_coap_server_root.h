/*
 * ZeSense CoAP Streaming Server
 * -- root module
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 *
 * Built using libcoap by
 * Olaf Bergmann <bergmann@tzi.org>
 * http://libcoap.sourceforge.net/
 */

#ifndef ZE_COAP_SERVER_ROOT_H
#define ZE_COAP_SERVER_ROOT_H

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>

#include "config.h"
#include "net.h"
#include "ze_coap_reqbuf.h"
#include "ze_sm_reqbuf.h"
#include "ze_streaming_manager.h"
//#include "coap.h"
#include <jni.h>

/* Server endpoint coordinates. */
#define SERVER_IP "192.168.43.1"	//Wifi hotspot interface
//#define SERVER_IP "10.0.2.15" 	//Android emulator interface
#define SERVER_PORT "5683"

/* Structures to wrap parameters for threads.*/
struct sm_thread_args {
	stream_context_t *smctx;
	ze_sm_request_buf_t *smreqbuf;
	ze_coap_request_buf_t *notbuf;
	jobject actx;
	JavaVM *jvm;
	jclass ZeGPSManager;
};
struct coap_thread_args {
	coap_context_t  *cctx;
	ze_sm_request_buf_t *smreqbuf;
	ze_coap_request_buf_t *notbuf;
};

/* Global quit flag.
 * No use of synch primitives as an integer
 * read or write operation should be atomic.
 */
int globalexit;

/* Only for testing purposes. */
int go;

/* Globals holding the buffer
 * Only for testing the JNI of RTP and
 * our Streaming Manager interaction through JNI
 */
ze_sm_request_buf_t *smreqbufg;
ze_coap_request_buf_t *notbufg;

//jint
//Java_eu_tb_zesense_ZeJNIHub_ze_1coap_1server_1root(JNIEnv* env, jobject thiz);

int
ze_coap_server_root(JNIEnv* env, jobject thiz, jobject actx);

coap_context_t *
get_context(const char *node, const char *port);

#endif

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

#include "ze_coap_server_root.h"
#include "resource.h"
#include "ze_coap_resources.h"
#include "ze_coap_server_core.h"
#include "ze_streaming_manager.h"
#include <jni.h>
#include "ze_log.h"

/* Log file handle. */
FILE *logfd;

//jint
//Java_eu_tb_zesense_ZeJNIHub_ze_1coap_1server_1root(JNIEnv* env, jobject thiz) {
int
ze_coap_server_root(JNIEnv* env, jobject thiz, jobject actx) {

	LOGI("ZeSense new CoAP server hello, pid%d, tid%d", getpid(), gettid());
	pthread_setname_np(pthread_self(), "ZeRoot");


	//pthread_exit(NULL);

	//ALooper* looper = ALooper_prepare(0);
/*
	jclass ZeGPSManager = (*env)->FindClass(env, "eu/tb/zesense/ZeGPSManager");
	if (ZeGPSManager==NULL) LOGW("class not found");
	jmethodID ZeGPSManager_constructor =
			(*env)->GetMethodID(env, ZeGPSManager, "<init>", "()V");
	if (ZeGPSManager_constructor==NULL) LOGW("constructor not found");
	jmethodID ZeGPSManager_start = (*env)->GetMethodID(env, ZeGPSManager, "startStream", "()I");
	if (ZeGPSManager_start==NULL) LOGW("startStream not found");
	jmethodID ZeGPSManager_init = (*env)->GetMethodID(env, ZeGPSManager, "init", "(Landroid/content/Context;)V");
	if (ZeGPSManager_init==NULL) LOGW("init not found");
	jmethodID ZeGPSManager_stop = (*env)->GetMethodID(env, ZeGPSManager, "stopStream", "()I");
	if (ZeGPSManager_stop==NULL) LOGW("stopStream not found");

	jobject gpsManager = (*env)->NewObject(env, ZeGPSManager, ZeGPSManager_constructor);

	(*env)->CallVoidMethod(env, gpsManager, ZeGPSManager_init, actx);
	jint sta = (*env)->CallIntMethod(env, gpsManager, ZeGPSManager_start);
	LOGW("from GPSManager %d", sta);

	sleep(10);

	jint stp = (*env)->CallIntMethod(env, gpsManager, ZeGPSManager_stop);
	LOGW("from GPSManager %d", stp);
*/
	//exit(1);
	return 1;


	/* Spawn *all* the threads from here, this thread has no other function
	 * except for fixing in memory, once and forever, the handles (what we
	 * call contexts) of the app components and passing their references
	 * to the threads.
	 *
	 * This thread is in some way the lifecycle manager of the system.
	 */

	/* App components definitions; NEVER to reallocate, move or free because
	 * they are shared among threads and their pointers are used from multiple
	 * subjects!
	 *
	 * Components that make up the app:
	 * - Android system: actx (enables to talk
	 * to OS services e.g. the singleton LocationManager)
	 * - CoAP Server: cctx
	 * - Streaming Manager: smctx
	 * - Streaming Manager request buffer: smreqbuf
	 * - CoAP server commands buffer: notbuf
	 * - JavaVM: jvm
	 *
	 * Remark on the JavaVM: it can be located only from this thread
	 * using the JNIEnv. JNIEnv is not valid across threads, but the
	 * JavaVM is and is able to produce a JNIEnv for a thread only
	 * from within the thread.
	 * */
	coap_context_t  *cctx = NULL;
	cctx = get_context(SERVER_IP, SERVER_PORT);
	if (cctx == NULL)
		return -1;
	LOGI("Root, got cctx");

	stream_context_t *smctx = NULL;
	smctx = get_streaming_manager(/*may want to parametrize*/);
	if (smctx == NULL)
		return -1;
	LOGI("Root, got smctx");

	ze_sm_request_buf_t *smreqbuf = NULL;
	smreqbuf = init_sm_buf();
	if (smreqbuf == NULL)
		return -1;
	LOGI("Root, got smreqbuf");

	ze_coap_request_buf_t *notbuf = NULL;
	notbuf = init_coap_buf();
	if (notbuf == NULL)
		return -1;
	LOGI("Root, got notbuf");

	JavaVM *jvm;
	int jvmres = GetJavaVM(env, &jvm);
	if (jvmres != 0) LOGW("Cannot get JavaVM");

    /* Open log file. */
	char *logpath = ZELOGPATH;
	logfd = fopen(logpath,"ab");
	if(logfd == NULL) {
		LOGW("unable to open %s", logpath);
		return -1;
	}
	else LOGI("success opening %s", logpath);

	/* Initialize the resource tree. */
	ze_coap_init_resources(cctx);
	LOGI("Root, resources initialized");

	/* Fire threads, at last.. */
	/*
	 * In a two-thread scenario:
	 * - CoAP server needs *cctx, *smreqbu, *notbuf
	 * - Streaming Manager needs *smctx, *smreqbuf, *notbuf, actx, jvm
	 *
	 * The objective of this umbrella is to gather the app components
	 * to pass them to the threads. They will fill up their own details
	 * contexts when created.
	 */
	globalexit = 0;

	int smerr, coaperr;

	pthread_t streaming_manager_thread;
	struct sm_thread_args smargs;
	smargs.smctx = smctx;
	smargs.smreqbuf = smreqbuf;
	smargs.notbuf = notbuf;
	smargs.actx = actx;
	smargs.jvm = jvm;

	pthread_t coap_server_thread;
	struct coap_thread_args coapargs;
	coapargs.cctx = cctx;
	coapargs.smreqbuf = smreqbuf;
	coapargs.notbuf = notbuf;

	smerr = pthread_create(&streaming_manager_thread, NULL,
			ze_coap_streaming_thread, &smargs);
	if (smerr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(smerr));
		exit(1);
	}
	LOGI("Root, launched streaming manager thread.");
	pthread_setname_np(streaming_manager_thread, "StreamingMngr");

	coaperr = pthread_create(&coap_server_thread, NULL,
			ze_coap_server_core_thread, &coapargs);
	if (coaperr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(coaperr));
		exit(1);
	}
	LOGI("Root, launched CoAP server thread.");
	pthread_setname_np(coap_server_thread, "CoAPServer");


	/* Logging. */
	time_t lt;
	lt = time(NULL);
	if (fputs("Threads launched correctly on \n", logfd)<0) LOGW("write failed");
	if (fputs(ctime(&lt), logfd)<0) LOGW("write failed");

	/* Wait for an exit request from the Java world and spread it
	 * to other threads. */
	jclass servclass = (*env)->FindClass(env, "java/lang/Thread");
	jmethodID inted = (*env)->GetMethodID(env, servclass, "isInterrupted", "()Z");
	while (!globalexit) {
		sleep(1);
		jboolean status = (*env)->CallBooleanMethod(env, thiz, inted);
		if (status == JNI_TRUE) globalexit = 1;
	}
	LOGI("Root, global exit requested, waiting global join.");

	/* Rejoin our children before closing, otherwise the variables we've
	 * created will go out of scope.. */
	int *exitcode;
	pthread_join(streaming_manager_thread, &exitcode);
	pthread_join(coap_server_thread, &exitcode);

	/* Free the four app components. */
	free(cctx);
	free(smctx);
	free(smreqbuf);
	free(notbuf);

	return 1;
}

/* Interprets IP/port on which opens and binds a socket. */
coap_context_t *
get_context(const char *node, const char *port) {
  coap_context_t *ctx = NULL;
  int s;
  struct addrinfo hints;
  struct addrinfo *result, *rp;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Coap uses UDP */
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

  s = getaddrinfo(node, port, &hints, &result);
  if ( s != 0 ) {
    LOGW("getaddrinfo: %s\n", gai_strerror(s));
    return NULL;
  }

  /* iterate through results until success */
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    coap_address_t addr;

    if (rp->ai_addrlen <= sizeof(addr.addr)) {
      coap_address_init(&addr);
      addr.size = rp->ai_addrlen;
      memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);

      ctx = coap_new_context(&addr);
      if (ctx) {
	/* TODO: output address:port for successful binding */
	goto finish;
      }
    }
  }

  LOGW("no context available for interface '%s'\n", node);

 finish:
  freeaddrinfo(result);
  return ctx;
}

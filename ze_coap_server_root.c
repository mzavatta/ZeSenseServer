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

#include <jni.h>
#include "ze_coap_server_root.h"
#include "ze_streaming_manager.h"
#include "ze_log.h"




/* Log file handle. */
FILE *logfd;


#ifdef COAP_SERVER
#include "resource.h"
#include "address.h"
#include "ze_coap_server_core.h"
#include "ze_coap_resources.h"
#else
#include "rtp_net.h"
#include "addr.h"
#include "ze_rtp_server_core.h"
#endif

//jint
//Java_eu_tb_zesense_ZeJNIHub_ze_1coap_1server_1root(JNIEnv* env, jobject thiz) {

#ifdef COAP_SERVER
coap_context_t *
get_coap_context(const char *node, const char *port);
#else
rtp_context_t *
get_rtp_context(const char *node, const char *port);
#endif


int
open_test_socket(coap_context_t *c, const char *node, const char *port);


int
ze_server_root(JNIEnv* env, jobject thiz, jobject actx) {

#ifdef COAP_SERVER
	LOGI("ZeSense new CoAP server hello, pid%d, tid%d", getpid(), gettid());
#else
	LOGI("ZeSense new RTP server hello, pid%d, tid%d", getpid(), gettid());
#endif
	pthread_setname_np(pthread_self(), "ZeRoot");

	//pthread_exit(NULL);
	//exit(1);
	//return 1;

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

#ifdef COAP_SERVER
	coap_context_t  *cctx = NULL;
	cctx = get_coap_context(SERVER_IP, SERVER_PORT);
	if (cctx == NULL)
		return -1;
	LOGI("Root, got cctx");
#else
	rtp_context_t  *rctx = NULL;
	rctx = get_rtp_context(SERVER_IP, SERVER_PORT);
	if (rctx == NULL)
		return -1;
	LOGI("Root, got cctx");
#endif


	LOGI("Opening test socket");
	/* Open test socket */
	open_test_socket(cctx, SERVER_IP, SERVER_PORT_TEST);

	stream_context_t *smctx = NULL;
	smctx = get_streaming_manager(/*may want to parametrize*/);
	if (smctx == NULL)
		return -1;
	LOGI("Root, got smctx");

	ze_sm_request_buf_t *smreqbuf = NULL;
	smreqbuf = init_sm_buf();
	if (smreqbuf == NULL)
		return -1;
	smreqbufg = smreqbuf;
	LOGI("Root, got smreqbuf");

	ze_sm_response_buf_t *notbuf = NULL;
	notbuf = init_coap_buf();
	if (notbuf == NULL)
		return -1;
	notbufg = notbuf;
	LOGI("Root, got notbuf");

	JavaVM *jvm;
	int jvmres = (*env)->GetJavaVM(env, &jvm);
	if (jvmres != 0) LOGW("Cannot get JavaVM");

    /* From Android docs:
     * You can get into trouble if you create a thread yourself
     * (perhaps by calling pthread_create and then attaching it
     * with AttachCurrentThread).
     * If you call FindClass from this thread, the JavaVM will
     * start in the "system" class loader instead of the one
     * associated with your application, so attempts to find
     * app-specific classes will fail.
     *
     * I could pass the ZeGPSManager class to this pthread from
     * the root thread which indeed uses the app class loader.
     * Remember the local and global reference mechanisms from
     * Oracle's JNI reference manual:
     * "Local references are only valid in the thread in which
     * they are created. The native code must not pass local
     * references from one thread to another."
     * Also, in jni.h there is typedef jobject jclass;
     * So if I pass the class object to the pthread from root
     * I probably should create a global reference for it
     * and explicitly free it before exiting the root. */
    jclass ZeGPSManagerL =
    		(*env)->FindClass(env, "eu/tb/zesense/ZeGPSManager");
    if ( !ZeGPSManagerL )
    	LOGW("Root, ZeGPSManager class not found");
    jclass ZeGPSManager = (*env)->NewGlobalRef(env, ZeGPSManagerL);
    if ( !ZeGPSManager )
    	LOGW("Root, cannot create global ref for ZeGPSManager");
    jobject actxg = (*env)->NewGlobalRef(env, actx);
    if ( !actxg )
    	LOGW("Root, cannot create global ref for actx");

    /* Open log file. */
	char *logpath = ZELOGPATH;
	logfd = fopen(logpath,"ab");
	if(logfd == NULL) {
		LOGW("unable to open %s", logpath);
		return -1;
	}
	else LOGI("success opening %s", logpath);

#ifdef COAP_SERVER
	/* Initialize the resource tree. */
	ze_coap_init_resources(cctx);
	LOGI("Root, resources initialized");
#endif

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

	int smerr;
#ifdef COAP_SERVER
	int coaperr;
#else
	int rtperr;
#endif

	pthread_t streaming_manager_thread;
	struct sm_thread_args smargs;
	smargs.smctx = smctx;
	smargs.smreqbuf = smreqbuf;
	smargs.notbuf = notbuf;
	smargs.actx = actxg;
	smargs.jvm = jvm;
	smargs.ZeGPSManager = ZeGPSManager;

#ifdef COAP_SERVER
	pthread_t coap_server_thread;
	struct coap_thread_args coapargs;
	coapargs.cctx = cctx;
	coapargs.smreqbuf = smreqbuf;
	coapargs.notbuf = notbuf;
#else
	pthread_t rtp_server_thread;
	struct rtp_thread_args rtpargs;
	rtpargs.rctx = rctx;
	rtpargs.smreqbuf = smreqbuf;
	rtpargs.notbuf = notbuf;
#endif

	smerr = pthread_create(&streaming_manager_thread, NULL,
			ze_coap_streaming_thread, &smargs);
	if (smerr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(smerr));
		exit(1);
	}
	LOGI("Root, launched streaming manager thread.");
	pthread_setname_np(streaming_manager_thread, "StreamingMngr");

#ifdef COAP_SERVER
	coaperr = pthread_create(&coap_server_thread, NULL,
			ze_coap_server_core_thread, &coapargs);
	if (coaperr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(coaperr));
		exit(1);
	}
	LOGI("Root, launched CoAP server thread.");
	pthread_setname_np(coap_server_thread, "CoAPServer");
#else
	rtperr = pthread_create(&rtp_server_thread, NULL,
			ze_rtp_server_core_thread, &rtpargs);
	if (rtperr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(rtperr));
		exit(1);
	}
	LOGI("Root, launched RTP server thread.");
	pthread_setname_np(rtp_server_thread, "RTPServer");
#endif


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
#ifdef COAP_SERVER
	pthread_join(coap_server_thread, &exitcode);
#else
	pthread_join(rtp_server_thread, &exitcode);
#endif

	/* Free global ZeGPSManager reference.
	 * We do it here as it's us that created it! */
	(*env)->DeleteGlobalRef(env, ZeGPSManager);
	(*env)->DeleteGlobalRef(env, actxg);

	/* Free the four app components that we allocated. */
#ifdef COAP_SERVER
	free(cctx);
#else
	free(rctx);
#endif
	free(smctx);
	free(smreqbuf);
	free(notbuf);

	return 1;
}

#ifdef COAP_SERVER
/* Interprets IP/port on which opens and binds a socket. */
coap_context_t *
get_coap_context(const char *node, const char *port) {
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

#else
/* Interprets IP/port on which opens and binds a socket. */
rtp_context_t *
get_rtp_context(const char *node, const char *port) {
  rtp_context_t *ctx = NULL;
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
    rtp_address_t addr;

    if (rp->ai_addrlen <= sizeof(addr.addr)) {
      rtp_address_init(&addr);
      addr.size = rp->ai_addrlen;
      memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);

      ctx = rtp_new_context(&addr);
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
#endif


int
open_test_socket(coap_context_t *c, const char *node, const char *port) {

		int s;
	  struct addrinfo hints;
	  struct addrinfo *result, *rp;

	  memset(&hints, 0, sizeof(struct addrinfo));
	  hints.ai_family = AF_UNSPEC;
	  hints.ai_socktype = SOCK_DGRAM;
	  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

	  s = getaddrinfo(node, port, &hints, &result);
	  if ( s != 0 ) {
	    LOGW("getaddrinfo: %s\n", gai_strerror(s));
	    return NULL;
	  }

	  coap_address_t addr;
	  /* iterate through results until success */
	  for (rp = result; rp != NULL; rp = rp->ai_next) {
	    if (rp->ai_addrlen <= sizeof(addr.addr)) {
	      coap_address_init(&addr);
	      addr.size = rp->ai_addrlen;
	      memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);
	      break;
	    }
	  }


	  freeaddrinfo(result);

	  coap_address_t *listen_addr = &addr;

  int reuse = 1;

  if (!listen_addr) {
    LOGW("listen address empty");
  }

  c->sockfdtest = socket(listen_addr->addr.sa.sa_family, SOCK_DGRAM, 0);
  if ( c->sockfdtest < 0 ) {
    LOGW("cannot create test socket");
    goto onerror;
  }

  if ( setsockopt( c->sockfdtest, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse) ) < 0 ) {
	    LOGW("testsocket problem with socket options");
  }

  if (bind(c->sockfdtest, &listen_addr->addr.sa, listen_addr->size) < 0) {
    LOGW("test socket problem bind");
    goto onerror;
  }


  return 1;

 onerror:
  if ( c->sockfdtest >= 0 )
    close ( c->sockfdtest );

}


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


struct sm_thread_args {
	stream_context_t *smctx;
	ze_sm_request_buf_t *smreqbuf;
	ze_coap_request_buf_t *notbuf;
};

struct coap_thread_args {
	coap_context_t  *cctx;
	ze_sm_request_buf_t *smreqbuf;
	ze_coap_request_buf_t *notbuf;
};


int Java_eu_tb_zesense_ZeJNIHub_ze_1coap_1server_1root() {

	LOGI("ZeSense new CoAP server hello!");

	/* Spawn *all* the threads from here, this thread has no other function
	 * except for instantiating the CoAP, SM contexts and the two buffers
	 * and passing their references to the threads.
	 *
	 * This thread is in some way the lifecycle manager of the system.
	 */

	/* Contexts and buffers; NEVER to reallocate, move or free because
	 * they are shared among threads! unless we use
	 * reference counting */
	coap_context_t  *cctx;
	cctx = get_context(SERVER_IP, SERVER_PORT);
	if (!cctx)
		return -1;

	stream_context_t *smctx;
	smctx = get_streaming_manager(/*may want to parametrize*/);
	if (!smctx)
		return -1;

	ze_sm_request_buf_t *smreqbuf;
	init_sm_req_buf(smreqbuf);
	if (!smreqbuf)
		return -1;

	ze_coap_request_buf_t *notbuf;
	init_coap_req_buf(notbuf);
	if (!notbuf)
		return -1;

	cctx->notbuf = notbuf;
	cctx->smreqbuf = smreqbuf;

    /* Open log file. */
	char *logpath = ZELOGPATH;
	logfd = fopen(logpath,"ab");
	if(logfd == NULL) {
		LOGW("unable to open %s", logpath);
		exit(1);
	}
	else LOGI("success opening %s", logpath);

	/* Initialize the resource tree. */
	ze_coap_init_resources(context);


	/* Fire threads, at last.. */
	/*
	 * In a two-thread scenario:
	 * - CoAP server needs *cctx, *smreqbu, *notbuf
	 * - Streaming Manager needs *smctx, *smreqbuf, *notbuf
	 *
	 * Since most of the already made library function calls take
	 * coap_context_t and are unaware of our buffers,
	 * let's put references to our buffers inside coap_context_t
	 * so there's no need to change library function signatures.
	 * (for example, the GET POST etc.. handlers have a signature
	 * coap_context_t  *, struct coap_resource_t *, coap_address_t *,
	 * coap_pdu_t *, str *, coap_pdu_t *
	 * but they do need the SM buffer!)
	 *
	 * The Streaming Manager is tailored for the use of the buffers
	 * so we can give the references in a separate way.
	 */

	int smerr, coaperr;

	pthread_t streaming_manager_thread;
	struct sm_thread_args smargs;
	smargs.smctx = smctx;
	smargs.smreqbuf = smreqbuf;
	smargs.notbuf = notbuf;

	pthread_t coap_server_thread;
	struct coap_thread_args coapargs;
	coapargs.cctx = cctx;
	coapargs.smreqbuf = smreqbuf;
	coapargs.notbuf = notbuf;

	smerr = pthread_create(&streaming_manager_thread, NULL,
			ze_coap_streaming_thread, &smargs);
	if (smerr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(error));
		exit(1);
	}

	coaperr = pthread_create(&coap_server_thread, NULL,
			ze_coap_server_core_thread, &coapargs);
	if (coaperr != 0) {
		LOGW("Failed to create thread: %s\n", strerror(error));
		exit(1);
	}

	/* Logging. */
	if (fputs("Threads launched correctly on ", logfd)<0) LOGW("write failed");
	if (fputs(ctime(&lt), logfd)<0) LOGW("write failed");


	/* Rejoin our children, otherwise the variables we've
	 * created will go out of scope.. */
	int *exitcode;
	pthread_join(streaming_manager_thread, &exitcode);
	pthread_join(ze_coap_server_core_thread, &exitcode);
}


/* Interprets IP & Port on which opens and binds a socket. */
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
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
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

  fprintf(stderr, "no context available for interface '%s'\n", node);

 finish:
  freeaddrinfo(result);
  return ctx;
}

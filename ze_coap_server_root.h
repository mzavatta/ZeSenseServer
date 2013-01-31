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
#include "resource.h"
#include "coap.h"

#include "ze_coap_resources.h"
#include "ze_coap_server_core.h"
#include "ze_streaming_manager.h"

#include "ze_log.h"

// Server endpoint coordinates
#define SERVER_IP "192.168.43.1"	//Wifi hotspot interface
//#define SERVER_IP "10.0.2.15" 	//Android emulator interface
#define SERVER_PORT "5683"

// Start function
int Java_eu_tb_zesense_ZeJNIHub_ze_1coap_1server_1example_1main();

#endif

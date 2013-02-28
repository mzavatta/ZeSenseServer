/*
 * ZeSense Streaming Manager
 * -- payload structures
 *
 * Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 * <marco.zavatta@mail.polimi.it>
 */

#ifndef ZE_PAYLOAD_H
#define ZE_PAYLOAD_H

/* Guide the space allocated to transport floating point
 * values. For the moment we use a fixed value, it's simpler.
 */
#define CHLEN 20

/* Packet types. */
#define DATAPOINT	1
#define SENDREPORT	2
#define RECREPORT	3

/* Sensor register mirrors Android's one. */
//...

/* Handy to supply to interfaces as a unique opaque
 * buffe rand its length
 */
typedef struct ze_payload_t {
	int length;
	unsigned char *data;
} ze_payload_t;

/* Common to all payloads in our solution,
 * distinguishes the payload structures. */
typedef struct {
	int packet_type;
} ze_payload_header_t;

/* Common to all data packets in our application. */
typedef struct {
	int sensor_type; //catalog to distinguish semantics and length of the data fiels
	int ts; //timestamp
	//int sn; //sequence number !!! XXX !!! it is conceptually at the packet level!
} ze_paydata_header_t;

/* Sender report structure. It is independent on the data carried
 * and therefore there's no need to include sensor specific
 * information. */
typedef struct {
	ze_payload_header_t phdr;	//0-3
	long ntp;					//4-11
	int ts;						//12-15
	int pc;						//16-19
	int oc;						//20-23
} ze_payload_sr_t;				//tot 24 bytes

/* One struct for each sensor data because the transported
 * values are different: some are 2, 3 axis, some transport
 * integers and others transport floats etc..
 */
typedef struct {
	ze_payload_header_t phdr;	//0-3
	ze_paydata_header_t pdhdr;	//4-15
	ze_accel_vector_t	vector;	//16-75
} ze_accel_paydata_t;			//tot 76 bytes

typedef struct {
	char x[CHLEN];				//0-19
	char y[CHLEN];				//20-39
	char z[CHLEN];				//40-59
} ze_accel_vector_t;			//tot 60 bytes

typedef struct {
	ze_payload_header_t phdr;
	ze_paydata_header_t pdhdr;
	char lat[CHLEN];
	char lon[CHLEN];
	char alt[CHLEN];
} ze_loc_paydata_t;

#endif

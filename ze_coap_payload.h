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

/* Guide the space allocated to transport character-encoded
 * floating point values. For the moment we use a fixed value,
 * it's simpler.
 */
#define CHARLEN 20

/* Packet types. */
#define DATAPOINT	1
#define SENDREPORT	2
#define RECREPORT	3

/* Sensor register mirrors Android's one. */
//...

/* Handy to supply to interfaces as a unique opaque
 * buffer and its length
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
} ze_paydata_header_t;

/* Sender report structure. It is independent on the data carried
 * and therefore there's no need to include sensor specific
 * information. */
typedef struct {
	long ntp;
	int ts;
	int pc;
	int oc;
} ze_payload_sr_t;	 //tot 20 bytes

typedef struct {
	char x[CHARLEN];				//0-19
	char y[CHARLEN];				//20-39
	char z[CHARLEN];				//40-59
} ze_accel_vector_t;			//tot 60 bytes

typedef struct {
	char lat[CHARLEN];
	char lon[CHARLEN];
	char alt[CHARLEN];
} ze_loc_vector_t;			//tot 60 bytes

/* One struct for each sensor data because the transported
 * values are different: some are 2, 3 axis, some transport
 * integers and others transport floats etc..
 */
/*typedef struct {
	ze_payload_header_t phdr;	//0-3
	ze_paydata_header_t pdhdr;	//4-11
	ze_accel_vector_t	vector;	//12-71
} ze_accel_paydata_t;			//tot 72 bytes*/

/*
typedef struct {
	ze_payload_header_t phdr;
	ze_paydata_header_t pdhdr;
	char lat[CHLEN];
	char lon[CHLEN];
	char alt[CHLEN];
} ze_loc_paydata_t;*/

#endif

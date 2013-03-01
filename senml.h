/*
 * ZeSense
 * -- SenML encoder
 *
 * Author: Marco Zavatta
 * <marco.zavatta@telecom-bretagne.eu>
 */

/*

define a new entry attribute called timestamp, "ts" key in JSON
model RTCP parameters as what draft-senml defines other parameters
that have the same status as sensor measurements

simple data packet
{
"e":[
	{ "n":"x", "v": 9.81, "ts": 68634, "u":"m/s2" }
	{ "n":"y", "v": 0.12, "ts": 68634, "u":"m/s2" }
	{ "n":"z", "v": 0.04, "ts": 68634, "u":"m/s2" }
	],
"bn":"192.168.0.40:1234/accel"
}



data packet along with sender report
(or do we leave the sender report outside as in pure RTCP?)
{
"e":[
	{ "n":"x", "v": 9.81, "ts": 68634, "u":"m/s2" }
	{ "n":"y", "v": 0.12, "ts": 68634, "u":"m/s2" }
	{ "n":"z", "v": 0.04, "ts": 68634, "u":"m/s2" }
	{ "n":"ntp", "v": 33558425, "u":"s" }  //NTP timestamp
	{ "n":"rtp", "v": 68650 }  //RTP timestamp
	{ "n":"oc", "v": 2214 }		//octect count
	{ "n":"pc", "v": 40 }		//packet count
	],
"bn":"192.168.0.40:1234/accel"
}
this includes ssrc and cname : 192.168.0.40:1234/accel
and all the fields of the sender info in the RTCP SR

receiver report
{
"e":[
	{ "n":"fl", "v": 0.1258 } //fraction lost
	{ "n":"cnpl", "v": 24 }		//cumulative number of packets lost
	{ "n":"ehsnr", "v": 2214 }			//extended highest sequence number received
	{ "n":"ij", "v": 40 }			//interarrival jitter
	{ "n":"lsr", "v": 40 }			//last SR timestamp
	{ "n":"dlsr", "v": 40 }			//delay since last SR
	]
}
in this case, if we map the sender report to a new
subscribe request, 192.168.0.40:1234/accel is already
embedded in the request header

 */


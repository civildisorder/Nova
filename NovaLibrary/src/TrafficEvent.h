//============================================================================
// Name        : TrafficEvent.h
// Author      : DataSoft Corporation
// Copyright   : GNU GPL v3
// Description : Information on a single or set of packets that allows for
//					identification and classification of the sender.
//============================================================================/*

#include <time.h>
#include <string.h>
#include <string>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/if_ether.h>
#include <vector>
#include <pcap.h>
#include <tr1/unordered_map>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/is_bitwise_serializable.hpp>


#ifndef TRAFFICEVENT_H_
#define TRAFFICEVENT_H_

#define TRAFFIC_EVENT_MTYPE 2
#define SCAN_EVENT_MTYPE 3

//Size of the static elements: 44
//Rest allocated for IP_packet_sizes. One int for each packet
//TODO: Increase this value if proven necessary.
#define TRAFFIC_EVENT_MAX_SIZE 1028

//From the Haystack or Doppelganger Module
#define FROM_HAYSTACK_DP	1
//From the Local Traffic Monitor
#define FROM_LTM			0

///	A struct for statically sized TrafficEvent Information
struct TEvent
{
	///Timestamp of the begining of this event
	time_t start_timestamp;
	///Timestamp of the end of this event
	time_t end_timestamp;

	///Source IP address of this event
	struct in_addr src_IP;
	///Destination IP address of this event
	struct in_addr dst_IP;

	///Source port of this event
	in_port_t src_port;
	///Destination port of this event
	in_port_t dst_port;

	///Total amount of IP layer bytes sent to the victim
	uint IP_total_data_bytes;

	///The IP proto type number
	///	IE: 6 == TCP, 17 == UDP, 1 == ICMP, etc...
	uint IP_protocol;

	///ICMP specific values
	///IE:	0,0 = Ping reply
	///		8,0 = Ping request
	///		3,3 = Destination port unreachable
	int ICMP_type;
	///IE:	0,0 = Ping reply
	///		8,0 = Ping request
	///		3,3 = Destination port unreachable
	int ICMP_code;

	///Packets involved in this event
	uint packet_count;

	///	False for from the host machine
	bool from_haystack;

	///For training use. Is this a hostile Event?
	bool isHostile;
};

using namespace std;
namespace Nova{
///An event which occurs on the wire.
class TrafficEvent
{
	public:

		//********************
		//* Member Variables *
		//********************

	///Timestamp of the begining of this event
		time_t start_timestamp;
		///Timestamp of the end of this event
		time_t end_timestamp;

		///Source IP address of this event
		struct in_addr src_IP;
		///Destination IP address of this event
		struct in_addr dst_IP;

		///Source port of this event
		in_port_t src_port;
		///Destination port of this event
		in_port_t dst_port;

		///Total amount of IP layer bytes sent to the victim
		uint IP_total_data_bytes;

		///The IP proto type number
		///	IE: 6 == TCP, 17 == UDP, 1 == ICMP, etc...
		uint IP_protocol;

		///ICMP specific values
		///IE:	0,0 = Ping reply
		///		8,0 = Ping request
		///		3,3 = Destination port unreachable
		int ICMP_type;
		///IE:	0,0 = Ping reply
		///		8,0 = Ping request
		///		3,3 = Destination port unreachable
		int ICMP_code;

		///Packets involved in this event
		uint packet_count;

		///Did this event originate from the Haystack?	///	Meta information about packet
		struct pcap_pkthdr pcap_header;
		///	Pointer to an IP header
		struct ip ip_hdr;
		/// Pointer to a TCP Header
		struct tcphdr tcp_hdr;
		/// Pointer to a UDP Header
		struct udphdr udp_hdr;
		/// Pointer to an ICMP Header
		struct icmphdr icmp_hdr;
		///	False for from the host machine
		bool from_haystack;

		///For training use. Is this a hostile Event?
		bool isHostile;

		///A vector of the sizes of the IP layers of
		vector <int> IP_packet_sizes;
		///A vector of start times for all packets in a tcp session
		vector <time_t> packet_intervals;

		//********************
		//* Member Functions *
		//********************

		///Used in serialization
		TrafficEvent();

		///UDP Constructor
		// int component_source is one of FROM_HAYSTACK or FROM_LTM
		TrafficEvent(struct Packet packet, int component_source);

		///TCP Constructor
		// int component_source is one of FROM_HAYSTACK or FROM_LTM
		TrafficEvent( vector <struct Packet>  *list, int component_source);

		///Returns a string representation of the LogEntry for printing to screen
		string ToString();

		///Returns true if the majority of packets are flagged as hostile
		/// Else, false
		bool ArePacketsHostile( vector<struct Packet>  *list);

		//Copies the contents of this TrafficEvent to the parameter TrafficEvent
		void copyTo(TrafficEvent *toEvent);

		//Serializes the event into a char buffer
		string serializeEvent();

		//Deserializes an event from a char buffer
		void deserializeEvent(string buf);

	private:
		friend class boost::serialization::access;
		template<class Archive>
		void serialize(Archive & ar, const unsigned int version)
		{
			ar & IP_packet_sizes;
			ar & packet_intervals;
		}
};



///	A struct version of a packet, as received from libpcap
struct Packet
{
	///	Meta information about packet
	struct pcap_pkthdr pcap_header;
	///	Pointer to an IP header
	struct ip ip_hdr;
	/// Pointer to a TCP Header
	struct tcphdr tcp_hdr;
	/// Pointer to a UDP Header
	struct udphdr udp_hdr;
	/// Pointer to an ICMP Header
	struct icmphdr icmp_hdr;
};
}
BOOST_IS_BITWISE_SERIALIZABLE(Nova::TrafficEvent);

#endif /* TRAFFICEVENT_H_ */

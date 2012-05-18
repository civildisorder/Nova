//============================================================================
// Name        : Evidence.cpp
// Copyright   : DataSoft Corporation 2011-2012
//	Nova is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   Nova is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with Nova.  If not, see <http://www.gnu.org/licenses/>.
// Description : Evidence object represents a preprocessed ip packet
//					for inclusion in a Suspect's Feature Set
//============================================================================/*

#include "Evidence.h"

using namespace std;

namespace Nova
{

//Default Constructor
Evidence::Evidence()
{
	m_next = NULL;
	m_evidencePacket.dst_port = 0;
	m_evidencePacket.ip_dst = 0;
	m_evidencePacket.ip_len = 0;
	m_evidencePacket.ip_p = 0;
	m_evidencePacket.ip_src = 0;
	m_evidencePacket.ts = 0;
}

Evidence::Evidence(const u_char * const& packet_at_ip_header, const pcap_pkthdr * const& pkthdr)
{
	//Get timestamp
	m_evidencePacket.ts = pkthdr->ts.tv_sec;

	//Copy out vals from header
	const u_char* offset = packet_at_ip_header + 9; // @9 - read 2
	m_evidencePacket.ip_len = ntohs((uint16_t)*packet_at_ip_header);
	offset += 7; // @16 - read 1
	m_evidencePacket.ip_p = (uint8_t)*packet_at_ip_header;
	offset += 3; // @19 - read 4
	m_evidencePacket.ip_src = ntohl((uint32_t)*packet_at_ip_header);
	offset += 4; // @23 - read 4
	m_evidencePacket.ip_dst = ntohl((uint32_t)*packet_at_ip_header);
	offset += 7; // @30 - read 2 //Same for udp or tcp

	//If TCP or UDP
	if((m_evidencePacket.ip_p == 6) || (m_evidencePacket.ip_p == 17))
	{
		m_evidencePacket.dst_port = ntohs((uint16_t)*packet_at_ip_header);
	}
	//Any other protocol
	else
	{
		m_evidencePacket.dst_port = 0;
	}
}

Evidence::Evidence(const _evidencePacket& packet)
{
	m_evidencePacket = packet;
	m_next = NULL;
}

}

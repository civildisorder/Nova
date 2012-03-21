//============================================================================
// Name        : RequestMessage.h
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
// Description : Requests from the GUI to Novad to get current state information
//============================================================================

#ifndef RequestMessage_H_
#define RequestMessage_H_

#include "UI_Message.h"
#include "../Suspect.h"

#include <vector>
#include <arpa/inet.h>

#define REQUEST_MSG_MIN_SIZE 3

//The different message types
enum RequestType: char
{
	// Requests for lists of suspect IPs
	REQUEST_SUSPECTLIST= 0,
	REQUEST_SUSPECTLIST_REPLY,

	// Request for an individual suspect
	REQUEST_SUSPECT,
	REQUEST_SUSPECT_REPLY,
};

enum SuspectListType : char
{
	SUSPECTLIST_ALL = 0,
	SUSPECTLIST_HOSTILE,
	SUSPECTLIST_BENIGN
};

namespace Nova
{

class RequestMessage : public UI_Message
{

public:

	RequestMessage();
	~RequestMessage();

	//Deserialization constructor
	//	buffer - pointer to array in memory where serialized RequestMessage resides
	//	length - the length of this array
	//	On error, sets m_serializeError to true, on success sets it to false
	RequestMessage(char *buffer, uint32_t length);

	//Serializes the UI_Message object into a char array
	//	*length - Return parameter, specifies the length of the serialized array returned
	// Returns - A pointer to the serialized array
	//	NOTE: The caller must manually free() the returned buffer after use

	enum RequestType m_requestType;

	// For suspect list
	enum SuspectListType m_listType;
	uint32_t m_suspectListLength;
	std::vector<in_addr_t> m_suspectList;

	// For returning a single suspect
	Suspect *m_suspect;
	uint32_t m_suspectLength;
	in_addr_t m_suspectAddress;

protected:
	//Serializes the requestinto a char array
	//	*length - Return parameter, specifies the length of the serialized array returned
	// Returns - A pointer to the serialized array
	//	NOTE: The caller must manually free() the returned buffer after use
	char *Serialize(uint32_t *length);

	// Serializes just the UI message and request message type into the buffer
	//	*buffer: Pointer to where to store the serialized values
	// Returns: Number of bytes written to *buffer
	int SerializeHeader(char *buffer);
};

}

#endif /* RequestMessage_H_ */
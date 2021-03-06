//============================================================================
// Name        : ControlMessage.h
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
// Description : Message objects sent to control Novad's operation
//	Novad should reply either with a success message or an ack. For more complicated
//	return messages, consider using RequestMessage instead.
//============================================================================

#ifndef CONTROLMESSAGE_H_
#define CONTROLMESSAGE_H_

#include <sys/types.h>
#include <arpa/inet.h>
#include "../../Suspect.h"

#include "Message.h"

//Maximum size of a linux file path
#define MAX_PATH_SIZE 4096
//Includes the UI_Message type and ControlType
#define CONTROL_MSG_MIN_SIZE 2

//The different message types
enum ControlType: char
{
	CONTROL_EXIT_REQUEST = 0,		//Request for Novad to exit
	CONTROL_EXIT_REPLY,				//Reply from Novad with success
	CONTROL_CLEAR_ALL_REQUEST,		//Request for Novad to clear all suspects
	CONTROL_CLEAR_ALL_REPLY,		//Reply from Novad with success
	CONTROL_CLEAR_SUSPECT_REQUEST,	//Request for Novad to clear specified suspect
	CONTROL_CLEAR_SUSPECT_REPLY,	//Reply from Novad with success
	CONTROL_SAVE_SUSPECTS_REQUEST,	//Request for Novad to save the suspects list to persistent storage
	CONTROL_SAVE_SUSPECTS_REPLY,	//Reply from Novad with success
	CONTROL_RECLASSIFY_ALL_REQUEST,	//Request for Novad to reclassify all suspects
	CONTROL_RECLASSIFY_ALL_REPLY,	//Reply from Novad with success
	CONTROL_CONNECT_REQUEST,		//Request to connect to Novad from UI
	CONTROL_CONNECT_REPLY,			//Reply from Novad with success
	CONTROL_DISCONNECT_NOTICE,		//Notice to Novad that the UI is closing
	CONTROL_DISCONNECT_ACK,
	CONTROL_START_CAPTURE,			// Start packet capture on configured interfaces
	CONTROL_START_CAPTURE_ACK,
	CONTROL_STOP_CAPTURE,			// Stop packet capture on configured interfaces
	CONTROL_STOP_CAPTURE_ACK,
	CONTROL_INVALID
};

namespace Nova
{

class ControlMessage : public Message
{

public:

	//Empty constructor
	ControlMessage(enum ControlType controlType);

	virtual ~ControlMessage();

	//Deserialization constructor
	//	buffer - pointer to array in memory where serialized ControlMessage resides
	//	length - the length of this array
	//	On error, sets m_serializeError to true, on success sets it to false
	ControlMessage(char *buffer, uint32_t length);

	enum ControlType m_controlType;

	// The argument, if applicable.
	char m_filePath[MAX_PATH_SIZE];

	SuspectIdentifier m_suspectAddress;

	//Did the requested command succeed?
	bool m_success;

	//Serializes the UI_Message object into a char array
	//	*length - Return parameter, specifies the length of the serialized array returned
	// Returns - A pointer to the serialized array
	//	NOTE: The caller must manually free() the returned buffer after use
	char *Serialize(uint32_t *length);
};
}

#endif /* CONTROLMESSAGE_H_ */

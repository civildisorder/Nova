//============================================================================
// Name        : ControlMessage.cpp
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

#include "ControlMessage.h"
#include <sys/un.h>
#include <iostream>

using namespace std;

namespace Nova
{

ControlMessage::ControlMessage(enum ControlType controlType)
{
	m_messageType = CONTROL_MESSAGE;
	m_controlType = controlType;
}

ControlMessage::~ControlMessage()
{

}

ControlMessage::ControlMessage(char *buffer, uint32_t length)
{
	if( length < CONTROL_MSG_MIN_SIZE )
	{
		m_serializeError = true;
		return;
	}

	m_serializeError = false;

	//Deserialize the UI_Message header
	if(!DeserializeHeader(&buffer))
	{
		m_serializeError = true;
		return;
	}

	//Copy the control message type
	memcpy(&m_controlType, buffer, sizeof(m_controlType));
	buffer += sizeof(m_controlType);

	switch(m_controlType)
	{
		case CONTROL_EXIT_REQUEST:
		case CONTROL_CLEAR_ALL_REQUEST:
		case CONTROL_DISCONNECT_NOTICE:
		case CONTROL_DISCONNECT_ACK:
		case CONTROL_RECLASSIFY_ALL_REQUEST:
		case CONTROL_CONNECT_REQUEST:
		case CONTROL_START_CAPTURE:
		case CONTROL_START_CAPTURE_ACK:
		case CONTROL_STOP_CAPTURE:
		case CONTROL_STOP_CAPTURE_ACK:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type

			uint32_t expectedSize = MESSAGE_HDR_SIZE + sizeof(m_controlType);
			if(length != expectedSize)
			{
				m_serializeError = true;
				return;
			}

			break;
		}

		case CONTROL_EXIT_REPLY:
		case CONTROL_CLEAR_SUSPECT_REPLY:
		case CONTROL_SAVE_SUSPECTS_REPLY:
		case CONTROL_CLEAR_ALL_REPLY:
		case CONTROL_RECLASSIFY_ALL_REPLY:
		case CONTROL_CONNECT_REPLY:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type
			//		3) Boolean success

			uint32_t expectedSize = MESSAGE_HDR_SIZE + sizeof(m_controlType) + sizeof(m_success);
			if(length != expectedSize)
			{
				m_serializeError = true;
				return;
			}

			memcpy(&m_success, buffer, sizeof(m_success));
			buffer += sizeof(m_success);

			break;
		}

		case CONTROL_CLEAR_SUSPECT_REQUEST:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type
			//		3) Suspect IP to clear

			m_suspectAddress.Deserialize(reinterpret_cast<u_char*>(buffer), length);

			break;
		}

		case CONTROL_SAVE_SUSPECTS_REQUEST:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type

			uint32_t expectedSize = MESSAGE_HDR_SIZE + sizeof(m_controlType) + sizeof(m_filePath);
			if(length != expectedSize)
			{
				m_serializeError = true;
				return;
			}

			memcpy(m_filePath, buffer, sizeof(m_filePath));
			buffer += sizeof(m_filePath);

			break;
		}

		case CONTROL_INVALID:
		{
			break;
		}
		default:
		{
			m_serializeError = true;
			return;
		}
	}
}
char *ControlMessage::Serialize(uint32_t *length)
{
	char *buffer = NULL, *originalBuffer = NULL;
	uint32_t messageSize = 0;

	switch(m_controlType)
	{
		case CONTROL_EXIT_REQUEST:
		case CONTROL_CLEAR_ALL_REQUEST:
		case CONTROL_DISCONNECT_NOTICE:
		case CONTROL_DISCONNECT_ACK:
		case CONTROL_RECLASSIFY_ALL_REQUEST:
		case CONTROL_CONNECT_REQUEST:
		case CONTROL_START_CAPTURE:
		case CONTROL_START_CAPTURE_ACK:
		case CONTROL_STOP_CAPTURE:
		case CONTROL_STOP_CAPTURE_ACK:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type
			messageSize = MESSAGE_HDR_SIZE + sizeof(m_controlType) + sizeof(messageSize);
			buffer = (char*)malloc(messageSize);
			originalBuffer = buffer;

			SerializeHeader(&buffer, messageSize);
			//Put the Control Message type in
			memcpy(buffer, &m_controlType, sizeof(m_controlType));
			buffer += sizeof(m_controlType);

			break;
		}

		case CONTROL_EXIT_REPLY:
		case CONTROL_CLEAR_SUSPECT_REPLY:
		case CONTROL_SAVE_SUSPECTS_REPLY:
		case CONTROL_CLEAR_ALL_REPLY:
		case CONTROL_RECLASSIFY_ALL_REPLY:
		case CONTROL_CONNECT_REPLY:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type
			//		3) Boolean success
			messageSize = MESSAGE_HDR_SIZE + sizeof(m_controlType) + sizeof(m_success)+ sizeof(messageSize);
			buffer = (char*)malloc(messageSize);
			originalBuffer = buffer;

			SerializeHeader(&buffer, messageSize);
			//Put the Control Message type in
			memcpy(buffer, &m_controlType, sizeof(m_controlType));
			buffer += sizeof(m_controlType);
			//Put the Control Message type in
			memcpy(buffer, &m_success, sizeof(m_success));
			buffer += sizeof(m_success);

			break;
		}

		case CONTROL_CLEAR_SUSPECT_REQUEST:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type
			//		3) IP of suspect to clear
			messageSize = MESSAGE_HDR_SIZE + sizeof(m_controlType) + sizeof(messageSize) + m_suspectAddress.GetSerializationLength();
			buffer = (char*)malloc(messageSize);
			originalBuffer = buffer;

			SerializeHeader(&buffer, messageSize);
			//Put the Control Message type in
			memcpy(buffer, &m_controlType, sizeof(m_controlType));
			buffer += sizeof(m_controlType);

			//Put the Control Message type in
			m_suspectAddress.Serialize(reinterpret_cast<u_char*>(buffer), messageSize);

			break;
		}

		case CONTROL_SAVE_SUSPECTS_REQUEST:
		{
			//Uses: 1) UI_Message Header
			//		2) ControlMessage Type
			// 		3) Path of file to save to
			messageSize = MESSAGE_HDR_SIZE + sizeof(m_controlType) + sizeof(m_filePath) + sizeof(messageSize);
			buffer = (char*)malloc(messageSize);
			originalBuffer = buffer;

			SerializeHeader(&buffer, messageSize);
			//Put the Control Message type in
			memcpy(buffer, &m_controlType, sizeof(m_controlType));
			buffer += sizeof(m_controlType);

			memcpy(buffer, m_filePath, sizeof(m_filePath));
			buffer += sizeof(m_filePath);

			break;
		}

		case CONTROL_INVALID:
		{
			break;
		}
		default:
		{
			//Error
			return NULL;
		}
	}

	*length = messageSize;
	return originalBuffer;
}

}

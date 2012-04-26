//============================================================================
// Name        : StatusQueries.cpp
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
// Description : Handles requests for information from Novad
//============================================================================

#include "messaging/MessageManager.h"
#include "messaging/messages/ControlMessage.h"
#include "messaging/messages/RequestMessage.h"
#include "messaging/messages/ErrorMessage.h"
#include "messaging/Socket.h"
#include "Commands.h"
#include "Logger.h"
#include "Lock.h"

#include <iostream>

using namespace Nova;
using namespace std;

extern int IPCSocketFD;
extern bool isFirstConnect;

namespace Nova
{
bool IsNovadUp()
{
	if(isFirstConnect)
	{
		//If we couldn't connect, then it's definitely not up
		return ConnectToNovad();
	}
	else if(IPCSocketFD == -1)
	{
		return false;
	}

	Lock lock = MessageManager::Instance().UseSocket(IPCSocketFD);

	ControlMessage ping(CONTROL_PING, DIRECTION_TO_NOVAD);
	if(!UI_Message::WriteMessage(&ping, IPCSocketFD) )
	{
		//There was an error in sending the message
		return false;
	}

	UI_Message *reply = UI_Message::ReadMessage(IPCSocketFD, DIRECTION_TO_NOVAD, REPLY_TIMEOUT);
	if (reply->m_messageType == ERROR_MESSAGE && ((ErrorMessage*)reply)->m_errorType == ERROR_TIMEOUT)
	{
		LOG(ERROR, "Timeout error when waiting for message reply", "");
		delete ((ErrorMessage*)reply);
		return false;
	}

	if(reply->m_messageType == ERROR_MESSAGE )
	{
		ErrorMessage *error = (ErrorMessage*)reply;
		if(error->m_errorType == ERROR_SOCKET_CLOSED)
		{
			// This was breaking things during the mess of isNovadUp calls
			// when the QT GUi starts and connects to novad. If there was some
			// important reason for it being here that I don't know about, we
			// might need to put it back and track down why exactly it was
			// causing problems.
			//CloseNovadConnection();
		}
		delete error;
		return false;
	}
	if(reply->m_messageType != CONTROL_MESSAGE )
	{
		//Received the wrong kind of message
		delete reply;
		return false;
	}

	ControlMessage *pong = (ControlMessage*)reply;
	if(pong->m_controlType != CONTROL_PONG)
	{
		//Received the wrong kind of control message
		delete pong;
		return false;
	}
	delete pong;
	return true;
}

int GetUptime()
{
	Lock lock = MessageManager::Instance().UseSocket(IPCSocketFD);

	RequestMessage request(REQUEST_UPTIME, DIRECTION_TO_NOVAD);

	if(!UI_Message::WriteMessage(&request, IPCSocketFD) )
	{
		return 0;
	}

	UI_Message *reply = UI_Message::ReadMessage(IPCSocketFD, DIRECTION_TO_NOVAD, REPLY_TIMEOUT);
	if (reply->m_messageType == ERROR_MESSAGE && ((ErrorMessage*)reply)->m_errorType == ERROR_TIMEOUT)
	{
		LOG(ERROR, "Timeout error when waiting for message reply", "");
		delete ((ErrorMessage*)reply);
		return 0;
	}

	if(reply->m_messageType == ERROR_MESSAGE )
	{
		ErrorMessage *error = (ErrorMessage*)reply;
		if(error->m_errorType == ERROR_SOCKET_CLOSED)
		{
			CloseNovadConnection();
		}
		delete error;
		return 0;
	}
	if(reply->m_messageType != REQUEST_MESSAGE )
	{
		//Received the wrong kind of message
		delete reply;
		return 0;
	}

	RequestMessage *requestReply = (RequestMessage*)reply;
	if(requestReply->m_requestType != REQUEST_UPTIME_REPLY)
	{
		//Received the wrong kind of control message
		delete requestReply;
		return 0;
	}

	int ret = requestReply->m_uptime;

	delete requestReply;
	return ret;
}

vector<in_addr_t> *GetSuspectList(enum SuspectListType listType)
{
	Lock lock = MessageManager::Instance().UseSocket(IPCSocketFD);

	RequestMessage request(REQUEST_SUSPECTLIST, DIRECTION_TO_NOVAD);
	request.m_listType = listType;

	if(!UI_Message::WriteMessage(&request, IPCSocketFD) )
	{
		//There was an error in sending the message
		return NULL;
	}

	UI_Message *reply = UI_Message::ReadMessage(IPCSocketFD, DIRECTION_TO_NOVAD, REPLY_TIMEOUT);
	if (reply->m_messageType == ERROR_MESSAGE && ((ErrorMessage*)reply)->m_errorType == ERROR_TIMEOUT)
	{
		LOG(ERROR, "Timeout error when waiting for message reply", "");
		delete ((ErrorMessage*)reply);
		return NULL;
	}

	if(reply->m_messageType == ERROR_MESSAGE )
	{
		ErrorMessage *error = (ErrorMessage*)reply;
		if(error->m_errorType == ERROR_SOCKET_CLOSED)
		{
			CloseNovadConnection();
		}
		delete error;
		return NULL;
	}
	if(reply->m_messageType != REQUEST_MESSAGE )
	{
		//Received the wrong kind of message
		delete reply;
		return NULL;
	}

	RequestMessage *requestReply = (RequestMessage*)reply;
	if(requestReply->m_requestType != REQUEST_SUSPECTLIST_REPLY)
	{
		//Received the wrong kind of control message
		delete requestReply;
		return NULL;
	}

	vector<in_addr_t> *ret = new vector<in_addr_t>;
	*ret = requestReply->m_suspectList;

	delete requestReply;
	return ret;
}

Suspect *GetSuspect(in_addr_t address)
{
	Lock lock = MessageManager::Instance().UseSocket(IPCSocketFD);

	RequestMessage request(REQUEST_SUSPECT, DIRECTION_TO_NOVAD);
	request.m_suspectAddress = address;


	if(!UI_Message::WriteMessage(&request, IPCSocketFD) )
	{
		//There was an error in sending the message
		return NULL;
	}


	UI_Message *reply = UI_Message::ReadMessage(IPCSocketFD, DIRECTION_TO_NOVAD, REPLY_TIMEOUT);
	if (reply->m_messageType == ERROR_MESSAGE && ((ErrorMessage*)reply)->m_errorType == ERROR_TIMEOUT)
	{
		LOG(ERROR, "Timeout error when waiting for message reply", "");
		delete ((ErrorMessage*)reply);
		return NULL;
	}

	if(reply->m_messageType == ERROR_MESSAGE )
	{
		ErrorMessage *error = (ErrorMessage*)reply;
		if(error->m_errorType == ERROR_SOCKET_CLOSED)
		{
			CloseNovadConnection();
		}
		delete error;
		return NULL;
	}
	if(reply->m_messageType != REQUEST_MESSAGE)
	{
		//Received the wrong kind of message
		delete reply;
		return NULL;
	}

	RequestMessage *requestReply = (RequestMessage*)reply;
	if(requestReply->m_requestType != REQUEST_SUSPECT_REPLY)
	{
		//Received the wrong kind of control message
		delete requestReply;
		return NULL;
	}

	Suspect *returnSuspect = new Suspect();
	*returnSuspect = *requestReply->m_suspect;
	delete requestReply;

	return returnSuspect;
}
}

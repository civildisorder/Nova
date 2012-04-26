//============================================================================
// Name        : MessageManager.h
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
// Description : Manages all incoming messages on sockets
//============================================================================

#include "MessageManager.h"
#include "../Lock.h"
#include "../Logger.h"
#include "messages/ErrorMessage.h"

#include <sys/socket.h>

namespace Nova
{

MessageManager *MessageManager::m_instance = NULL;

MessageManager::MessageManager(enum ProtocolDirection direction)
{
	pthread_mutex_init(&m_queuesLock, NULL);
	pthread_mutex_init(&m_socketsLock, NULL);
	m_forwardDirection = direction;
}

void MessageManager::Initialize(enum ProtocolDirection direction)
{
	m_instance = new MessageManager(direction);
}

MessageManager &MessageManager::Instance()
{
	if(m_instance == NULL)
	{
		LOG(ERROR, "Critical error in Message Manager", "Critical error in MessageManager: You must first initialize it with a direction"
				"before calling Instance()");
	}
	return *MessageManager::m_instance;
}

UI_Message *MessageManager::GetMessage(int socketFD, enum ProtocolDirection direction)
{
	//Initialize the queue lock, if it doesn't exist
	{
		Lock queuesLock(&m_queuesLock);
		if(m_queueLocks.find(socketFD) == m_queueLocks.end())
		{
			//If there is no lock object here yet, initialize it
			m_queueLocks[socketFD] = new pthread_mutex_t;
			pthread_mutex_init(m_queueLocks[socketFD], NULL);
		}
	}

	UI_Message *retMessage;
	{
		Lock lock(m_queueLocks[socketFD]);
		if(m_queues.find(socketFD) != m_queues.end())
		{
			retMessage = m_queues[socketFD]->PopMessage(direction);
		}
		else
		{
			return new ErrorMessage(ERROR_SOCKET_CLOSED);
		}
	}

	if(retMessage->m_messageType == ERROR_MESSAGE)
	{
		ErrorMessage *errorMessage = (ErrorMessage*)retMessage;
		if(errorMessage->m_errorType == ERROR_SOCKET_CLOSED)
		{
			{
				Lock lock(m_queueLocks[socketFD]);
				delete m_queues[socketFD];
				m_queues.erase(socketFD);
			}
			{
				Lock lock(m_socketLocks[socketFD]);
				delete m_sockets[socketFD];
				m_sockets.erase(socketFD);
			}
		}
	}

	return retMessage;
}

void MessageManager::StartSocket(int socketFD)
{
	//Initialize the socket lock if it doesn't yet exist
	{
		Lock socketLock(&m_socketsLock);
		if(m_socketLocks.find(socketFD) == m_socketLocks.end())
		{
			//If there is no lock object here yet, initialize it
			m_socketLocks[socketFD] = new pthread_mutex_t;
			pthread_mutex_init(m_socketLocks[socketFD], NULL);
		}
	}

	//Initialize the socket object if it doesn't already exist
	{
		Lock sockLock(m_socketLocks[socketFD]);
		if(m_sockets.find(socketFD) == m_sockets.end())
		{
			m_sockets[socketFD] = new Socket();
			m_sockets[socketFD]->m_socketFD = socketFD;
		}
	}

	pthread_mutex_t *qMutex;
	//Initialize the queue lock if it doesn't yet exist
	{
		Lock queueLock(&m_queuesLock);
		if(m_queueLocks.find(socketFD) == m_queueLocks.end())
		{
			//If there is no lock object here yet, initialize it
			m_queueLocks[socketFD] = new pthread_mutex_t;
			pthread_mutex_init(m_queueLocks[socketFD], NULL);
		}
		qMutex = m_queueLocks[socketFD];
	}

	//Initialize the MessageQueue if it doesn't yet exist
	//	NOTE: We use the above qMutex in order to avoid a nested lock situation
	{
		Lock qLock(qMutex);
		if(m_queues.find(socketFD) == m_queues.end())
		{
			m_queues[socketFD] = new MessageQueue(socketFD, m_forwardDirection);
		}
	}
}

const Lock& MessageManager::UseSocket(int socketFD)
{
	bool needsInit = false;
	{
		Lock lock(&m_socketsLock);
		if(m_socketLocks.find(socketFD) == m_socketLocks.end())
		{
			needsInit = true;
		}
	}
	if(needsInit)
	{
		StartSocket(socketFD);
	}
	const Lock * ret = NULL;
	{
		Lock lock(&m_socketsLock);
		const Lock& temp = Lock(m_socketLocks[socketFD]);
		ret = &temp;
	}
	return *ret;
}

void MessageManager::CloseSocket(int socketFD)
{
	Lock lock(&m_queuesLock);
	if(m_queues.find(socketFD) != m_queues.end())
	{
		shutdown(socketFD, SHUT_RDWR);
	}
}

void MessageManager::RegisterCallback(int socketFD)
{
	bool foundIt = false;
	{
		Lock lock(&m_queuesLock);
		if(m_queues.find(socketFD) != m_queues.end())
		{
			foundIt = true;
		}
	}
	if(foundIt)
	{
		m_queues[socketFD]->RegisterCallback();
	}
}

}

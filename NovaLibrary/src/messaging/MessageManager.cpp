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
#include "messages/ErrorMessage.h"

#include <sys/socket.h>

namespace Nova
{

MessageManager *MessageManager::m_instance = NULL;

MessageManager::MessageManager()
{
	pthread_mutex_init(&m_queuesLock, NULL);
}

MessageManager &MessageManager::Instance()
{
	if (m_instance == NULL)
	{
		m_instance = new MessageManager();
	}
	return *m_instance;
}

UI_Message *MessageManager::GetMessage(int socketFD)
{

	Lock queueLock;
	{
		Lock queueLock(&m_queuesLock);
		if(m_queueLocks.count(socketFD) == 0)
		{
			//If there is no lock object here yet, initialize it
			pthread_mutex_init(&m_queueLocks[socketFD], NULL);
		}
		queueLock.GetLock(&m_queueLocks[socketFD]);
	}

	UI_Message *retMessage;

	if(m_queues.count(socketFD) > 0)
	{
		retMessage = m_queues[socketFD]->PopMessage();
	}
	else
	{
		return new ErrorMessage(ERROR_SOCKET_CLOSED);
	}

	if(retMessage->m_messageType == ERROR_MESSAGE)
	{
		ErrorMessage *errorMessage = (ErrorMessage*)retMessage;
		if(errorMessage->m_errorType == ERROR_SOCKET_CLOSED)
		{
			delete m_queues[socketFD];
			m_queues.erase(socketFD);
		}
	}

	return retMessage;
}

void MessageManager::StartSocket(Socket socket)
{
	Lock queueLock(&m_queuesLock);

	if(m_queueLocks.count(socket.m_socketFD) == 0)
	{
		//If there is no lock object here yet, initialize it
		pthread_mutex_init(&m_queueLocks[socket.m_socketFD], NULL);
	}

	Lock lock(&m_queueLocks[socket.m_socketFD]);

	m_queues[socket.m_socketFD] = new MessageQueue(socket);

}

void MessageManager::CloseSocket(int socketFD)
{
	Lock lock(&m_queuesLock);

	if(m_queues.count(socketFD) > 0)
	{
		shutdown(socketFD, SHUT_RDWR);
	}
}

}
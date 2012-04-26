//============================================================================
// Name        : MessageQueue.cpp
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
// Description : An item in the MessageManager's table. Contains a pair of queues
//	of received messages on a particular socket
//============================================================================

#include "MessageQueue.h"
#include "../Lock.h"
#include "messages/ErrorMessage.h"

#include <sys/socket.h>
#include <errno.h>
#include "unistd.h"
#include "string.h"
#include "stdio.h"

namespace Nova
{

MessageQueue::MessageQueue(int socket, enum ProtocolDirection direction)
{
	pthread_mutex_init(&m_forwardQueueMutex, NULL);
	pthread_mutex_init(&m_popMutex, NULL);
	pthread_mutex_init(&m_callbackRegisterMutex, NULL);
	pthread_mutex_init(&m_callbackCondMutex, NULL);
	pthread_mutex_init(&m_callbackQueueMutex, NULL);
	pthread_cond_init(&m_readWakeupCondition, NULL);
	pthread_cond_init(&m_callbackWakeupCondition, NULL);

	m_callbackDoWakeup = false;
	m_forwardDirection = direction;
	m_socketFD = socket;

}

MessageQueue::~MessageQueue()
{
	//Shutdown will cause the producer thread to make an ErrorMessage then quit
	shutdown(m_socketFD, SHUT_RDWR);

	//We then must wait for the popping thread to finish
	//	Can't let it wake up into a destroyed object
	//	So this lock will either wait for the pop message to finish, or just
	//	go ahead if there is none
	{
		Lock lockPop();
	}

	pthread_mutex_destroy(&m_forwardQueueMutex);
	pthread_mutex_destroy(&m_popMutex);
	pthread_mutex_destroy(&m_callbackRegisterMutex);
	pthread_mutex_destroy(&m_callbackCondMutex);
	pthread_mutex_destroy(&m_callbackQueueMutex);
	pthread_cond_destroy(&m_readWakeupCondition);
	pthread_cond_destroy(&m_callbackWakeupCondition);
}

//blocking call
UI_Message *MessageQueue::PopMessage(enum ProtocolDirection direction)
{
	//Only one thread in this function at a time
	Lock lockPop(&m_popMutex);
	UI_Message* retMessage;

	if(direction == m_forwardDirection)
	{
		//Protection for the queue structure
		Lock lockQueue(&m_forwardQueueMutex);

		//While loop to protect against spurious wakeups
		while(m_forwardQueue.empty())
		{
			pthread_cond_wait(&m_readWakeupCondition, &m_forwardQueueMutex);
		}

		retMessage = m_forwardQueue.front();
		m_forwardQueue.pop();
	}
	else
	{
		//Protection for the queue structure
		Lock lockQueue(&m_callbackQueueMutex);

		//While loop to protect against spurious wakeups
		while(m_callbackQueue.empty())
		{
			pthread_cond_wait(&m_readWakeupCondition, &m_forwardQueueMutex);
		}

		retMessage = m_callbackQueue.front();
		m_callbackQueue.pop();
	}

	return retMessage;
}

void *MessageQueue::StaticThreadHelper(void *ptr)
{
	return reinterpret_cast<MessageQueue*>(ptr)->ProducerThread();
}

void MessageQueue::PushMessage(UI_Message *message)
{
	//Protection for the queue structure
	Lock lock(&m_forwardQueueMutex);

	m_forwardQueue.push(message);

	//If this is a callback message (not the forward direction)
	if(message->m_protocolDirection != m_forwardDirection)
	{
		//Protection for the m_callbackDoWakeup bool
		Lock condLock(&m_callbackCondMutex);
		m_callbackDoWakeup = true;
		pthread_cond_signal(&m_callbackWakeupCondition);
		pthread_cond_signal(&m_readWakeupCondition);
	}
	else
	{
		//If there are no sleeping threads, this simply does nothing
		pthread_cond_signal(&m_readWakeupCondition);
	}
}

void MessageQueue::RegisterCallback()
{
	//Only one thread in this function at a time
	Lock lock(&m_callbackRegisterMutex);
	//Protection for the m_callbackDoWakeup bool
	Lock condLock(&m_callbackCondMutex);

	while(!m_callbackDoWakeup)
	{
		pthread_cond_wait(&m_callbackWakeupCondition, &m_callbackCondMutex);
	}

	m_callbackDoWakeup = false;
}

void *MessageQueue::ProducerThread()
{
	while(true)
	{
		uint32_t length = 0;
		char buff[sizeof(length)];
		uint totalBytesRead = 0;
		int bytesRead = 0;

		// Read in the message length
		while( totalBytesRead < sizeof(length))
		{
			bytesRead = read(m_socketFD, buff + totalBytesRead, sizeof(length) - totalBytesRead);

			if( bytesRead < 0 )
			{
				//The socket died on us!
				//Put an error message on the queue, and quit reading
				perror(NULL);
				PushMessage(new ErrorMessage(ERROR_SOCKET_CLOSED));
				return NULL;
			}
			else
			{
				totalBytesRead += bytesRead;
			}
		}

		// Make sure the length appears valid
		// TODO: Assign some arbitrary max message size to avoid filling up memory by accident
		memcpy(&length, buff, sizeof(length));
		if (length == 0)
		{
			PushMessage(new ErrorMessage(ERROR_MALFORMED_MESSAGE));
			continue;
		}

		char *buffer = (char*)malloc(length);

		if (buffer == NULL)
		{
			// This should never happen. If it does, probably because length is an absurd value (or we're out of memory)
			PushMessage(new ErrorMessage(ERROR_MALFORMED_MESSAGE));
			continue;
		}

		// Read in the actual message
		totalBytesRead = 0;
		bytesRead = 0;
		while(totalBytesRead < length)
		{
			bytesRead = read(m_socketFD, buffer + totalBytesRead, length - totalBytesRead);

			if( bytesRead < 0 )
			{
				//The socket died on us!
				//Put an error message on the queue, and quit reading
				PushMessage(new ErrorMessage(ERROR_SOCKET_CLOSED));
				return NULL;
			}
			else
			{
				totalBytesRead += bytesRead;
			}
		}


		if(length < MESSAGE_MIN_SIZE)
		{
			PushMessage(new ErrorMessage(ERROR_MALFORMED_MESSAGE));
			continue;
		}

		PushMessage(UI_Message::Deserialize(buffer, length));
		continue;
	}

	return NULL;
}

}

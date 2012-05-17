//============================================================================
// Name        : EvidenceQueue.cpp
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
// Description : The EvidenceQueue object is a specialized linked-list
//			 of Evidence objects for high performance IP packet processing
//============================================================================/*

#include "EvidenceQueue.h"
#include <pthread.h>

using namespace std;

namespace Nova
{

EvidenceQueue::EvidenceQueue()
{
	m_first = NULL;
	m_last = NULL;
	pthread_mutex_init(&m_lock, NULL);
}

EvidenceQueue::~EvidenceQueue()
{
	//Grab the lock to assert another thread isn't currently using it
	pthread_mutex_lock(&m_lock);
	//Destroy the lock, any threads blocking on the lock will receive the EDESTROYED error val
	pthread_mutex_destroy(&m_lock);
}

Evidence * EvidenceQueue::Pop()
{
	pthread_mutex_lock(&m_lock);
	Evidence * ret = (Evidence *)m_first;
	if(ret)
	{
		m_first = ret->m_next;
		ret->m_next = NULL;
		//If m_first == NULL (empty queue)
		if(!m_first)
		{
			//set m_last to reflect empty queue
			m_last = NULL;
		}
	}
	pthread_mutex_unlock(&m_lock);
	return ret;
}

Evidence * EvidenceQueue::PopAll()
{
	pthread_mutex_lock(&m_lock);
	Evidence * ret = (Evidence *)m_first;
	if(ret)
	{
		m_first = NULL;
		m_last = NULL;
	}
	pthread_mutex_unlock(&m_lock);
	return ret;
}

//Returns true if this is the first piece of Evidence
bool EvidenceQueue::Push(Evidence * evidence)
{
	pthread_mutex_lock(&m_lock);
	//If m_last != NULL
	if(m_last)
	{
		//Link the next node against the previous
		m_last->m_next = evidence;
		//m_last == last pushed evidence
		m_last = evidence;
		pthread_mutex_unlock(&m_lock);
		return false;
	}
	//If this is the only piece of evidence, set the front (m_last == NULL on empty list)
	else
	{
		//m_last == last pushed evidence
		m_last = evidence;
		m_first = evidence;
		pthread_mutex_unlock(&m_lock);
		return true;
	}
}

}

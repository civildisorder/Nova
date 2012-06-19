//============================================================================
// Name        :
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
// Description :
//============================================================================

#include "NodeManager.h"
#include "Logger.h"
#include "NovaGuiTypes.h"

using namespace std;

namespace Nova
{

NodeManager::NodeManager(PersonalityTree *persTree)
{
	if(persTree != NULL)
	{
		m_persTree = persTree;
		m_hdconfig = m_persTree->GetHDConfig();
	}
}

bool NodeManager::SetPersonalityTree(PersonalityTree *persTree)
{
	if(persTree == NULL)
	{
		return false;
	}
	m_persTree = persTree;
	m_hdconfig = m_persTree->GetHDConfig();
	return true;
}

void NodeManager::GenerateProfileCounters()
{
	PersonalityNode *rootNode = m_persTree->GetRootNode();
	m_hostCount = rootNode->m_count;
	RecursiveGenProfileCounter(rootNode);
}

void NodeManager::RecursiveGenProfileCounter(PersonalityNode *parent)
{
	if(parent->m_children.empty())
	{
		struct ProfileCounter pCounter;

		if(m_hdconfig->GetProfile(parent->m_key) == NULL)
		{
			LOG(ERROR, "Couldn't retrieve expected NodeProfile: " + parent->m_key, "");
			return;
		}
		pCounter.m_profile = *m_hdconfig->GetProfile(parent->m_key);
		pCounter.m_maxCount = parent->m_count/m_hostCount;
		pCounter.m_minCount = 100 - pCounter.m_maxCount;
		for(unsigned int i = 0; i < parent->m_vendor_dist.size(); i++)
		{
			pCounter.m_macCounters.push_back(GenerateMacCounter(parent->m_vendor_dist[i].first, parent->m_vendor_dist[i].second));
		}
		for(unsigned int i = 0; i < parent->m_ports_dist.size(); i++)
		{
			pCounter.m_portCounters.push_back(GeneratePortCounter(parent->m_ports_dist[i].first, parent->m_ports_dist[i].second));
		}
	}
	for(uint i = 0; i < parent->m_children.size(); i++)
	{
		RecursiveGenProfileCounter(parent->m_children[i].second);
	}
}

MacCounter NodeManager::GenerateMacCounter(string vendor, double dist_val)
{
	struct MacCounter ret;
	if(m_hdconfig->GenerateUniqueMACAddress(vendor).compare(""))
	{
		ret.m_ethVendor = vendor;
	}
	ret.m_maxCount = dist_val;
	ret.m_minCount = 100 - dist_val;
	return ret;
}

PortCounter NodeManager::GeneratePortCounter(string portName, double dist_val)
{
	struct PortCounter ret;
	ret.m_port = portName;
	ret.m_maxCount = dist_val;
	ret.m_minCount = 100 - dist_val;
	return ret;
}

vector<Node> NodeManager::GenerateNodesFromProfile(NodeProfile *prof, int numNodes)
{
	vector<Node> retNodes;
	retNodes.clear();
	uint i = 0;
	while(m_profileCounters[i].m_profile.m_name.compare(prof->m_name) && (i < m_profileCounters.size()))
	{
		i++;
	}
	if(i < m_profileCounters.size())
	{
		ProfileCounter * curCounter = &m_profileCounters[i];
		NodeProfile * curProf = &curCounter->m_profile;
		for(int i = 0; i < numNodes; i++)
		{
			Node tempNode;
			tempNode.m_pfile = curProf->m_name;
		}
	}
	return retNodes;
}

}

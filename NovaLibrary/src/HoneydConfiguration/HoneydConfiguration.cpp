//============================================================================
// Name        : HoneydConfiguration.cpp
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
// Description : Object for reading and writing Honeyd XML configurations
//============================================================================

#include "HoneydConfiguration.h"
#include "../NovaUtil.h"
#include "../Logger.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include <arpa/inet.h>
#include <math.h>
#include <ctype.h>
#include <netdb.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <fstream>

using namespace std;
using namespace Nova;
using boost::property_tree::ptree;
using boost::property_tree::xml_parser::trim_whitespace;

namespace Nova
{

//Basic constructor for the Honeyd Configuration object
// Initializes the MAC vendor database and hash tables
// *Note: To populate the object from the file system you must call LoadAllTemplates();
HoneydConfiguration::HoneydConfiguration()
{
	m_macAddresses.LoadPrefixFile();
	m_homePath = Config::Inst()->GetPathHome();
	m_subnets.set_empty_key("");
	m_ports.set_empty_key("");
	m_nodes.set_empty_key("");
	m_profiles.set_empty_key("");
	m_scripts.set_empty_key("");
	m_subnets.set_deleted_key("Deleted");
	m_nodes.set_deleted_key("Deleted");
	m_profiles.set_deleted_key("Deleted");
	m_ports.set_deleted_key("Deleted");
	m_scripts.set_deleted_key("Deleted");
}

//This function sets the home directory from which the templates are relative to
//	homePath: file system path to the directory you wish to use
void HoneydConfiguration::SetHomePath(string homePath)
{
	m_homePath = homePath;
}

//This function returns the home path from which it is currently reading and writing from
// Returns: the local file system path in string form to the current home directory
string HoneydConfiguration::GetHomePath()
{
	return m_homePath;
}

//Attempts to populate the HoneydConfiguration object with the xml templates.
// The configuration is saved and loaded relative to the homepath specificed by the Nova Configuration
// Returns true if successful, false if loading failed.
bool HoneydConfiguration::LoadAllTemplates()
{
	m_scripts.clear_no_resize();
	m_ports.clear_no_resize();
	m_profiles.clear_no_resize();
	m_nodes.clear_no_resize();
	m_subnets.clear_no_resize();

	if(!LoadScriptsTemplate())
	{
		LOG(ERROR, "Unable to load Script templates!", "");
		return false;
	}
	if(!LoadPortsTemplate())
	{
		LOG(ERROR, "Unable to load Port templates!", "");
		return false;
	}
	if(!LoadProfilesTemplate())
	{
		LOG(ERROR, "Unable to load NodeProfile templates!", "");
		return false;
	}
	if(!LoadNodesTemplate())
	{
		LOG(ERROR, "Unable to load Nodes templates!", "");
		return false;
	}
	if(!LoadNodeKeys())
	{
		LOG(ERROR, "Unable to load Node Keys!", "");
		return false;
	}
	return true;
}

/************************************************
  Save Honeyd XML Configuration Functions
 ************************************************/

//This function takes the current values in the HoneydConfiguration and Config objects
// 		and translates them into an xml format for persistent storage that can be
// 		loaded at a later time by any HoneydConfiguration object
// Returns true if successful and false if the save fails
bool HoneydConfiguration::SaveAllTemplates()
{
	using boost::property_tree::ptree;
	ptree propTree;

	//Scripts
	m_scriptTree.clear();
	for(ScriptTable::iterator it = m_scripts.begin(); it != m_scripts.end(); it++)
	{
		propTree = it->second.m_tree;
		propTree.put<string>("name", it->second.m_name);
		propTree.put<string>("service", it->second.m_service);
		propTree.put<string>("osclass", it->second.m_osclass);
		propTree.put<string>("path", it->second.m_path);
		m_scriptTree.add_child("scripts.script", propTree);
	}

	//Ports
	m_portTree.clear();
	for(PortTable::iterator it = m_ports.begin(); it != m_ports.end(); it++)
	{
		//Put in required values
		propTree = it->second.m_tree;
		propTree.put<string>("name", it->second.m_portName);
		propTree.put<string>("number", it->second.m_portNum);
		propTree.put<string>("type", it->second.m_type);
		propTree.put<string>("service", it->second.m_service);
		propTree.put<string>("behavior", it->second.m_behavior);

		//If this port uses a script, save it.
		if(!it->second.m_behavior.compare("script") || !it->second.m_behavior.compare("internal"))
		{
			propTree.put<string>("script", it->second.m_scriptName);
		}

		//If the port works as a proxy, save destination
		else if(!it->second.m_behavior.compare("proxy"))
		{
			propTree.put<string>("IP", it->second.m_proxyIP);
			propTree.put<string>("Port", it->second.m_proxyPort);
		}
		//Add the child to the tree
		m_portTree.add_child("ports.port", propTree);
	}

	m_subnetTree.clear();
	for(SubnetTable::iterator it = m_subnets.begin(); it != m_subnets.end(); it++)
	{
		propTree = it->second.m_tree;
		propTree.put<string>("name", it->second.m_name);
		propTree.put<bool>("enabled",it->second.m_enabled);

		//Remove /## format mask from the address then put it in the XML.
		stringstream ss;
		ss << "/" << it->second.m_maskBits;
		int i = ss.str().size();
		string addrString = it->second.m_address.substr(0,(it->second.m_address.size()-i));
		propTree.put<string>("IP", addrString);

		//Gets the mask from mask bits then put it in XML
		in_addr_t rawBitMask = ::pow(2, 32-it->second.m_maskBits) - 1;

		//If maskBits is 24 then we have 2^8 -1 = 0x000000FF
		rawBitMask = ~rawBitMask; //After getting the inverse of this we have the mask in host addr form.

		//Convert to network order, put in in_addr struct
		//call ntoa to get char pointer and make string
		in_addr netOrderMask;
		netOrderMask.s_addr = htonl(rawBitMask);
		addrString = string(inet_ntoa(netOrderMask));
		propTree.put<string>("mask", addrString);
		m_subnetTree.add_child("manualInterface", propTree);
	}

	//Nodes
	m_nodesTree.clear();
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		propTree = it->second.m_tree;

		// No need to save names besides the doppel, we can derive them
		if(!it->second.m_name.compare("Doppelganger"))
		{
			// Make sure the IP reflects whatever is being used right now
			it->second.m_IP = Config::Inst()->GetDoppelIp();
			propTree.put<string>("name", it->second.m_name);
		}

		//Required xml entires
		propTree.put<string>("interface", it->second.m_interface);
		propTree.put<string>("IP", it->second.m_IP);
		propTree.put<bool>("enabled", it->second.m_enabled);
		propTree.put<string>("MAC", it->second.m_MAC);
		propTree.put<string>("profile.name", it->second.m_pfile);
		ptree newPortTree;
		newPortTree.clear();
		for(uint i = 0; i < it->second.m_ports.size(); i++)
		{
			if(!it->second.m_isPortInherited[i])
			{
				newPortTree.add<string>("port", it->second.m_ports[i]);
			}
		}
		propTree.put_child("profile.add.ports", newPortTree);
		m_nodesTree.add_child("node",propTree);
	}

	BOOST_FOREACH(ptree::value_type &value, m_groupTree.get_child("groups"))
	{
		//Find the specified group
		if(!value.second.get<string>("name").compare(Config::Inst()->GetGroup()))
		{
			//Load Subnets first, they are needed before we can load nodes
			value.second.put_child("subnets", m_subnetTree);
			value.second.put_child("nodes",m_nodesTree);
		}
	}
	m_profileTree.clear();
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		if(!it->second.m_parentProfile.compare(""))
		{
			propTree = it->second.m_tree;
			m_profileTree.add_child("profiles.profile", propTree);
		}
	}
	try
	{
		boost::property_tree::xml_writer_settings<char> settings('\t', 1);
		write_xml(m_homePath + "/scripts.xml", m_scriptTree, locale(), settings);
		write_xml(m_homePath + "/templates/ports.xml", m_portTree, locale(), settings);
		write_xml(m_homePath + "/templates/nodes.xml", m_groupTree, locale(), settings);
		write_xml(m_homePath + "/templates/profiles.xml", m_profileTree, locale(), settings);
	}
	catch(boost::property_tree::xml_parser_error &e)
	{
		LOG(ERROR, "Unable to right to xml files, caught except " + string(e.what()), "");
		return false;
	}

	LOG(DEBUG, "Honeyd templates have been saved" ,"");
	return true;
}

//Writes out the current HoneydConfiguration object to the Honeyd configuration file in the expected format
// path: path in the file system to the desired HoneydConfiguration file
// Returns true if successful and false if not
bool HoneydConfiguration::WriteHoneydConfiguration(string path)
{
	if(!path.compare(""))
	{
		if(!Config::Inst()->GetPathConfigHoneydHS().compare(""))
		{
			LOG(ERROR, "Invalid path given to Honeyd configuration file!", "");
			return false;
		}
		return WriteHoneydConfiguration(Config::Inst()->GetPathConfigHoneydHS());
	}

	LOG(DEBUG, "Writing honeyd configuration to " + path, "");

	stringstream out;
	vector<string> profilesParsed;

	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		if(!it->second.m_parentProfile.compare(""))
		{
			string pString = ProfileToString(&it->second);
			if(!pString.compare(""))
			{
				LOG(ERROR, "Unable to convert expected profile '" + it->first + "' into a valid Honeyd configuration string!", "");
				return false;
			}
			out << pString;
			profilesParsed.push_back(it->first);
		}
	}

	while(profilesParsed.size() < m_profiles.size())
	{
		for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
		{
			bool selfMatched = false;
			bool parentFound = false;
			for(uint i = 0; i < profilesParsed.size(); i++)
			{
				if(!it->second.m_parentProfile.compare(profilesParsed[i]))
				{
					parentFound = true;
					continue;
				}
				if(!it->first.compare(profilesParsed[i]))
				{
					selfMatched = true;
					break;
				}
			}

			if(!selfMatched && parentFound)
			{
				string pString = ProfileToString(&it->second);
				if(!pString.compare(""))
				{
					LOG(ERROR, "Unable to convert expected profile '" + it->first + "' into a valid Honeyd configuration string!", "");
					return false;
				}
				out << pString;
				profilesParsed.push_back(it->first);
			}
		}
	}

	// Start node section
	m_nodeProfileIndex = 0;
	for(NodeTable::iterator it = m_nodes.begin(); (it != m_nodes.end()) && (m_nodeProfileIndex < (uint)(~0)); it++)
	{
		m_nodeProfileIndex++;
		if(!it->second.m_enabled)
		{
			continue;
		}
		//We write the dopp regardless of whether or not it is enabled so that it can be toggled during runtime.
		else if(!it->second.m_name.compare("Doppelganger"))
		{
			string pString = DoppProfileToString(&m_profiles[it->second.m_pfile]);
			if(!pString.compare(""))
			{
				LOG(ERROR, "Unable to convert expected profile '" + it->second.m_pfile + "' into a valid Honeyd configuration string!", "");
				return false;
			}
			out << '\n' << pString;
			out << "bind " << it->second.m_IP << " DoppelgangerReservedTemplate" << '\n' << '\n';
			//Use configured or discovered loopback
		}
		else
		{
			string profName = HoneydConfiguration::SanitizeProfileName(it->second.m_pfile);

			//Clone a custom profile for a node
			out << "clone CustomNodeProfile-" << m_nodeProfileIndex << " " << profName << '\n';

			//Add any custom port settings
			for(uint i = 0; i < it->second.m_ports.size(); i++)
			{
				//Only write out ports that aren't inherited from parent profiles
				if(!it->second.m_isPortInherited[i])
				{
					Port prt = m_ports[it->second.m_ports[i]];
					//Skip past invalid port objects
					if(!(prt.m_type.compare("")) || !(prt.m_portNum.compare("")) || !(prt.m_behavior.compare("")))
					{
						continue;
					}

					out << "add CustomNodeProfile-" << m_nodeProfileIndex << " ";
					if(!prt.m_type.compare("TCP"))
					{
						out << " tcp port ";
					}
					else
					{
						out << " udp port ";
					}

					out << prt.m_portNum << " ";

					if(!(prt.m_behavior.compare("script")))
					{
						string scriptName = prt.m_scriptName;

						if(m_scripts[scriptName].m_path.compare(""))
						{
							out << '"' << m_scripts[scriptName].m_path << '"'<< '\n';
						}
						else
						{
							LOG(ERROR, "Error writing node port script.", "Path to script " + scriptName + " is null.");
						}
					}
					else
					{
						out << prt.m_behavior << '\n';
					}
				}
			}

			//If DHCP
			if(!it->second.m_IP.compare("DHCP"))
			{
				//Wrtie dhcp line
				out << "dhcp CustomNodeProfile-" << m_nodeProfileIndex << " on " << it->second.m_interface;
				//If the node has a MAC address (not random generated)
				if(it->second.m_MAC.compare("RANDOM"))
				{
					out << " ethernet \"" << it->second.m_MAC << "\"";
				}
				out << '\n';
			}
			//If static IP
			else
			{
				//If the node has a MAC address (not random generated)
				if(it->second.m_MAC.compare("RANDOM"))
				{
					//Set the MAC for the custom node profile
					out << "set " << "CustomNodeProfile-" << m_nodeProfileIndex;
					out << " ethernet \"" << it->second.m_MAC << "\"" << '\n';
				}
				//bind the node to the IP address
				out << "bind " << it->second.m_IP << " CustomNodeProfile-" << m_nodeProfileIndex << '\n';
			}
		}
	}
	ofstream outFile(path);
	outFile << out.str() << '\n';
	outFile.close();
	return true;
}

// This function takes in the raw byte form of a network mask and converts it to the number of bits
// 	used when specifiying a subnet in the dots and slash notation. ie. 192.168.1.1/24
// 	mask: The raw numerical form of the netmask ie. 255.255.255.0 -> 0xFFFFFF00
// Returns an int equal to the number of bits that are 1 in the netmask, ie the example value for mask returns 24
int HoneydConfiguration::GetMaskBits(in_addr_t mask)
{
	mask = ~mask;
	int i = 32;
	while(mask != 0)
	{
		if((mask % 2) != 1)
		{
			LOG(ERROR, "Invalid mask passed in as a parameter!", "");
			return -1;
		}
		mask = mask/2;
		i--;
	}
	return i;
}

//Outputs the NodeProfile in a string formate suitable for use in the Honeyd configuration file.
// p: pointer to the profile you wish to create a Honeyd template for
// Returns a string for direct inserting into a honeyd configuration file or an empty string if it fails.
string HoneydConfiguration::ProfileToString(NodeProfile *p)
{
	if(p == NULL)
	{
		LOG(ERROR, "NULL NodeProfile passed as parameter!", "");
		return "";
	}

	stringstream out;

	//XXX This is just a temporary band-aid on a larger wound, we cannot allow whitespaces in profile names.
	string profName = HoneydConfiguration::SanitizeProfileName(p->m_name);
	string parentProfName = HoneydConfiguration::SanitizeProfileName(p->m_parentProfile);

	if(!parentProfName.compare("default") || !parentProfName.compare(""))
	{
		out << "create " << profName << '\n';
	}
	else
	{
		out << "clone " << profName << " " << parentProfName << '\n';
	}

	out << "set " << profName  << " default tcp action " << p->m_tcpAction << '\n';
	out << "set " << profName  << " default udp action " << p->m_udpAction << '\n';
	out << "set " << profName  << " default icmp action " << p->m_icmpAction << '\n';

	if(p->m_personality.compare(""))
	{
		out << "set " << profName << " personality \"" << p->m_personality << '"' << '\n';
	}

	string vendor = "";
	double maxDist = 0;
	for(uint i = 0; i < p->m_ethernetVendors.size(); i++)
	{
		if(p->m_ethernetVendors[i].second > maxDist)
		{
			maxDist = p->m_ethernetVendors[i].second;
			vendor = p->m_ethernetVendors[i].first;
		}
	}
	if(vendor.compare(""))
	{
		out << "set " << profName << " ethernet \"" << vendor << '"' << '\n';
	}

	if(p->m_dropRate.compare(""))
	{
		out << "set " << profName << " droprate in " << p->m_dropRate << '\n';
	}

	out << '\n';
	return out.str();
}

//Outputs the NodeProfile in a string formate suitable for use in the Honeyd configuration file.
// p: pointer to the profile you wish to create a Honeyd template for
// Returns a string for direct inserting into a honeyd configuration file or an empty string if it fails.
// *Note: This function differs from ProfileToString in that it omits values incompatible with the loopback
//  interface and is used strictly for the Doppelganger node
string HoneydConfiguration::DoppProfileToString(NodeProfile *p)
{
	if(p == NULL)
	{
		LOG(ERROR, "NULL NodeProfile passed as parameter!", "");
		return "";
	}

	stringstream out;
	out << "create DoppelgangerReservedTemplate" << '\n';

	out << "set DoppelgangerReservedTemplate default tcp action " << p->m_tcpAction << '\n';
	out << "set DoppelgangerReservedTemplate default udp action " << p->m_udpAction << '\n';
	out << "set DoppelgangerReservedTemplate default icmp action " << p->m_icmpAction << '\n';

	if(p->m_personality.compare(""))
	{
		out << "set DoppelgangerReservedTemplate" << " personality \"" << p->m_personality << '"' << '\n';
	}

	if(p->m_dropRate.compare(""))
	{
		out << "set DoppelgangerReservedTemplate" << " droprate in " << p->m_dropRate << '\n';
	}

	for (uint i = 0; i < p->m_ports.size(); i++)
	{
		// Only include non-inherited ports
		if(!p->m_ports[i].second.first)
		{
			Port *portPtr = &m_ports[p->m_ports[i].first];
			if(portPtr == NULL)
			{
				LOG(ERROR, "Unable to retrieve expected port '" + p->m_ports[i].first + "'!", "");
				return "";
			}
			out << "add DoppelgangerReservedTemplate";
			if(!portPtr->m_type.compare("TCP"))
			{
				out << " tcp port ";
			}
			else
			{
				out << " udp port ";
			}
			out << portPtr->m_portNum << " ";
			if(!(portPtr->m_behavior.compare("script")))
			{
				string scriptName = m_ports[p->m_ports[i].first].m_scriptName;
				Script *scriptPtr = &m_scripts[scriptName];
				if(scriptPtr == NULL)
				{
					LOG(ERROR, "Unable to lookup script with name '" + scriptName + "'!", "");
					return "";
				}
				out << '"' << scriptPtr->m_path << '"'<< '\n';
			}
			else
			{
				out << portPtr->m_behavior << '\n';
			}
		}
	}
	out << '\n';
	return out.str();
}

//Adds a port with the specified configuration into the port table
//	portNum: Must be a valid port number (1-65535)
//	isTCP: if true the port uses TCP, if false it uses UDP
//	behavior: how this port treats incoming connections
//	scriptName: this parameter is only used if behavior == SCRIPT, in which case it designates
//		the key of the script it can lookup and execute for incoming connections on the port
//	Note(s): If CleanPorts is called before using this port in a profile, it will be deleted
//			If using a script it must exist in the script table before calling this function
//Returns: the port name if successful and an empty string if unsuccessful
string HoneydConfiguration::AddPort(uint16_t portNum, portProtocol isTCP, portBehavior behavior, string scriptName, string service)
{
	Port pr;

	//Check the validity and assign the port number
	if(!portNum)
	{
		LOG(ERROR, "Cannot create port: Port Number of 0 is Invalid.", "");
		return string("");
	}

	stringstream ss;
	ss << portNum;
	pr.m_portNum = ss.str();

	//Assign the port type (UDP or TCP)
	if(isTCP)
	{
		pr.m_type = "TCP";
	}
	else
	{
		pr.m_type = "UDP";
	}

	//Check and assign the port behavior
	switch(behavior)
	{
		case BLOCK:
		{
			pr.m_behavior = "block";
			break;
		}
		case OPEN:
		{
			pr.m_behavior = "open";
			break;
		}
		case RESET:
		{
			pr.m_behavior = "reset";
			break;
		}
		case SCRIPT:
		{
			//If the script does not exist
			if(m_scripts.find(scriptName) == m_scripts.end())
			{
				LOG(ERROR, "Cannot create port: specified script " + scriptName + " does not exist.", "");
				return "";
			}
			pr.m_behavior = "script";
			pr.m_scriptName = scriptName;
			break;
		}
		default:
		{
			LOG(ERROR, "Cannot create port: Attempting to use unknown port behavior", "");
			return string("");
		}
	}

	pr.m_service = service;

	//	Creates the ports unique identifier these names won't collide unless the port is the same
	if(!pr.m_behavior.compare("script"))
	{
		pr.m_portName = pr.m_portNum + "_" + pr.m_type + "_" + pr.m_scriptName;
	}
	else
	{
		pr.m_portName = pr.m_portNum + "_" + pr.m_type + "_" + pr.m_behavior;
	}

	//Checks if the port already exists
	if(m_ports.find(pr.m_portName) != m_ports.end())
	{
		LOG(WARNING, "Cannot create port: Specified port " + pr.m_portName + " already exists.", "");
		return pr.m_portName;
	}

	//Adds the port into the table
	m_ports[pr.m_portName] = pr;
	return pr.m_portName;
}

//This function inserts a pre-created port into the HoneydConfiguration object
//	pr: Port object you wish to add into the table
//	Returns a string containing the name of the port or an empty string if it fails
string HoneydConfiguration::AddPort(Port pr)
{
	if(m_ports.find(pr.m_portName) != m_ports.end())
	{
		return pr.m_portName;
	}
	m_ports[pr.m_portName] = pr;
	return pr.m_portName;
}

//This function creates a new Honeyd node based on the parameters given
//	profileName: name of the existing NodeProfile the node should use
//	ipAddress: string form of the IP address or the string "DHCP" if it should acquire an address using DHCP
//	macAddress: string form of a MAC address or the string "RANDOM" if one should be generated each time Honeyd is run
//	interface: the name of the physical or virtual interface the Honeyd node should be deployed on.
//	subnet: the name of the subnet object the node is associated with for organizational reasons.
//	Returns true if successful and false if not
bool HoneydConfiguration::AddNewNode(string profileName, string ipAddress, string macAddress, string interface, string subnet)
{
	Node newNode;
	uint macPrefix = m_macAddresses.AtoMACPrefix(macAddress);
	string vendor = m_macAddresses.LookupVendor(macPrefix);

	//Finish populating the node
	newNode.m_interface = interface;
	newNode.m_pfile = profileName;
	newNode.m_enabled = true;

	//Check the IP  and MAC address
	if(ipAddress.compare("DHCP"))
	{
		//Lookup the mac vendor to assert a valid mac
		if(!m_macAddresses.IsVendorValid(vendor))
		{
			LOG(WARNING, "Invalid MAC string '" + macAddress + "' given!", "");
		}

		uint retVal = inet_addr(ipAddress.c_str());
		if(retVal == INADDR_NONE)
		{
			LOG(ERROR, "Invalid node IP address '" + ipAddress + "' given!", "");
			return false;
		}
		newNode.m_realIP = htonl(retVal);
	}
	else if(!m_macAddresses.IsVendorValid(vendor))
	{
		LOG(ERROR, "Unable to use DHCP without a valid MAC, unable to use given address '" + macAddress + "'.", "");
		return false;
	}
	else
	{
		newNode.m_sub = newNode.m_interface;
	}

	//Get the name after assigning the values
	newNode.m_MAC = macAddress;
	newNode.m_IP = ipAddress;
	newNode.m_name = newNode.m_IP + " - " + newNode.m_MAC;

	//Make sure we have a unique identifier
	uint j = ~0;
	stringstream ss;
	if(!newNode.m_name.compare("DHCP - RANDOM"))
	{
		uint i = 1;
		while((m_nodes.keyExists(newNode.m_name)) && (i < j))
		{
			i++;
			ss.str("");
			ss << "DHCP - RANDOM(" << i << ")";
			newNode.m_name = ss.str();
		}
	}
	if(m_nodes.keyExists(newNode.m_name))
	{
		LOG(ERROR, "Unable to generate valid identifier for new node!", "");
		return false;
	}

	//Check for a valid interface
	vector<string> interfaces = Config::Inst()->GetInterfaces();
	if(interfaces.empty())
	{
		LOG(ERROR, "No interfaces specified for node creation!", "");
		return false;
	}
	//Iterate over the interface list and try to find one.
	for(uint i = 0; i < interfaces.size(); i++)
	{
		if(!interfaces[i].compare(newNode.m_interface))
		{
			break;
		}
		else if((i + 1) == interfaces.size())
		{
			LOG(WARNING, "No interface '" + newNode.m_interface + "' detected! Using interface '" + interfaces[0] + "' instead.", "");
			newNode.m_interface = interfaces[0];
		}
	}

	//Check for a valid subnet
	if(m_subnets.keyExists(subnet))
	{
		newNode.m_sub = subnet;
	}
	else if(newNode.m_IP.compare("DHCP"))
	{
		newNode.m_sub = FindSubnet(newNode.m_realIP);
		if(!newNode.m_sub.compare(""))
		{
			LOG(ERROR, "Unable to find a valid subnet for new node with IP '" + ipAddress + "'.", "");
			return false;
		}
	}

	//Check validity of NodeProfile
	NodeProfile *p = &m_profiles[profileName];
	if(p == NULL)
	{
		LOG(ERROR, "Unable to find expected NodeProfile '" + profileName + "'.", "");
		return false;
	}

	//Assign Ports
	for(uint i = 0; i < p->m_ports.size(); i++)
	{
		uint randVal = rand() % 100;
		//If our random value allows us to use the NodeProfile's port
		if(p->m_ports[i].second.second > randVal )
		{
			newNode.m_ports.push_back(p->m_ports[i].first);
			newNode.m_isPortInherited.push_back(true);
		}
		else
		{
			//Look up the port
			Port *portPtr = &m_ports[p->m_ports[i].first];
			if(portPtr == NULL)
			{
				LOG(ERROR, "Unable to retrieve expected port '" + p->m_ports[i].first + "'!", "");
				return false;
			}

			string portName = portPtr->m_portNum + "_" + portPtr->m_type + "_";

			//Grab the default action for the profile depending on the type
			string action = p->m_udpAction;
			if(portPtr->m_type.compare("TCP"))
			{
				action = p->m_tcpAction;
			}

			//Validate or choose the default action for a closed port
			if((!action.compare("block")) || (!action.compare("open")) || (!action.compare("reset")))
			{
				portName.append(action);
			}
			else
			{
				portName.append("reset");
			}

			//If the port exists
			if(!m_ports.keyExists(portName))
			{
				LOG(ERROR, "No port '" + portName + "' exists in the HoneydConfiguration object!", "");
				return false;
			}

			//Push back the closed or default action port
			newNode.m_ports.push_back(portName);
			newNode.m_isPortInherited.push_back(false);
		}
	}

	//Check validity of subnet
	Subnet *subPtr = &m_subnets[newNode.m_sub];
	if(subPtr == NULL)
	{
		LOG(ERROR, "Unable to retrieve expected subnet '" + newNode.m_sub + "' for node '" + newNode.m_name + "'!", "");
		return false;
	}

	//Assign all the values
	subPtr->m_nodes.push_back(newNode.m_name);
	p->m_nodeKeys.push_back(newNode.m_name);
	m_nodes[newNode.m_name] = newNode;

	LOG(DEBUG, "Added new node '" + newNode.m_name + "'.", "");

	return true;
}

//This function adds a new node to the configuration based on the existing node.
// Note* this function does not perform robust validation and is used primarily by the NodeManager,
//	avoid using this otherwise
bool HoneydConfiguration::AddPreGeneratedNode(Node &newNode)
{
	if(m_nodes.keyExists(newNode.m_name))
	{
		LOG(WARNING, "Node with name '" + newNode.m_name + "' already exists!", "");
		return true;
	}

	Subnet *subPtr = &m_subnets[newNode.m_sub];
	if(subPtr == NULL)
	{
		LOG(ERROR, "Unable to locate expected subnet '" + newNode.m_sub + "'.", "");
		return false;
	}

	NodeProfile *profPtr = &m_profiles[newNode.m_pfile];
	if(profPtr == NULL)
	{
		LOG(ERROR, "Unable to locate expected profile '" + newNode.m_pfile + "'.", "");
		return false;
	}

	subPtr->m_nodes.push_back(newNode.m_name);
	profPtr->m_nodeKeys.push_back(newNode.m_name);
	m_nodes[newNode.m_name] = newNode;

	LOG(DEBUG, "Added new node '" + newNode.m_name + "'.", "");
	return true;
}

//This function allows us to add many nodes of the same type easily
// *Note this function is very limited, for configuring large numbers of nodes you should use the NodeManager
bool HoneydConfiguration::AddNewNodes(string profileName, string ipAddress, string interface, string subnet, int numberOfNodes)
{
	NodeProfile *profPtr = &m_profiles[profileName];
	if(profPtr == NULL)
	{
		LOG(ERROR, "Unable to find valid profile named '" + profileName + "' during node creation!", "");
		return false;
	}

	if(numberOfNodes <= 0)
	{
		LOG(ERROR, "Must create 1 or more nodes", "");
		return false;
	}

	//Choose most highly distributed mac vendor or RANDOM
	uint max = 0;
	string macAddressPass = "RANDOM";
	string macVendor = "";
	for(unsigned int i = 0; i < profPtr->m_ethernetVendors.size(); i++)
	{
		if(profPtr->m_ethernetVendors[i].second > max)
		{
			max = profPtr->m_ethernetVendors[i].second;
			macVendor = profPtr->m_ethernetVendors[i].first;
			macAddressPass = m_macAddresses.GenerateRandomMAC(macVendor);
		}
	}
	if(macVendor.compare("RANDOM") && !m_macAddresses.IsVendorValid(macVendor))
	{
		LOG(WARNING, "Unable to resolve profile MAC vendor '" + macVendor + "', using RANDOM instead.", "");
		macVendor = "RANDOM";
		macAddressPass = "RANDOM";
	}

	//Add nodes in the DHCP case
	if(!ipAddress.compare("DHCP"))
	{
		for(int i = 0; i < numberOfNodes; i++)
		{
			if(!AddNewNode(profileName, ipAddress, macAddressPass, interface, subnet))
			{
				LOG(ERROR, "Adding new nodes failed during node creation!", "");
				return false;
			}
		}
		return true;
	}

	//Check the starting ipaddress
	in_addr_t sAddr = inet_addr(ipAddress.c_str());
	if(sAddr == INADDR_NONE)
	{
		LOG(ERROR,"Invalid IP Address given!", "");
	}

	//Add nodes in the statically addressed case
	sAddr = ntohl(sAddr);
	//Removes un-init compiler warning given for in_addr currentAddr;
	in_addr currentAddr = *(in_addr *)&sAddr;

	for(int i = 0; i < numberOfNodes; i++)
	{
		currentAddr.s_addr = htonl(sAddr);
		if(!AddNewNode(profileName, string(inet_ntoa(currentAddr)), macAddressPass, interface, subnet))
		{
			LOG(ERROR, "Adding new nodes failed during node creation!", "");
			return false;
		}
		sAddr++;
	}
	return true;
}

//Just a basic function for added a subnet into the configuration, may not be needed anymore.
bool HoneydConfiguration::AddSubnet(const Subnet &add)
{
	if(m_subnets.find(add.m_name) != m_subnets.end())
	{
		return false;
	}
	else
	{
		m_subnets[add.m_name] = add;
		return true;
	}
}

//This function allows easy access to all children profiles of the parent
// Returns a vector of strings containing the names of the children profile
vector<string> HoneydConfiguration::GetProfileChildren(string parent)
{
	vector<string> childProfiles;
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		if(!it->second.m_parentProfile.compare(parent))
		{
			childProfiles.push_back(it->second.m_name);
		}
	}
	return childProfiles;
}

//This function allows easy access to all profiles
// Returns a vector of strings containing the names of all profiles
vector<string> HoneydConfiguration::GetProfileNames()
{
	vector<string> childProfiles;
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		childProfiles.push_back(it->first);
	}
	return childProfiles;
}

//This function allows easy access to all nodes
// Returns a vector of strings containing the names of all nodes
vector<string> HoneydConfiguration::GetNodeNames()
{
	vector<string> nodeNames;
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		nodeNames.push_back(it->second.m_name);
	}
	return nodeNames;
}

//This function allows easy access to all subnets
// Returns a vector of strings containing the names of all subnets
vector<string> HoneydConfiguration::GetSubnetNames()
{
	vector<string> subnetNames;
	for(SubnetTable::iterator it = m_subnets.begin(); it != m_subnets.end(); it++)
	{
		subnetNames.push_back(it->second.m_name);
	}
	return subnetNames;
}

//This function allows easy access to all scripts
// Returns a vector of strings containing the names of all scripts
vector<string> HoneydConfiguration::GetScriptNames()
{
	vector<string> scriptNames;
	for(ScriptTable::iterator it = m_scripts.begin(); it != m_scripts.end(); it++)
	{
		scriptNames.push_back(it->first);
	}
	return scriptNames;
}

//This function allows easy access to all generated profiles
// Returns a vector of strings containing the names of all generated profiles
// *Note: Used by auto configuration? may not be needed.
vector<string> HoneydConfiguration::GetGeneratedProfileNames()//XXX Needed?
{
	vector<string> childProfiles;
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		if(it->second.m_generated && !it->second.m_personality.empty() && !it->second.m_ethernetVendors.empty())
		{
			childProfiles.push_back(it->first);
		}
	}
	return childProfiles;
}

//This function allows easy access to debug strings of all generated profiles
// Returns a vector of strings containing debug outputs of all generated profiles
vector<string> HoneydConfiguration::GeneratedProfilesStrings()//XXX Needed?
{
	vector<string> returnVector;
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		if(it->second.m_generated)
		{
			stringstream currentProfileStream;
			currentProfileStream << "Name: " << it->second.m_name << "\n";
			currentProfileStream << "Personality: " << it->second.m_personality << "\n";
			for(uint i = 0; i < it->second.m_ethernetVendors.size(); i++)
			{
				currentProfileStream << "MAC Vendor:  " << it->second.m_ethernetVendors[i].first << " - " << it->second.m_ethernetVendors[i].second <<"% \n";
			}
			currentProfileStream << "Associated Nodes:\n";
			for(uint i = 0; i < it->second.m_nodeKeys.size(); i++)
			{
				currentProfileStream << "\t" << it->second.m_nodeKeys[i] << "\n";

				for(uint j = 0; j < m_nodes[it->second.m_nodeKeys[i]].m_ports.size(); j++)
				{
					currentProfileStream << "\t\t" << m_nodes[it->second.m_nodeKeys[i]].m_ports[j];
				}
			}
			returnVector.push_back(currentProfileStream.str());
		}
	}
	return returnVector;
}

//This function determines whether or not the given profile is empty
// targetProfileKey: The name of the profile being inherited
// Returns true, if valid parent and false if not
// *Note: Used by auto configuration? may not be needed.
bool HoneydConfiguration::CheckNotInheritingEmptyProfile(string targetProfileKey)
{
	if(m_profiles.keyExists(targetProfileKey))
	{
		return RecursiveCheckNotInheritingEmptyProfile(m_profiles[targetProfileKey]);
	}
	else
	{
		return false;
	}
}

//This function allows easy access to all auto-generated nodes.
// Returns a vector of node names for each node on a generated profile.
vector<string> HoneydConfiguration::GetGeneratedNodeNames()//XXX Needed?
{
	vector<string> childnodes;
	Config::Inst()->SetGroup("HaystackAutoConfig");
	LoadNodesTemplate();
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		if(m_profiles[it->second.m_pfile].m_generated)
		{
			childnodes.push_back(it->second.m_name);
		}
	}
	return childnodes;
}

//This function allows access to NodeProfile objects by their name
// profileName: the name or key of the NodeProfile
// Returns a pointer to the NodeProfile object or NULL if the key doesn't exist
NodeProfile *HoneydConfiguration::GetProfile(string profileName)
{
	if(m_profiles.keyExists(profileName))
	{
		return &m_profiles[profileName];
	}
	return NULL;
}

//This function allows access to Port objects by their name
// portName: the name or key of the Port
// Returns a pointer to the Port object or NULL if the key doesn't exist
Port *HoneydConfiguration::GetPort(string portName)
{
	if(m_ports.keyExists(portName))
	{
		return &m_ports[portName];
	}
	return NULL;
}

//This function allows the caller to find out if the given MAC string is taken by a node
// mac: the string representation of the MAC address
// Returns true if the MAC is in use and false if it is not.
// *Note this function may have poor performance when there are a large number of nodes
bool HoneydConfiguration::IsMACUsed(string mac)
{
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		if(!it->second.m_MAC.compare(mac))
		{
			return true;
		}
	}
	return false;
}

//This function allows the caller to find out if the given IP string is taken by a node
// ip: the string representation of the IP address
// Returns true if the IP is in use and false if it is not.
// *Note this function may have poor performance when there are a large number of nodes
bool HoneydConfiguration::IsIPUsed(string ip)
{
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		if(!it->second.m_IP.compare(ip) && it->second.m_name.compare(ip))
		{
			return true;
		}
	}
	return false;
}

//This function allows the caller to find out if the given profile is being used by a node
// profileName: the name or key of the profile
// Returns true if the profile is in use and false if it is not.
// *Note this function may have poor performance when there are a large number of nodes
// TODO - change this to check the m_nodeKeys vector in the NodeProfile objects to avoid table iteration
bool HoneydConfiguration::IsProfileUsed(string profileName)
{
	//Find out if any nodes use this profile
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		//if we find a node using this profile
		if(!it->second.m_pfile.compare(profileName))
		{
			return true;
		}
	}
	return false;
}

//This function generates a MAC address that is currently not in use by any other node
// vendor: string name of the MAC vendor from which to choose a MAC range from
// Returns a string representation of MAC address or an empty string if the vendor is not valid
string HoneydConfiguration::GenerateUniqueMACAddress(string vendor)
{
	if(!m_macAddresses.IsVendorValid(vendor))
	{
		LOG(ERROR, "Unable to generate MAC address because vendor '" + vendor + "' is not a valid key.", "");
		return "";
	}
	string macAddress = m_macAddresses.GenerateRandomMAC(vendor);
	while(IsMACUsed(macAddress))
	{
		macAddress = m_macAddresses.GenerateRandomMAC(vendor);
	}
	return macAddress;
}

//This is used when a profile is cloned, it allows us to copy a ptree and extract all children from it
// it is exactly the same as novagui's xml extraction functions except that it gets the ptree from the
// cloned profile and it asserts a profile's name is unique and changes the name if it isn't
bool HoneydConfiguration::LoadProfilesFromTree(string parent)
{
	using boost::property_tree::ptree;
	ptree *ptr, pt = m_profiles[parent].m_tree;
	try
	{
		BOOST_FOREACH(ptree::value_type &value, pt.get_child("profiles"))
		{
			//Generic profile, essentially a honeyd template
			if(!string(value.first.data()).compare("profile"))
			{
				NodeProfile prof = m_profiles[parent];
				//Root profile has no parent
				prof.m_parentProfile = parent;
				prof.m_tree = value.second;

				for(uint i = 0; i < INHERITED_MAX; i++)
				{
					prof.m_inherited[i] = true;
				}

				//Asserts the name is unique, if it is not it finds a unique name
				// up to the range of 2^32
				string profileStr = prof.m_name;
				stringstream ss;
				uint i = 0, j = 0;
				j = ~j; //2^32-1

				while((m_profiles.keyExists(prof.m_name)) && (i < j))
				{
					ss.str("");
					i++;
					ss << profileStr << "-" << i;
					prof.m_name = ss.str();
				}
				prof.m_tree.put<string>("name", prof.m_name);

				prof.m_ports.clear();

				try //Conditional: has "set" values
				{
					ptr = &value.second.get_child("set");
					//pass 'set' subset and pointer to this profile
					LoadProfileSettings(ptr, &prof);
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e) {};

				try //Conditional: has "add" values
				{
					ptr = &value.second.get_child("add");
					//pass 'add' subset and pointer to this profile
					LoadProfileServices(ptr, &prof);
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e) {};

				//Save the profile
				m_profiles[prof.m_name] = prof;
				UpdateProfile(prof.m_name);

				try //Conditional: has children profiles
				{
					ptr = &value.second.get_child("profiles");

					//start recurisive descent down profile tree with this profile as the root
					//pass subtree and pointer to parent
					LoadProfileChildren(prof.m_name);
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e) {};
			}

			//Honeyd's implementation of switching templates based on conditions
			else if(!string(value.first.data()).compare("dynamic"))
			{
				//TODO
			}
			else
			{
				LOG(ERROR, "Invalid XML Path "+string(value.first.data()), "");
			}
		}
		return true;
	}
	catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
	{
		LOG(ERROR, "Problem loading Profiles: "+string(e.what()), "");
		return false;
	}

	return false;
}

//Sets the configuration of 'set' values for profile that called it
bool HoneydConfiguration::LoadProfileSettings(ptree *propTree, NodeProfile *nodeProf)
{
	string valueKey;
	try
	{
		BOOST_FOREACH(ptree::value_type &value, propTree->get_child(""))
		{
			valueKey = "TCP";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_tcpAction = value.second.data();
				nodeProf->m_inherited[TCP_ACTION] = false;
				continue;
			}
			valueKey = "UDP";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_udpAction = value.second.data();
				nodeProf->m_inherited[UDP_ACTION] = false;
				continue;
			}
			valueKey = "ICMP";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_icmpAction = value.second.data();
				nodeProf->m_inherited[ICMP_ACTION] = false;
				continue;
			}
			valueKey = "personality";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_personality = value.second.data();
				nodeProf->m_inherited[PERSONALITY] = false;
				continue;
			}
			valueKey = "ethernet";
			if(!string(value.first.data()).compare(valueKey))
			{
				pair<string, double> ethPair;
				ethPair.first = value.second.get<string>("vendor");
				ethPair.second = value.second.get<double>("ethDistribution");
				//If we inherited ethernet vendors but have our own, clear the vector
				if(nodeProf->m_inherited[ETHERNET] == true)
				{
					nodeProf->m_ethernetVendors.clear();
				}
				nodeProf->m_ethernetVendors.push_back(ethPair);
				nodeProf->m_inherited[ETHERNET] = false;
				continue;
			}
			valueKey = "uptimeMin";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_uptimeMin = value.second.data();
				nodeProf->m_inherited[UPTIME] = false;
				continue;
			}
			valueKey = "uptimeMax";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_uptimeMax = value.second.data();
				nodeProf->m_inherited[UPTIME] = false;
				continue;
			}
			valueKey = "dropRate";
			if(!string(value.first.data()).compare(valueKey))
			{
				nodeProf->m_dropRate = value.second.data();
				nodeProf->m_inherited[DROP_RATE] = false;
				continue;
			}
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading profile set parameters: " + string(e.what()) + ".", "");
		return false;
	}

	return true;
}

//Adds specified ports and subsystems
// removes any previous port with same number and type to avoid conflicts
bool HoneydConfiguration::LoadProfileServices(ptree *propTree, NodeProfile *nodeProf)
{
	string valueKey;
	Port *port;

	try
	{
		for(uint i = 0; i < nodeProf->m_ports.size(); i++)
		{
			nodeProf->m_ports[i].second.first = true;
		}
		BOOST_FOREACH(ptree::value_type &value, propTree->get_child(""))
		{
			//Checks for ports
			valueKey = "port";
			if(!string(value.first.data()).compare(valueKey))
			{
				string portName = value.second.get<string>("portName");
				port = &m_ports[portName];
				if(port == NULL)
				{
					LOG(ERROR, "Invalid port '" + portName + "' in NodeProfile '" + nodeProf->m_name + "'.","");
					return false;
				}

				//Checks inherited ports for conflicts
				for(uint i = 0; i < nodeProf->m_ports.size(); i++)
				{
					//Erase inherited port if a conflict is found
					if(!port->m_portNum.compare(m_ports[nodeProf->m_ports[i].first].m_portNum) && !port->m_type.compare(m_ports[nodeProf->m_ports[i].first].m_type))
					{
						nodeProf->m_ports.erase(nodeProf->m_ports.begin() + i);
					}
				}
				//Add specified port
				pair<bool,double> insidePortPair;
				pair<string, pair<bool, double> > outsidePortPair;
				outsidePortPair.first = port->m_portName;
				insidePortPair.first = false;

				double tempVal = atof(value.second.get<string>("portDistribution").c_str());
				//If outside the range, set distribution to 0
				if((tempVal < 0) ||(tempVal > 100))
				{
					tempVal = 0;
				}
				insidePortPair.second = tempVal;
				outsidePortPair.second = insidePortPair;
				if(!nodeProf->m_ports.size())
				{
					nodeProf->m_ports.push_back(outsidePortPair);
				}
				else
				{
					uint i = 0;
					for(i = 0; i < nodeProf->m_ports.size(); i++)
					{
						Port *tempPort = &m_ports[nodeProf->m_ports[i].first];
						if((atoi(tempPort->m_portNum.c_str())) < (atoi(port->m_portNum.c_str())))
						{
							continue;
						}
						break;
					}
					if(i < nodeProf->m_ports.size())
					{
						nodeProf->m_ports.insert(nodeProf->m_ports.begin() + i, outsidePortPair);
					}
					else
					{
						nodeProf->m_ports.push_back(outsidePortPair);
					}
				}
			}
			//Checks for a subsystem
			valueKey = "subsystem";
			if(!string(value.first.data()).compare(valueKey))
			{
				 //TODO - implement subsystem handling
				continue;
			}
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading profile add parameters: " + string(e.what()) + ".", "");
		return false;
	}

	return true;
}

//Recursive descent down a profile tree, inherits parent, sets values and continues if not leaf.
bool HoneydConfiguration::LoadProfileChildren(string parentKey)
{
	ptree propTree = m_profiles[parentKey].m_tree;
	try
	{
		BOOST_FOREACH(ptree::value_type &value, propTree.get_child("profiles"))
		{
			ptree *childPropTree;

			//Inherits parent,
			NodeProfile nodeProf = m_profiles[parentKey];

			try
			{
				nodeProf.m_tree = value.second;
				nodeProf.m_parentProfile = parentKey;

				nodeProf.m_generated = value.second.get<bool>("generated");
				nodeProf.m_distribution = value.second.get<double>("distribution");

				//Gets name, initializes DHCP
				nodeProf.m_name = value.second.get<string>("name");
			}
			catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
			{
				// Can't get name, generated, or some needed field
				// Skip this profile
				LOG(ERROR, "Profile XML file contained invalid profile. Skipping", "");
				continue;
			};

			if(!nodeProf.m_name.compare(""))
			{
				LOG(ERROR, "Problem loading honeyd XML files.", "");
				continue;
			}

			for(uint i = 0; i < INHERITED_MAX; i++)
			{
				nodeProf.m_inherited[i] = true;
			}

			try
			{
				childPropTree = &value.second.get_child("set");
				LoadProfileSettings(childPropTree, &nodeProf);
			}
			catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
			{
			};

			try
			{
				childPropTree = &value.second.get_child("add");
				LoadProfileServices(childPropTree, &nodeProf);
			}
			catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
			{
			};

			//Saves the profile
			m_profiles[nodeProf.m_name] = nodeProf;
			try
			{
				LoadProfileChildren(nodeProf.m_name);
			}
			catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
			{
			};
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading sub profiles: " + string(e.what()) + ".", "");
		return false;
	}
	return true;
}

// ******************* Private Methods **************************

//Loads scripts from the xml template located relative to the currently set home path
// Returns true if successful, false if not.
bool HoneydConfiguration::LoadScriptsTemplate()
{
	using boost::property_tree::ptree;
	using boost::property_tree::xml_parser::trim_whitespace;
	m_scriptTree.clear();
	try
	{
		read_xml(m_homePath + "/scripts.xml", m_scriptTree, boost::property_tree::xml_parser::trim_whitespace);

		BOOST_FOREACH(ptree::value_type &value, m_scriptTree.get_child("scripts"))
		{
			Script script;
			script.m_tree = value.second;
			//Each script consists of a name and path to that script
			script.m_name = value.second.get<string>("name");

			if(!script.m_name.compare(""))
			{
				LOG(ERROR, "Unable to a valid script from the templates!", "");
				return false;
			}

			try
			{
				script.m_osclass = value.second.get<string>("osclass");
			}
			catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
			{
				LOG(DEBUG, "No OS class found for script '" + script.m_name + "'.", "");
			};
			try
			{
				script.m_service = value.second.get<string>("service");
			}
			catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
			{
				LOG(DEBUG, "No service found for script '" + script.m_name + "'.", "");
			};
			script.m_path = value.second.get<string>("path");
			m_scripts[script.m_name] = script;
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading scripts: " + string(e.what()) + ".", "");
		return false;
	}
	return true;
}

//Loads Ports from the xml template located relative to the currently set home path
// Returns true if successful, false if not.
bool HoneydConfiguration::LoadPortsTemplate()
{
	using boost::property_tree::ptree;
	using boost::property_tree::xml_parser::trim_whitespace;

	m_portTree.clear();
	try
	{
		read_xml(m_homePath + "/templates/ports.xml", m_portTree, boost::property_tree::xml_parser::trim_whitespace);

		BOOST_FOREACH(ptree::value_type &value, m_portTree.get_child("ports"))
		{
			Port port;
			port.m_tree = value.second;
			//Required xml entries
			port.m_portName = value.second.get<string>("name");

			if(!port.m_portName.compare(""))
			{
				LOG(ERROR, "Unable to parse valid port name from xml file!", "");
				return false;
			}

			port.m_portNum = value.second.get<string>("number");
			if(!port.m_portNum.compare(""))
			{
				LOG(ERROR, "Unable to parse valid port number from xml file!", "");
				return false;
			}

			port.m_type = value.second.get<string>("type");
			if(!port.m_type.compare(""))
			{
				LOG(ERROR, "Unable to parse valid port type from xml file!", "");
				return false;
			}

			port.m_service = value.second.get<string>("service");

			port.m_behavior = value.second.get<string>("behavior");
			if(!port.m_behavior.compare(""))
			{
				LOG(ERROR, "Unable to parse valid port behavior from xml file!", "");
				return false;
			}

			//If this port uses a script, find and assign it.
			if(!port.m_behavior.compare("script") || !port.m_behavior.compare("internal"))
			{
				port.m_scriptName = value.second.get<string>("script");
			}
			//If the port works as a proxy, find destination
			else if(!port.m_behavior.compare("proxy"))
			{
				port.m_proxyIP = value.second.get<string>("IP");
				port.m_proxyPort = value.second.get<string>("Port");
			}
			m_ports[port.m_portName] = port;
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading ports: " + string(e.what()) + ".", "");
		return false;
	}
	return true;
}

//Loads NodeProfiles from the xml template located relative to the currently set home path
// Returns true if successful, false if not.
bool HoneydConfiguration::LoadProfilesTemplate()
{
	using boost::property_tree::ptree;
	using boost::property_tree::xml_parser::trim_whitespace;
	ptree *propTree;
	m_profileTree.clear();
	try
	{
		read_xml(m_homePath + "/templates/profiles.xml", m_profileTree, boost::property_tree::xml_parser::trim_whitespace);

		BOOST_FOREACH(ptree::value_type &value, m_profileTree.get_child("profiles"))
		{
			//Generic profile, essentially a honeyd template
			if(!string(value.first.data()).compare("profile"))
			{
				NodeProfile nodeProf;
				try
				{
					//Root profile has no parent
					nodeProf.m_parentProfile = "";
					nodeProf.m_tree = value.second;
					nodeProf.m_generated = value.second.get<bool>("generated");
					nodeProf.m_distribution = value.second.get<double>("distribution");
					nodeProf.m_name = value.second.get<string>("name");
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
				{
					LOG(ERROR, "Unable to parse required values for the NodeProfiles!", "");
					return false;
				};

				if(!nodeProf.m_name.compare(""))
				{
					LOG(ERROR, "Profile XML file contained invalid profile name!", "");
					return false;
				}

				nodeProf.m_ports.clear();
				for(uint i = 0; i < INHERITED_MAX; i++)
				{
					nodeProf.m_inherited[i] = false;
				}

				//Conditional: if has "set" values
				try
				{
					propTree = &value.second.get_child("set");
					if(!LoadProfileSettings(propTree, &nodeProf))
					{
						LOG(ERROR, "Unable to load profile settings subtree from xml template!", "");
						return false;
					}
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
				{
					LOG(DEBUG, "No profile settings found for NodeProfile '" + nodeProf.m_name + "'.", "");
				};

				//Conditional: if has "add" values
				try
				{
					propTree = &value.second.get_child("add");
					if(!LoadProfileServices(propTree, &nodeProf))
					{
						LOG(ERROR, "Unable to load profile settings subtree from xml template!", "");
						return false;
					}
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
				{
					LOG(DEBUG, "No profile ports or services found for NodeProfile '" + nodeProf.m_name + "'.", "");
				};

				//Save the profile
				m_profiles[nodeProf.m_name] = nodeProf;

				if(!LoadProfileChildren(nodeProf.m_name))
				{
					LOG(DEBUG, "Unable to load any children for the NodeProfile '" + nodeProf.m_name + "'.", "");
				}
			}
			else
			{
				LOG(ERROR, "Invalid XML Path " + string(value.first.data()) + ".", "");
			}
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading Profiles: " + string(e.what()) + ".", "");
		return false;
	}
	return true;
}

//Loads Nodes from the xml template located relative to the currently set home path
// Returns true if successful, false if not.
bool HoneydConfiguration::LoadNodesTemplate()
{
	using boost::property_tree::ptree;
	using boost::property_tree::xml_parser::trim_whitespace;

	m_groupTree.clear();
	ptree propTree;

	try
	{
		read_xml(m_homePath + "/templates/nodes.xml", m_groupTree, boost::property_tree::xml_parser::trim_whitespace);
		m_groups.clear();
		BOOST_FOREACH(ptree::value_type &value, m_groupTree.get_child("groups"))
		{
			m_groups.push_back(value.second.get<string>("name"));

			//Find the specified group
			if(!value.second.get<string>("name").compare(Config::Inst()->GetGroup()))
			{
				try
				{
					//Load Subnets first, they are needed before we can load nodes
					m_subnetTree = value.second.get_child("subnets");
					if(!LoadSubnets(&m_subnetTree))
					{
						LOG(ERROR, "Unable to load subnets from xml templates!", "");
						return false;
					}

					try
					{
						//If subnets are loaded successfully, load nodes
						m_nodesTree = value.second.get_child("nodes");
						if(!LoadNodes(&m_nodesTree))
						{
							LOG(ERROR, "Unable to load nodes from xml templates!", "");
							return false;
						}
					}
					catch(Nova::hashMapException &e)
					{
						LOG(ERROR, "Problem loading nodes: " + string(e.what()) + ".", "");
						return false;
					}
				}
				catch(Nova::hashMapException &e)
				{
					LOG(ERROR, "Problem loading subnets: " + string(e.what()) + ".", "");
					return false;
				}
			}
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading groups: " + Config::Inst()->GetGroup() + " - " + string(e.what()) + ".", "");
		return false;
	}
	return true;
}

//Iterates of the node table and populates the NodeProfiles with accessor keys to the node objects that use them.
// Returns true if successful and false if it is unable to assocate a profile with an exisiting node.
bool HoneydConfiguration::LoadNodeKeys()
{
	if(m_nodes.begin() == m_nodes.end())
	{
		LOG(WARNING, "Unable to locate any nodes in the configuration object.", "");
	}
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		NodeProfile * p = &m_profiles[it->second.m_pfile];
		if(p == NULL)
		{
			LOG(ERROR, "Unable to locate node profile '" + it->second.m_pfile + "'!", "");
			return false;
		}
		p->m_nodeKeys.push_back(it->first);
	}
	return true;
}

//loads subnets from file for current group
bool HoneydConfiguration::LoadSubnets(ptree *propTree)
{
	// getifaddrs to get physical ifaces
	pair<bool, int> replace;
	replace.first = false;
	replace.second = 0;
    vector<Subnet> subnetsFound;

    subnetsFound = FindPhysicalInterfaces();

	try
	{
		BOOST_FOREACH(ptree::value_type &value, propTree->get_child(""))
		{
			//If real interface
			if(!string(value.first.data()).compare("manualInterface"))
			{
				Subnet sub;
				sub.m_tree = value.second;
				//sub.m_isRealDevice =  value.second.get<bool>("isReal");
				//Extract the data
				sub.m_name = value.second.get<string>("name");
				sub.m_address = value.second.get<string>("IP");
				sub.m_mask = value.second.get<string>("mask");
				sub.m_enabled = value.second.get<bool>("enabled");

				//Gets the IP address in uint32 form
				in_addr_t hostOrderAddr = ntohl(inet_addr(sub.m_address.c_str()));

				//Converting the mask to uint32 allows a simple bitwise AND to get the lowest IP in the subnet.
				in_addr_t hostOrderMask = ntohl(inet_addr(sub.m_mask.c_str()));
				sub.m_base = (hostOrderAddr & hostOrderMask);

				//Get the number of bits in the mask
				if((sub.m_maskBits = GetMaskBits(hostOrderMask)) == -1)
				{
					LOG(ERROR, "Invalid subnet mask '" + sub.m_mask + "' parsed from xml templates!", "");
					return false;
				}

				//Adding the binary inversion of the mask gets the highest usable IP
				sub.m_max = sub.m_base + ~hostOrderMask;
				stringstream ss;
				ss << sub.m_address << "/" << sub.m_maskBits;
				sub.m_address = ss.str();

				//Save subnet
				m_subnets[sub.m_name] = sub;
			}
			//If virtual honeyd subnet
			else if(!string(value.first.data()).compare("virtual"))
			{
			}
			else
			{
				LOG(ERROR, "Unexpected Entry in file: " + string(value.first.data()) + ".", "");
				return false;
			}
		}

		stringstream ss;

		for(SubnetTable::iterator it = m_subnets.begin(); it != m_subnets.end(); it++)
		{
			for(uint i = 0; i < subnetsFound.size(); i++)
			{
				if(subnetsFound[i].m_name.compare(it->first))
				{
					it->second.m_name = subnetsFound[i].m_name;
					it->second.m_mask = subnetsFound[i].m_mask;
					it->second.m_enabled = subnetsFound[i].m_enabled;
					it->second.m_base = subnetsFound[i].m_base;
					it->second.m_maskBits = subnetsFound[i].m_maskBits;
					it->second.m_max = subnetsFound[i].m_max;
					ss << subnetsFound[i].m_address << "/" << subnetsFound[i].m_maskBits;
					it->second.m_address = ss.str();
					ss.str("");
				}
			}
		}

		if(subnetsFound.size() > m_subnets.size())
		{
			for(uint i = 0; i < subnetsFound.size(); i++)
			{
				if(!m_subnets.keyExists(subnetsFound[i].m_name))
				{
					AddSubnet(subnetsFound[i]);
				}
			}
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading subnets: " + string(e.what()), "");
		return false;
	}
	return true;
}


//loads haystack nodes from file for current group
bool HoneydConfiguration::LoadNodes(ptree *propTree)
{
	NodeProfile nodeProf;
	//ptree *ptr2;
	try
	{
		BOOST_FOREACH(ptree::value_type &value, propTree->get_child(""))
		{
			if(!string(value.first.data()).compare("node"))
			{
				Node node;
				stringstream ss;
				uint j = 0;
				j = ~j; // 2^32-1

				node.m_tree = value.second;
				//Required xml entires
				node.m_interface = value.second.get<string>("interface");
				node.m_IP = value.second.get<string>("IP");
				node.m_enabled = value.second.get<bool>("enabled");
				node.m_pfile = value.second.get<string>("profile.name");

				if(!node.m_pfile.compare(""))
				{
					LOG(ERROR, "Problem loading honeyd XML files.", "");
					continue;
				}

				nodeProf = m_profiles[node.m_pfile];

				//Get mac if present									nodeProf->m_ports.erase(nodeProf->m_ports.begin() + i);

				try //Conditional: has "set" values
				{
					node.m_MAC = value.second.get<string>("MAC");
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
				{

				};

				try //Conditional: has "set" values
				{
					ptree nodePorts = value.second.get_child("profile.add");
					LoadProfileServices(&nodePorts, &nodeProf);
					for(uint i = 0; i < nodeProf.m_ports.size(); i++)
					{
						node.m_ports.push_back(nodeProf.m_ports[i].first);
						node.m_isPortInherited.push_back(false);
					}
					//Loads inherited ports and checks for inheritance conflicts
					if(m_profiles.keyExists(node.m_pfile))
					{
						vector <pair <string, pair <bool, double> > > profilePorts = m_profiles[node.m_pfile].m_ports;
						for(uint i = 0; i < profilePorts.size(); i++)
						{
							bool conflict = false;
							Port curPort = m_ports[profilePorts[i].first];
							for(uint j = 0; j < node.m_ports.size(); j++)
							{
								Port nodePort = m_ports[node.m_ports[j]];
								if(!(curPort.m_portNum.compare(nodePort.m_portNum))
									&& !(curPort.m_type.compare(nodePort.m_type)))
								{
									conflict = true;
									break;
								}
							}
							if(!conflict)
							{
								node.m_ports.push_back(profilePorts[i].first);
								node.m_isPortInherited.push_back(true);
							}
						}
					}
				}
				catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e) {};

				if(!node.m_IP.compare(Config::Inst()->GetDoppelIp()))
				{
					node.m_name = "Doppelganger";
					node.m_sub = node.m_interface;
					node.m_realIP = htonl(inet_addr(node.m_IP.c_str())); //convert ip to uint32
					//save the node in the table
					m_nodes[node.m_name] = node;

					//Put address of saved node in subnet's list of nodes.
					m_subnets[m_nodes[node.m_name].m_sub].m_nodes.push_back(node.m_name);
				}
				else
				{
					node.m_name = node.m_IP + " - " + node.m_MAC;

					if(!node.m_name.compare("DHCP - RANDOM"))
					{
						//Finds a unique identifier
						uint i = 1;
						while((m_nodes.keyExists(node.m_name)) && (i < j))
						{
							i++;
							ss.str("");
							ss << "DHCP - RANDOM(" << i << ")";
							node.m_name = ss.str();
						}
					}

					if(node.m_IP != "DHCP")
					{
						node.m_realIP = htonl(inet_addr(node.m_IP.c_str())); //convert ip to uint32
						node.m_sub = FindSubnet(node.m_realIP);
						//If we have a subnet and node is unique
						if(node.m_sub != "")
						{
							//Put address of saved node in subnet's list of nodes.
							m_subnets[node.m_sub].m_nodes.push_back(node.m_name);
						}
						//If no subnet found, can't use node unless it's doppelganger.
						else
						{
							LOG(ERROR, "Node at IP: " + node.m_IP + "is outside all valid subnet ranges.", "");
						}
					}
					else
					{
						node.m_sub = node.m_interface; //TODO virtual subnets will need to be handled when implemented
						// If no valid subnet/interface found
						if(!node.m_sub.compare(""))
						{
							LOG(ERROR, "DHCP Enabled Node with MAC: " + node.m_name + " is unable to resolve it's interface.","");
							continue;
						}

						//Put address of saved node in subnet's list of nodes.
						m_subnets[node.m_sub].m_nodes.push_back(node.m_name);
					}
				}

				if(!node.m_name.compare(""))
				{
					LOG(ERROR, "Problem loading honeyd XML files.", "");
					continue;
				}
				else
				{
					//save the node in the table
					m_nodes[node.m_name] = node;
				}
			}
			else
			{
				LOG(ERROR, "Unexpected Entry in file: " + string(value.first.data()), "");
			}
		}
	}
	catch(Nova::hashMapException &e)
	{
		LOG(ERROR, "Problem loading nodes: " + string(e.what()), "");
		return false;
	}

	return true;
}

string HoneydConfiguration::FindSubnet(in_addr_t ip)
{
	string subnet = "";
	int max = 0;
	//Check each subnet
	for(SubnetTable::iterator it = m_subnets.begin(); it != m_subnets.end(); it++)
	{
		//If node falls outside a subnets range skip it
		if((ip < it->second.m_base) || (ip > it->second.m_max))
		{
			continue;
		}
		//If this is the smallest range
		if(it->second.m_maskBits > max)
		{
			subnet = it->second.m_name;
		}
	}

	return subnet;
}

//Inserts the profile into the honeyd configuration
//	profile: pointer to the profile you wish to add
//	Returns (true) if the profile could be created, (false) if it cannot.
bool HoneydConfiguration::AddProfile(NodeProfile &targetProfile)
{
	if(!m_profiles.keyExists(targetProfile.m_name))
	{
		m_profiles[targetProfile.m_name] = targetProfile;
		if(!UpdateProfile(targetProfile.m_name))
		{
			LOG(ERROR, "Unable to update target profile '" + targetProfile.m_name + "'.", "");
			return false;
		}
		return true;
	}
	return false;
}

vector<string> HoneydConfiguration::GetGroups()
{
	return m_groups;
}


bool HoneydConfiguration::AddGroup(string groupName)
{
	using boost::property_tree::ptree;

	for(uint i = 0; i < m_groups.size(); i++)
	{
		if(!groupName.compare(m_groups[i]))
		{
			return false;
		}
	}
	m_groups.push_back(groupName);
	try
	{
		ptree newGroup, emptyTree;
		newGroup.clear();
		emptyTree.clear();
		newGroup.put<string>("name", groupName);
		newGroup.put_child("subnets", m_subnetTree);
		newGroup.put_child("nodes", emptyTree);
		m_groupTree.add_child("groups.group", newGroup);
	}
	catch(boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::property_tree::ptree_bad_path> > &e)
	{
		LOG(ERROR, "Problem adding group ports: " + string(e.what()) + ".", "");
		return false;
	}
	return true;
}

//This function simply changes a profiles old name to the given new name
// oldName: name of the profile you wish to rename
// newName: the new name you wish to give a profile
// *Note: This function stands alone, you simply need to call it to save all changes.
bool HoneydConfiguration::RenameProfile(string oldName, string newName)
{
	//If item text and profile name don't match, we need to update
	if(oldName.compare(newName) && (m_profiles.keyExists(oldName)) && !m_profiles.keyExists(newName))
	{
		//Set the profile to the correct name and put the profile in the table
		NodeProfile assign = m_profiles[oldName];
		assign.m_name = newName;
		m_profiles[newName] = assign;

		//Find all nodes who use this profile and update to the new one
		for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
		{
			if(!it->second.m_pfile.compare(oldName))
			{
				m_nodes[it->first].m_pfile = newName;
			}
		}

		//Change the parent of all children profiles
		for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
		{
			if(!it->second.m_parentProfile.compare(oldName))
			{
				it->second.m_parentProfile = newName;
			}
		}
		if(!UpdateProfile(newName))
		{
			LOG(ERROR, "Unable to update profile tree of profile '" + newName + "'.", "");
			return false;
		}
		m_profiles[oldName].m_nodeKeys.clear();
		if(!DeleteProfile(oldName))
		{
			LOG(ERROR, "Unable to delete profile '" + oldName + "'.", "");
			return false;
		}
		return true;
	}
	return false;
}

//Makes the profile named child inherit the profile named parent
// child: the name of the child profile
// parent: the name of the new parent profile
// Returns: (true) if successful, (false) if either name could not be found
bool HoneydConfiguration::InheritProfile(string targetProfileName, string parentProfileName)
{
	//If both profiles exists
	if(m_profiles.keyExists(targetProfileName) && m_profiles.keyExists(parentProfileName))
	{
		//Grab the old parents name
		string oldParentName = m_profiles[targetProfileName].m_parentProfile;

		//Set the childs parent to the new parent profile
		m_profiles[targetProfileName].m_parentProfile = parentProfileName;
		if(!UpdateProfile(targetProfileName))
		{
			LOG(ERROR, "Unable to update target profile '" + targetProfileName + "'.", "");
			return false;
		}
		//If the child has an old parent
		if(m_profiles.keyExists(oldParentName))
		{
			if(!UpdateProfile(oldParentName))
			{
				LOG(ERROR, "Unable to update previous parent profile '" + oldParentName + "'", "");
				return false;
			}
		}
		return true;
	}
	LOG(ERROR, "Unable to inherit profile '" + parentProfileName + "' on target profile '" + targetProfileName + "' because one or both profiles could not be found!", "");
	return false;
}

//Recreates the entire ptree structure by rebuilding from the root
// *Note: This function looks for the default profile and simply rebuilds the entire tree from there.
// Returns true if successful and false if the function fails for some reason.
bool HoneydConfiguration::UpdateAllProfiles()
{
	if(m_profiles.keyExists("default"))
	{
		if(!UpdateProfile("default"))
		{
			LOG(ERROR, "Updating all profiles has failed!", "");
			return false;
		}
		return true;
	}
	LOG(CRITICAL, "Unable to locate root default profile! You may need to reinstall Nova!", "");
	return false;
}

bool HoneydConfiguration::EnableNode(string nodeName)
{
	// Make sure the node exists
	if(!m_nodes.keyExists(nodeName))
	{
		LOG(ERROR, "There was an attempt to delete a honeyd node (name = " + nodeName + " that doesn't exist", "");
		return false;
	}

	m_nodes[nodeName].m_enabled = true;

	// Enable the subnet of this node
	m_subnets[m_nodes[nodeName].m_sub].m_enabled = true;

	return true;
}

bool HoneydConfiguration::DisableNode(string nodeName)
{
	// Make sure the node exists
	if(!m_nodes.keyExists(nodeName))
	{
		LOG(ERROR, string("There was an attempt to disable a honeyd node (name = ")
			+ nodeName + string(" that doesn't exist"), "");
		return false;
	}

	m_nodes[nodeName].m_enabled = false;
	return true;
}

bool HoneydConfiguration::DeleteNode(string nodeName)
{
	// We don't delete the doppelganger node, only edit it
	if(!nodeName.compare("Doppelganger"))
	{
		LOG(WARNING, "Unable to delete the Doppelganger node", "");
		return false;
	}

	// Make sure the node exists
	Node *nodePtr = NULL;
	try
	{
		nodePtr = &m_nodes[nodeName];
	}
	catch(hashMapException &e)
	{
		LOG(WARNING, "Unable to locate expected node '" + nodeName + "'.","");
		return false;
	}
	if(nodePtr == NULL)
	{
		LOG(WARNING, "Unable to locate expected node '" + nodeName + "'.","");
		return false;
	}

	// Make sure the subnet exists
	Subnet *subPtr = NULL;
	try
	{
		subPtr = &m_subnets[nodePtr->m_sub];
	}
	catch(hashMapException &e)
	{
		LOG(ERROR, "Unable to locate expected subnet '" + nodePtr->m_sub + "'.","");
		return false;
	}
	if(subPtr == NULL)
	{
		LOG(ERROR, "Unable to locate expected subnet '" +nodePtr->m_sub + "'.","");
		return false;
	}

	//Update the Subnet
	for(uint i = 0; i < subPtr->m_nodes.size(); i++)
	{
		if(!subPtr->m_nodes[i].compare(nodeName))
		{
			subPtr->m_nodes.erase(subPtr->m_nodes.begin() + i);
		}
	}

	// Make sure the profile exists
	NodeProfile *profPtr = NULL;
	try
	{
		profPtr = &m_profiles[nodePtr->m_pfile];
	}
	catch(hashMapException &e)
	{
		LOG(ERROR, "Unable to locate expected profile '" + nodePtr->m_pfile + "'.","");
		return false;
	}
	if(profPtr == NULL)
	{
		LOG(ERROR, "Unable to locate expected profile '" + nodePtr->m_pfile + "'.","");
		return false;
	}

	for(uint i = 0; i < profPtr->m_nodeKeys.size(); i++)
	{
		if(!profPtr->m_nodeKeys[i].compare(nodeName))
		{
			profPtr->m_nodeKeys.erase(profPtr->m_nodeKeys.begin() + i);
		}
	}
	//Delete the node
	m_nodes.erase(nodeName);
	return true;
}

Node *HoneydConfiguration::GetNode(string nodeName)
{
	// Make sure the node exists
	if(m_nodes.keyExists(nodeName))
	{
		return &m_nodes[nodeName];
	}
	return NULL;
}

string HoneydConfiguration::GetNodeSubnet(string nodeName)
{
	// Make sure the node exists
	if(m_nodes.find(nodeName) == m_nodes.end())
	{
		LOG(ERROR, string("There was an attempt to retrieve the subnet of a honeyd node (name = ")
			+ nodeName + string(" that doesn't exist"), "");
		return "";
	}
	return m_nodes[nodeName].m_sub;
}

void HoneydConfiguration::DisableProfileNodes(string profileName)
{
	for(NodeTable::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
	{
		if(!it->second.m_pfile.compare(profileName))
		{
			DisableNode(it->first);
		}
	}
}

//Checks for ports that aren't used and removes them from the table if so
void HoneydConfiguration::CleanPorts()
{
	vector<string> delList;
	bool found;
	for(PortTable::iterator it = m_ports.begin(); it != m_ports.end(); it++)
	{
		found = false;
		for(ProfileTable::iterator jt = m_profiles.begin(); (jt != m_profiles.end()) && !found; jt++)
		{
			for(uint i = 0; (i < jt->second.m_ports.size()) && !found; i++)
			{
				if(!jt->second.m_ports[i].first.compare(it->first))
				{
					found = true;
				}
			}
		}
		for(NodeTable::iterator jt = m_nodes.begin(); (jt != m_nodes.end()) && !found; jt++)
		{
			for(uint i = 0; (i < jt->second.m_ports.size()) && !found; i++)
			{
				if(!jt->second.m_ports[i].compare(it->first))
				{
					found = true;
				}
			}
		}
		if(!found)
		{
			delList.push_back(it->first);
		}
	}
	while(!delList.empty())
	{
		m_ports.erase(delList.back());
		delList.pop_back();
	}
}

ScriptTable &HoneydConfiguration::GetScriptTable()
{
	return m_scripts;
}

//Removes a profile and all associated nodes from the Honeyd configuration
//	profileName: name of the profile you wish to delete
//	originalCall: used internally to designate the recursion's base condition, can old be set with
//		private access. Behavior is undefined if the first DeleteProfile call has originalCall == false
// 	Returns: (true) if successful and (false) if the profile could be deleted properly
bool HoneydConfiguration::DeleteProfile(string profileName, bool originalCall)
{
	//First assert the profile exists
	if(!m_profiles.keyExists(profileName))
	{
		LOG(WARNING, "Attempted to delete profile that does not exist", "");
		return false;
	}

	//Recursive descent to find and call delete on any children of the profile
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		//If the profile at the iterator is a child of this profile
		if(!it->second.m_parentProfile.compare(profileName))
		{
			if(!DeleteProfile(it->first, false))
			{
				LOG(ERROR, "Expected child profile '" + it->first + "' deletion failed", "");
				return false;
			}
		}
	}
	NodeProfile *p = &m_profiles[profileName];
	//Remove any nodes from the configuration
	for(uint i = 0; i < p->m_nodeKeys.size(); i++)
	{
		if(!DeleteNode(p->m_nodeKeys[i]))
		{
			LOG(WARNING, "Expected child node '" + p->m_nodeKeys[i] + "' deletion failed, continue attempt at deletion.", "");
		}
	}
	string parentName = p->m_parentProfile;
	m_profiles.erase(profileName);
	if(originalCall)
	{
		if(!UpdateProfile(parentName))
		{
			LOG(CRITICAL, "Unable to update parent profile '" + parentName + "' after profile '" + profileName + "' was deleted!", "");
			return false;
		}
	}
	return true;
}

//Recreates the profile tree of ancestors, children or both
//	Note: This needs to be called after making changes to a profile to update the hierarchy
//	Returns (true) if successful and (false) if no profile with name 'profileName' exists
bool HoneydConfiguration::UpdateProfileTree(string profileName, recursiveDirection direction)
{
	//Assert the profile exists
	if(!m_profiles.keyExists(profileName))
	{
		LOG(ERROR, "Expected NodeProfile '" + profileName + "' not found in the profile table!", "");
		return false;
	}

	//Grab the profile
	NodeProfile &p = m_profiles[profileName];

	//Separate the enum into checkable booleans
	bool up = false, down = false;
	switch(direction)
	{
		case UP:
		{
			up = true;
			break;
		}
		case DOWN:
		{
			down = true;
			break;
		}
		case ALL:
		default:
		{
			up = true;
			down = true;
			break;
		}
	}

	//Save any changes to the profile
	if(!CreateProfileTree(profileName))
	{
		LOG(ERROR, "Unable to CreateProfileTree for profile named '" + profileName + "'!", "");
	}

	ptree pt;
	pt.clear();

	//Find and insert all children
	for(ProfileTable::iterator it = m_profiles.begin(); it != m_profiles.end(); it++)
	{
		//If child is found
		if(!it->second.m_parentProfile.compare(p.m_name))
		{
			//Update recursively downward if needed
			if(down && m_profiles.keyExists(it->first))
			{
				if(!UpdateProfileTree(it->first, DOWN))
				{
					LOG(ERROR, "Updating profile tree for child NodeProfile '" + it->second.m_name + "' failed!", "");
					return false;
				}
			}
			//Put the child in the parent's ptree
			pt.add_child("profile", m_profiles[it->first].m_tree);
		}
	}
	//Replace the previous portion of the profiles ptree with the newly generated trees of the children
	p.m_tree.put_child("profiles", pt);
	//If we need to continue recursion upward to the root and haven't hit the terminating case of no parent/root
	if(up && m_profiles.keyExists(p.m_parentProfile))
	{
		if(!UpdateProfileTree(p.m_parentProfile, UP))
		{
			LOG(ERROR, "Updating profile tree for parent NodeProfile '" + p.m_parentProfile + "' failed!", "");
			return false;
		}
	}
	return true;
}

//Creates a ptree for a profile from scratch using the values found in the table
//	name: the name of the profile you wish to create a new tree for
//	Note: this only creates a leaf-node profile tree, after this call it will have no children.
//		to put the children back into the tree and place the this new tree into the parent's hierarchy
//		you must first call UpdateProfileTree(name, ALL);
//	Returns (true) if successful and (false) if no profile with name 'profileName' exists
bool HoneydConfiguration::CreateProfileTree(string profileName)
{
	ptree temp;
	if(!m_profiles.keyExists(profileName))
	{
		return false;
	}
	NodeProfile &p = m_profiles[profileName];
	if(p.m_name.compare(""))
	{
		temp.put<string>("name", p.m_name);
	}

	temp.put<bool>("generated", p.m_generated);
	if(p.m_distribution < 0)
	{
		temp.put<double>("distribution", 0);
	}
	if(p.m_distribution > 100)
	{
		temp.put<double>("distribution", 100);
	}
	else
	{
		temp.put<double>("distribution", p.m_distribution);
	}

	if(p.m_tcpAction.compare("") && !p.m_inherited[TCP_ACTION])
	{
		temp.put<string>("set.TCP", p.m_tcpAction);
	}
	if(p.m_udpAction.compare("") && !p.m_inherited[UDP_ACTION])
	{
		temp.put<string>("set.UDP", p.m_udpAction);
	}
	if(p.m_icmpAction.compare("") && !p.m_inherited[ICMP_ACTION])
	{
		temp.put<string>("set.ICMP", p.m_icmpAction);
	}
	if(p.m_personality.compare("") && !p.m_inherited[PERSONALITY])
	{
		temp.put<string>("set.personality", p.m_personality);
	}
	if(!p.m_inherited[ETHERNET])
	{
		for(uint i = 0; i < p.m_ethernetVendors.size(); i++)
		{
			ptree ethTemp;
			ethTemp.put<string>("vendor", p.m_ethernetVendors[i].first);
			ethTemp.put<double>("ethDistribution", p.m_ethernetVendors[i].second);
			temp.add_child("set.ethernet", ethTemp);
		}
	}
	if(p.m_uptimeMin.compare("") && !p.m_inherited[UPTIME])
	{
		temp.put<string>("set.uptimeMin", p.m_uptimeMin);
	}
	if(p.m_uptimeMax.compare("") && !p.m_inherited[UPTIME])
	{
		temp.put<string>("set.uptimeMax", p.m_uptimeMax);
	}
	if(p.m_dropRate.compare("") && !p.m_inherited[DROP_RATE])
	{
		temp.put<string>("set.dropRate", p.m_dropRate);
	}

	ptree pt;
	pt.clear();
	//Populates the ports, if none are found create an empty field because it is expected.
	if(p.m_ports.size())
	{
		for(uint i = 0; i < p.m_ports.size(); i++)
		{
			//If the port isn't inherited
			if(!p.m_ports[i].second.first)
			{
				ptree ptemp;
				ptemp.clear();
				ptemp.add<string>("portName", p.m_ports[i].first);
				ptemp.add<double>("portDistribution", p.m_ports[i].second.second);
				temp.add_child("add.port", ptemp);
			}
		}
	}
	else
	{
		temp.put_child("add",pt);
	}
	//put empty ptree in profiles as well because it is expected, does not matter that it is the same
	// as the one in add.m_ports if profile has no ports, since both are empty.
	temp.put_child("profiles", pt);
	p.m_tree = temp;
	return true;
}

vector<Subnet> HoneydConfiguration::FindPhysicalInterfaces()
{
	vector<Subnet> ret;

	struct ifaddrs *devices = NULL;
	ifaddrs *curIf = NULL;
	char addrBuffer[NI_MAXHOST];
	char bitmaskBuffer[NI_MAXHOST];
	struct in_addr netOrderAddrStruct;
	struct in_addr netOrderBitmaskStruct;
	struct in_addr minAddrInRange;
	struct in_addr maxAddrInRange;
	uint32_t hostOrderAddr;
	uint32_t hostOrderBitmask;

	if(getifaddrs(&devices))
	{
		LOG(ERROR, "Ethernet Interface Auto-Detection failed", "");
	}

	for(curIf = devices; curIf != NULL; curIf = curIf->ifa_next)
	{
		// IF we've found a loopback address with an IPv4 address
		if((curIf->ifa_flags & IFF_LOOPBACK) && ((int)curIf->ifa_addr->sa_family == AF_INET))
		{
			Subnet newSubnet;
			// start processing it to generate the subnet for the interface.
			// interfaces.push_back(string(curIf->ifa_name));

			// Get the string representation of the interface's IP address,
			// and put it into the host character array.
			int socket = getnameinfo(curIf->ifa_addr, sizeof(sockaddr_in), addrBuffer, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

			if(socket != 0)
			{
				// If getnameinfo returned an error, stop processing for the
				// method, assign the proper errorCode, and return an empty
				// vector.
				LOG(ERROR, "Getting Name info of Interface IP failed", "");
			}

			// Do the same thing as the above, but for the netmask of the interface
			// as opposed to the IP address.
			socket = getnameinfo(curIf->ifa_netmask, sizeof(sockaddr_in), bitmaskBuffer, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

			if(socket != 0)
			{
				// If getnameinfo returned an error, stop processing for the
				// method, assign the proper errorCode, and return an empty
				// vector.
				LOG(ERROR, "Getting Name info of Interface Netmask failed", "");
			}
			// Convert the bitmask and host address character arrays to strings
			// for use later
			string bitmaskString = string(bitmaskBuffer);
			string addrString = string(addrBuffer);

			// Put the network ordered address values into the
			// address and bitmaks in_addr structs, and then
			// convert them to host longs for use in
			// determining how much hostspace is empty
			// on this interface's subnet.
			inet_aton(addrString.c_str(), &netOrderAddrStruct);
			inet_aton(bitmaskString.c_str(), &netOrderBitmaskStruct);
			hostOrderAddr = ntohl(netOrderAddrStruct.s_addr);
			hostOrderBitmask = ntohl(netOrderBitmaskStruct.s_addr);

			// Get the base address for the subnet
			uint32_t hostOrderMinAddrInRange = hostOrderBitmask & hostOrderAddr;
			minAddrInRange.s_addr = htonl(hostOrderMinAddrInRange);

			// and the max address
			uint32_t hostOrderMaxAddrInRange = ~(hostOrderBitmask) + hostOrderMinAddrInRange;
			maxAddrInRange.s_addr = htonl(hostOrderMaxAddrInRange);

			// Find out how many bits there are to work with in
			// the subnet (i.e. X.X.X.X/24? X.X.X.X/31?).
			uint32_t tempRawMask = ~hostOrderBitmask;
			int i = 32;

			while(tempRawMask != 0)
			{
				tempRawMask /= 2;
				i--;
			}

			// Populate the subnet struct for use in the SubnetTable of the HoneydConfiguration
			// object.
			newSubnet.m_address = string(inet_ntoa(minAddrInRange));
			newSubnet.m_mask = string(inet_ntoa(netOrderBitmaskStruct));
			newSubnet.m_maskBits = i;
			newSubnet.m_base = minAddrInRange.s_addr;
			newSubnet.m_max = maxAddrInRange.s_addr;
			newSubnet.m_name = string(curIf->ifa_name);
			newSubnet.m_enabled = (curIf->ifa_flags & IFF_UP);

			ret.push_back(newSubnet);
		}
		// If we've found an interface that has an IPv4 address and isn't a loopback
		if(!(curIf->ifa_flags & IFF_LOOPBACK) && ((int)curIf->ifa_addr->sa_family == AF_INET))
		{
			Subnet newSubnet;

			// Get the string representation of the interface's IP address,
			// and put it into the host character array.
			int s = getnameinfo(curIf->ifa_addr, sizeof(sockaddr_in), addrBuffer, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

			if(s != 0)
			{
				// If getnameinfo returned an error, stop processing for the
				// method, assign the proper errorCode, and return an empty
				// vector.
				LOG(ERROR, "Getting Name info of Interface IP failed", "");
			}

			// Do the same thing as the above, but for the netmask of the interface
			// as opposed to the IP address.
			s = getnameinfo(curIf->ifa_netmask, sizeof(sockaddr_in), bitmaskBuffer, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

			if(s != 0)
			{
				// If getnameinfo returned an error, stop processing for the
				// method, assign the proper errorCode, and return an empty
				// vector.
				LOG(ERROR, "Getting Name info of Interface Netmask failed", "");
			}
			// Convert the bitmask and host address character arrays to strings
			// for use later
			string bitmaskString = string(bitmaskBuffer);
			string addrString = string(addrBuffer);

			// Put the network ordered address values into the
			// address and bitmaks in_addr structs, and then
			// convert them to host longs for use in
			// determining how much hostspace is empty
			// on this interface's subnet.
			inet_aton(addrString.c_str(), &netOrderAddrStruct);
			inet_aton(bitmaskString.c_str(), &netOrderBitmaskStruct);
			hostOrderAddr = ntohl(netOrderAddrStruct.s_addr);
			hostOrderBitmask = ntohl(netOrderBitmaskStruct.s_addr);

			// Get the base address for the subnet
			uint32_t hostOrderMinAddrInRange = hostOrderBitmask & hostOrderAddr;
			minAddrInRange.s_addr = htonl(hostOrderMinAddrInRange);

			// and the max address
			uint32_t hostOrderMaxAddrInRange = ~(hostOrderBitmask) + hostOrderMinAddrInRange;
			maxAddrInRange.s_addr = htonl(hostOrderMaxAddrInRange);

			// Find out how many bits there are to work with in
			// the subnet (i.e. X.X.X.X/24? X.X.X.X/31?).
			uint32_t mask = ~hostOrderBitmask;
			int i = 32;

			while(mask != 0)
			{
				mask /= 2;
				i--;
			}

			newSubnet.m_address = string(inet_ntoa(minAddrInRange));
			newSubnet.m_mask = string(inet_ntoa(netOrderBitmaskStruct));
			newSubnet.m_maskBits = i;
			newSubnet.m_base = minAddrInRange.s_addr;
			newSubnet.m_max = maxAddrInRange.s_addr;
			newSubnet.m_name = string(curIf->ifa_name);
			newSubnet.m_enabled = (curIf->ifa_flags & IFF_UP);

			ret.push_back(newSubnet);
		}
	}

	return ret;
}

string HoneydConfiguration::SanitizeProfileName(std::string oldName)
{
	if (!oldName.compare("default") || !oldName.compare(""))
	{
		return oldName;
	}

	string newname = "pfile" + oldName;
	ReplaceString(newname, " ", "-");
	ReplaceString(newname, ",", "COMMA");
	ReplaceString(newname, ";", "COLON");
	ReplaceString(newname, "@", "AT");
	return newname;
}

//This internal function recurses upward to determine whether or not the given profile has a personality
// check: Reference to the profile to check
// Returns true if there is a personality defined, false if not
// *Note: Used by auto configuration? shouldn't be needed.
bool HoneydConfiguration::RecursiveCheckNotInheritingEmptyProfile(const NodeProfile& check)
{
	if(!check.m_parentProfile.compare("default"))
	{
		if(!check.m_personality.empty())
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else if(m_profiles.keyExists(check.m_parentProfile) && check.m_personality.empty() && check.m_inherited[PERSONALITY])
	{
		return RecursiveCheckNotInheritingEmptyProfile(m_profiles[check.m_parentProfile]);
	}
	else
	{
		return false;
	}
}
}

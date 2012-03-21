//============================================================================
// Name        : NovaCLI.cpp
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
// Description : Command line interface for Nova
//============================================================================

#include "NovaCLI.h"
#include "Connection.h"
#include "HaystackControl.h"
#include "NovadControl.h"
#include "StatusQueries.h"
#include "Logger.h"


#include <iostream>
#include <stdlib.h>
#include "boost/program_options.hpp"


namespace po = boost::program_options;
using namespace std;
using namespace Nova;
using namespace NovaCLI;


int main(int argc, const char *argv[])
{
	// Fail if no arguements
	if (argc < 2)
	{
		PrintUsage();
	}

	// We parse the input arguments here,
	// but refer to other functions to do any
	// actual work.

	// Listing suspect IP addresses
	if (!strcmp(argv[1], "list"))
	{
		if (argc < 3)
		{
			PrintUsage();
		}

		if (!strcmp(argv[2], "all"))
		{
			PrintSuspectList(SUSPECTLIST_ALL);
		}
		else if (!strcmp(argv[2], "hostile"))
		{
			PrintSuspectList(SUSPECTLIST_HOSTILE);
		}
		else if (!strcmp(argv[2], "benign"))
		{
			PrintSuspectList(SUSPECTLIST_BENIGN);
		}
		else
		{
			PrintUsage();
		}
	}

	// Checking status of components
	else if (!strcmp(argv[1], "status"))
	{
		if (argc < 3)
		{
			PrintUsage();
		}

		if (!strcmp(argv[2], "nova"))
		{
			StatusNovaWrapper();
		}
		else if (!strcmp(argv[2], "haystack"))
		{
			StatusHaystackWrapper();
		}
		else
		{
			PrintUsage();
		}
	}

	// Starting components
	else if (!strcmp(argv[1], "start"))
	{
		if (argc < 3)
		{
			PrintUsage();
		}

		if (!strcmp(argv[2], "nova"))
		{
			StartNovaWrapper();
		}
		else if (!strcmp(argv[2], "haystack"))
		{
			StartHaystackWrapper();
		}
		else
		{
			PrintUsage();
		}
	}

	// Stopping components
	else if (!strcmp(argv[1], "stop"))
	{
		if (argc < 3)
		{
			PrintUsage();
		}

		if (!strcmp(argv[2], "nova"))
		{
			StopNovaWrapper();
		}
		else if (!strcmp(argv[2], "haystack"))
		{
			StopHaystackWrapper();
		}
		else
		{
			PrintUsage();
		}
	}

	// Getting suspect information
	else if (!strcmp(argv[1], "get"))
	{
		if (argc < 3)
		{
			PrintUsage();
		}

		if (!strcmp(argv[2], "all"))
		{
			PrintAllSuspects();
		}
		else
		{
			// Some early error checking for the
			in_addr_t address;
			if (inet_pton(AF_INET, argv[2], &address) != 1)
			{
				cout << "Error: Unable to convert to IP address" << endl;
				exit(EXIT_FAILURE);
			}

			PrintSuspect(address);
		}
	}
	else
	{
		PrintUsage();
	}

	return EXIT_SUCCESS;
}

namespace NovaCLI
{

void PrintUsage()
{
	cout << "Usage:" << endl;
	cout << "    " << EXECUTABLE_NAME << " status nova|haystack" << endl;
	cout << "    " << EXECUTABLE_NAME << " start nova|haystack" << endl;
	cout << "    " << EXECUTABLE_NAME << " stop nova|haystack" << endl;
	cout << "    " << EXECUTABLE_NAME << " list all|hostile|benign" << endl;
	cout << "    " << EXECUTABLE_NAME << " get all" << endl;
	cout << "    " << EXECUTABLE_NAME << " get xxx.xxx.xxx.xxx" << endl;
	cout << endl;

	exit(EXIT_FAILURE);
}

void StatusNovaWrapper()
{
	if (!ConnectToNovad())
	{
		cout << "Novad Status: Not running" << endl;
	}
	else
	{
		if (IsUp())
		{
			cout << "Novad Status: Running and responding" << endl;
		}
		else
		{
			cout << "Novad Status: Not responding" << endl;
		}
	}
}

void StatusHaystackWrapper()
{
	if (IsHaystackUp())
	{
		cout << "Haystack Status: Running" << endl;
	}
	else
	{
		cout << "Haystack Status: Not running" << endl;
	}
}

void StartNovaWrapper()
{
	if (!ConnectToNovad())
	{
		if (StartNovad())
		{
			cout << "Started Novad" << endl;
		}
		else
		{
			cout << "Failed to start Novad" << endl;
		}
	}
	else
	{
		cout << "Novad is already running" << endl;
		CloseNovadConnection();
	}
}

void StartHaystackWrapper()
{
	if (!IsHaystackUp())
	{
		if (StartHaystack())
		{
			cout << "Started Haystack" << endl;
		}
		else
		{
			cout << "Failed to start Haystack" << endl;
		}
	}
	else
	{
		cout << "Haystack is already running" << endl;
	}
}

void StopNovaWrapper()
{
	Connect();

	if(StopNovad())
	{
		cout << "Novad has been stopped" << endl;
	}
	else
	{
		cout << "There was a problem stopping Novad" << endl;
	}
}

void StopHaystackWrapper()
{
	if (StopHaystack())
	{
		cout << "Haystack has been stopped" << endl;
	}
	else
	{
		cout << "There was a problem stopping the Haystack" << endl;
	}
}

void PrintSuspect(in_addr_t address)
{
	Connect();

	Suspect *suspect = GetSuspect(address);

	if (suspect != NULL)
	{
		cout << "Suspect:" << endl;
		cout << suspect->ToString() << endl;
	}
	else
	{
		cout << "Error: No suspect received" << endl;
	}

	CloseNovadConnection();
}

void PrintAllSuspects()
{
	Connect();

	vector<in_addr_t> *suspects;
	suspects = GetSuspectList(SUSPECTLIST_ALL);

	if (suspects == NULL)
	{
		cout << "Failed to get suspect list" << endl;
		exit(EXIT_FAILURE);
	}

	for (uint i = 0; i < suspects->size(); i++)
	{
		Suspect *suspect = GetSuspect(suspects->at(i));

		if (suspect != NULL)
		{
			cout << "Suspect:" << endl;
			cout << suspect->ToString() << endl;
		}
		else
		{
			cout << "Error: No suspect received" << endl;
		}

		delete suspect;

	}

	CloseNovadConnection();

}

void PrintSuspectList(enum SuspectListType listType)
{
	Connect();

	vector<in_addr_t> *suspects;
	suspects = GetSuspectList(listType);


	if (suspects == NULL)
	{
		cout << "Failed to get suspect list" << endl;
		exit(EXIT_FAILURE);
	}

	for (uint i = 0; i < suspects->size(); i++)
	{
		in_addr tmp;
		tmp.s_addr = suspects->at(i);
		char *address = inet_ntoa(tmp);
		cout << address << endl;
	}

	CloseNovadConnection();
}

void Connect()
{
	if (!ConnectToNovad())
	{
		cout << "ERROR: Unable to connect to Nova" << endl;
		exit(EXIT_FAILURE);
	}
}

}
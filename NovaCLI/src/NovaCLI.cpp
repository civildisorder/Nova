#include "Connection.h"
#include "StatusQueries.h"

#include "messages/RequestMessage.h"

#include "boost/program_options.hpp"
#include <iostream>


namespace po = boost::program_options;
using namespace std;
using namespace Nova;

int main(int argc, const char *argv[])
{
	if (!ConnectToNovad())
	{
		cout << "Failed to connect to Nova" << endl;
		return EXIT_FAILURE;
	}

	if (!strcmp(argv[1], "list"))
	{
		vector<in_addr_t> *suspects;
		if (!strcmp(argv[2], "all"))
		{
			suspects = GetSuspectList(SUSPECTLIST_ALL);
		}
		else if (!strcmp(argv[2], "hostile"))
		{
			suspects = GetSuspectList(SUSPECTLIST_HOSTILE);
		}
		else if (!strcmp(argv[2], "benign"))
		{
			suspects = GetSuspectList(SUSPECTLIST_BENIGN);
		}


		if (suspects == NULL)
		{
			cout << "Failed to get suspect list" << endl;
			return EXIT_FAILURE;
		}

		for (uint i = 0; i < suspects->size(); i++)
		{
			in_addr tmp;
			tmp.s_addr = suspects->at(i);
			char *address = inet_ntoa(tmp);
			cout << address << endl;
		}
	}

	CloseNovadConnection();

	return EXIT_SUCCESS;
}

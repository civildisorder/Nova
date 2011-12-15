//============================================================================
// Name        : ClassificationEngine.cpp
// Author      : DataSoft Corporation
// Copyright   : GNU GPL v3
// Description : Classifies suspects as either hostile or benign then takes appropriate action
//============================================================================

#include "ClassificationEngine.h"
#include <TrafficEvent.h>
#include <errno.h>
#include <fstream>
#include "Point.h"
#include <arpa/inet.h>
#include <GUIMsg.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <net/if.h>
#include <sys/un.h>
#include <log4cxx/xml/domconfigurator.h>

using namespace log4cxx;
using namespace log4cxx::xml;
using namespace std;
using namespace Nova;
using namespace ClassificationEngine;


static SuspectHashTable suspects;

pthread_rwlock_t lock;

//NOT normalized
vector <Point*> dataPtsWithClass;
bool isTraining = false;
bool useTerminals;
static string hostAddrString;
static struct sockaddr_in hostAddr;

//Global variables related to Classification

//Global memory assignments to improve performance of IPC loops

//** Main (ReceiveTrafficEvent) **
struct sockaddr_un remote;
struct sockaddr* remoteSockAddr = (struct sockaddr *)&remote;
int bytesRead;
int connectionSocket;
u_char buffer[MAX_MSG_SIZE];
TrafficEvent *tempEvent, *event;
int IPCsock;

//** Silent Alarm **
struct sockaddr_un alarmRemote;
struct sockaddr* alarmRemotePtr =(struct sockaddr *)&alarmRemote;
struct sockaddr_in serv_addr;
struct sockaddr* serv_addrPtr = (struct sockaddr *)&serv_addr;
int len;

u_char data[MAX_MSG_SIZE];
uint plaintextLen;

int numBytesRead;
int socketFD;
int sockfd, broadcast = 1;
int * bdPtr = &broadcast;
int bdsize = sizeof broadcast;
char alarmBuf[MAX_MSG_SIZE];


//** ReceiveGUICommand **
int GUISocket;

//** SendToUI **
struct sockaddr_un GUISendRemote;
struct sockaddr* GUISendPtr = (struct sockaddr *)&GUISendRemote;
int GUISendSocket;
int GUILen;
u_char GUIData[MAX_MSG_SIZE];
uint GUIDataLen;


//Universal Socket variables (constants that can be re-used)
int socketSize = sizeof(remote);
int inSocketSize = sizeof(serv_addr);
socklen_t * sockSizePtr = (socklen_t*)&socketSize;




//Configured in NOVAConfig_CE or specified config file.
string broadcastAddr;			//Silent Alarm destination IP address
int sAlarmPort;					//Silent Alarm destination port
int classificationTimeout;		//In seconds, how long to wait between classifications
int maxFeatureVal;				//The value to normalize feature values to.
									//Higher value makes for more precision, but more cycles.

int k;							//number of nearest neighbors
double eps;						//error bound
int maxPts;						//maximum number of data points
double classificationThreshold = .5; //value of classification to define split between hostile / benign
//End configured variables
const int dim = DIM;					//dimension

int nPts = 0;						//actual number of data points
ANNpointArray dataPts;				//data points
ANNpointArray normalizedDataPts;	//normalized data points
ANNkd_tree*	kdTree;					// search structure
string dataFile;					//input for data points
istream* queryIn = NULL;			//input for query points

const char *outFile;				//output for data points during training

char * pathsFile = (char*)"/etc/nova/paths";
string homePath;

void *outPtr;

LoggerPtr m_logger(Logger::getLogger("main"));

int maxFeatureValues[dim];

int main(int argc,char *argv[])
{
	bzero(GUIData,MAX_MSG_SIZE);
	bzero(data,MAX_MSG_SIZE);
	bzero(buffer, MAX_MSG_SIZE);
	pthread_rwlock_init(&lock, NULL);

	suspects.set_empty_key(NULL);
	suspects.resize(INITIAL_TABLESIZE);

	//Initialize the aes_key struct
	AES_set_encrypt_key(SAkey,256,&aes_key);
	AES_set_decrypt_key(SAkey,256,&aes_key);

	int len;
	struct sockaddr_un localIPCAddress;

	pthread_t classificationLoopThread;
	pthread_t trainingLoopThread;
	pthread_t silentAlarmListenThread;
	pthread_t GUIListenThread;

	string novaConfig, logConfig;
	string line, prefix; //used for input checking

	//Get locations of nova files
	ifstream *paths =  new ifstream(pathsFile);

	if(paths->is_open())
	{
		while(paths->good())
		{
			getline(*paths,line);

			prefix = "NOVA_HOME";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				homePath = line;
				break;
			}
		}
	}
	paths->close();
	delete paths;
	paths = NULL;

	//Resolves environment variables
	int start = 0;
	int end = 0;
	string var;

	while((start = homePath.find("$",end)) != -1)
	{
		end = homePath.find("/", start);
		//If no path after environment var
		if(end == -1)
		{

			var = homePath.substr(start+1, homePath.size());
			var = getenv(var.c_str());
			homePath = homePath.substr(0,start) + var;
		}
		else
		{
			var = homePath.substr(start+1, end-1);
			var = getenv(var.c_str());
			var = var + homePath.substr(end, homePath.size());
			if(start > 0)
			{
				homePath = homePath.substr(0,start)+var;
			}
			else
			{
				homePath = var;
			}
		}
	}

	if(homePath == "")
	{
		exit(1);
	}

	novaConfig = homePath + "/Config/NOVAConfig.txt";
	logConfig = homePath + "/Config/Log4cxxConfig_Console.xml";

	DOMConfigurator::configure(logConfig.c_str());

	//Runs the configuration loader
	LoadConfig((char*)novaConfig.c_str());

	if(!useTerminals)
	{
		logConfig = homePath +"/Config/Log4cxxConfig.xml";
		DOMConfigurator::configure(logConfig.c_str());
	}

	dataFile = homePath + "/" +dataFile;
	outFile = dataFile.c_str();
	outPtr = (void *)outFile;

	pthread_create(&GUIListenThread, NULL, GUILoop, NULL);
	//Are we Training or Classifying?
	if(isTraining)
	{
		pthread_create(&trainingLoopThread,NULL,TrainingLoop,(void *)outFile);
	}
	else
	{
		LoadDataPointsFromFile(dataFile);

		pthread_create(&classificationLoopThread,NULL,ClassificationLoop, NULL);
		pthread_create(&silentAlarmListenThread,NULL,SilentAlarmLoop, NULL);
	}

	if((IPCsock = socket(AF_UNIX,SOCK_STREAM,0)) == -1)
	{
		LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		close(IPCsock);
		exit(1);
	}

	localIPCAddress.sun_family = AF_UNIX;

	//Builds the key path
	string key = KEY_FILENAME;
	key = homePath + key;

	strcpy(localIPCAddress.sun_path, key.c_str());
	unlink(localIPCAddress.sun_path);
	len = strlen(localIPCAddress.sun_path) + sizeof(localIPCAddress.sun_family);

    if(bind(IPCsock,(struct sockaddr *)&localIPCAddress,len) == -1)
    {
    	LOG4CXX_ERROR(m_logger, "bind: " << strerror(errno));
    	close(IPCsock);
        exit(1);
    }

    if(listen(IPCsock, SOCKET_QUEUE_SIZE) == -1)
    {
    	LOG4CXX_ERROR(m_logger, "listen: " << strerror(errno));
		close(IPCsock);
        exit(1);
    }

	//"Main Loop"
	while(true)
	{
		event = new TrafficEvent();
		if( ReceiveTrafficEvent() == false)
		{
			delete event;
			event = NULL;
			continue;
		}
		pthread_rwlock_wrlock(&lock);
		//If this is a new Suspect
		if(suspects.count(event->src_IP.s_addr) == 0)
		{
			suspects[event->src_IP.s_addr] = new Suspect(event);
		}

		//A returning suspect
		else
		{
			suspects[event->src_IP.s_addr]->AddEvidence(event);

		}
		pthread_rwlock_unlock(&lock);
	}

	//Shouldn't get here!
	LOG4CXX_ERROR(m_logger,"Main thread ended. Shouldn't get here!!!");
	close(IPCsock);
	return 1;
}

//Infinite loop that recieves messages from the GUI
void *Nova::ClassificationEngine::GUILoop(void *ptr)
{
	struct sockaddr_un GUIAddress;
	int len;

	if((GUISocket = socket(AF_UNIX,SOCK_STREAM,0)) == -1)
	{
		LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		close(GUISocket);
		exit(1);
	}

	GUIAddress.sun_family = AF_UNIX;

	//Builds the key path
	string key = GUI_FILENAME;
	key = homePath + key;

	strcpy(GUIAddress.sun_path, key.c_str());
	unlink(GUIAddress.sun_path);
	len = strlen(GUIAddress.sun_path) + sizeof(GUIAddress.sun_family);

	if(bind(GUISocket,(struct sockaddr *)&GUIAddress,len) == -1)
	{
		LOG4CXX_ERROR(m_logger, "bind: " << strerror(errno));
		close(GUISocket);
		exit(1);
	}

	if(listen(GUISocket, SOCKET_QUEUE_SIZE) == -1)
	{
		LOG4CXX_ERROR(m_logger, "listen: " << strerror(errno));
		close(GUISocket);
		exit(1);
	}
	while(true)
	{
		ReceiveGUICommand();
	}
}

//Separate thread which infinite loops, periodically updating all the classifications
//	for all the current suspects
void *Nova::ClassificationEngine::ClassificationLoop(void *ptr)
{
	int oldClassification;

	//Builds the GUI address
	string GUIKey = homePath + CE_FILENAME;
	GUISendRemote.sun_family = AF_UNIX;
	strcpy(GUISendRemote.sun_path, GUIKey.c_str());
	GUILen = strlen(GUISendRemote.sun_path) + sizeof(GUISendRemote.sun_family);

	//Builds the Silent Alarm IPC address
	string key = homePath + KEY_ALARM_FILENAME;
	alarmRemote.sun_family = AF_UNIX;
	strcpy(alarmRemote.sun_path, key.c_str());
	len = strlen(alarmRemote.sun_path) + sizeof(alarmRemote.sun_family);

	//Builds the Silent Alarm Network address
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(sAlarmPort);
	serv_addr.sin_addr.s_addr = INADDR_BROADCAST;

	//Classification Loop
	while(true)
	{
		sleep(classificationTimeout);
		pthread_rwlock_rdlock(&lock);
		//Calculate the "true" Feature Set for each Suspect
		for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
		{
			if(it->second->needs_feature_update)
			{
				pthread_rwlock_unlock(&lock);
				pthread_rwlock_wrlock(&lock);
				it->second->CalculateFeatures(isTraining);
				pthread_rwlock_unlock(&lock);
				pthread_rwlock_rdlock(&lock);
			}
		}
		//Calculate the normalized feature sets, actually used by ANN
		//	Writes into Suspect ANNPoints
		NormalizeDataPoints();
		//Perform classification on each suspect
		for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
		{
			if(it->second->needs_classification_update)
			{
				oldClassification = it->second->isHostile;

				Classify(it->second);
				cout << it->second->ToString();

				//If the hostility changed
				if( oldClassification != it->second->isHostile)
				{
					SilentAlarm(it->second);
				}

				SendToUI(it->second);
			}
		}
		pthread_rwlock_unlock(&lock);
	}
	//Shouldn't get here!
	LOG4CXX_ERROR(m_logger,"Main thread ended. Shouldn't get here!!!");
	return NULL;
}


//Thread for calculating training data, and writing to file.
void *Nova::ClassificationEngine::TrainingLoop(void *ptr)
{
	string GUIKey = homePath + CE_FILENAME;
	strcpy(GUISendRemote.sun_path, GUIKey.c_str());

	GUISendRemote.sun_family = AF_UNIX;
	GUILen = strlen(GUISendRemote.sun_path) + sizeof(GUISendRemote.sun_family);

	//Training Loop
	while(true)
	{
		sleep(classificationTimeout);
		ofstream myfile (string(outFile).data(), ios::app);
		if (myfile.is_open())
		{
			pthread_rwlock_wrlock(&lock);
			//Calculate the "true" Feature Set for each Suspect
			for (SuspectHashTable::iterator it = suspects.begin() ; it != suspects.end(); it++)
			{
				if(it->second->needs_feature_update)
				{
					it->second->CalculateFeatures(isTraining);
					if( it->second->annPoint == NULL)
					{
						it->second->annPoint = annAllocPt(DIMENSION);
					}
					for(int j=0; j < dim; j++)
					{
						it->second->annPoint[j] = it->second->features.features[j];
						myfile << it->second->annPoint[j] << " ";
					}
					it->second->needs_feature_update = false;
					myfile << it->second->classification;
					myfile << "\n";
					cout << it->second->ToString();
					SendToUI(it->second);
				}
			}
			pthread_rwlock_unlock(&lock);
		}
		else LOG4CXX_INFO(m_logger, "Unable to open file.");
		myfile.close();
	}
	//Shouldn't get here!
	LOG4CXX_ERROR(m_logger,"Training thread ended. Shouldn't get here!!!");
	return NULL;
}

//Thread for listening for Silent Alarms from other Nova instances
void *Nova::ClassificationEngine::SilentAlarmLoop(void *ptr)
{
	int sockfd;
	u_char buf[MAX_MSG_SIZE];
	struct sockaddr_in sendaddr;

	int numbytes;
	int addr_len;
	int broadcast=1;

	if((sockfd = socket(PF_INET,SOCK_DGRAM,0)) == -1)
	{
		LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		close(sockfd);
		exit(1);
	}

	if(setsockopt(sockfd,SOL_SOCKET,SO_BROADCAST,&broadcast,sizeof broadcast) == -1)
	{
		LOG4CXX_ERROR(m_logger, "setsockopt - SO_SOCKET: " << strerror(errno));
		close(sockfd);
		exit(1);
	}

	sendaddr.sin_family = AF_INET;
	sendaddr.sin_port = htons(sAlarmPort);
	sendaddr.sin_addr.s_addr = INADDR_ANY;
	memset(sendaddr.sin_zero,'\0', sizeof sendaddr.sin_zero);
	struct sockaddr* sockaddrPtr = (struct sockaddr*) &sendaddr;
	socklen_t sendaddrSize = sizeof sendaddr;

	if(bind(sockfd,sockaddrPtr,sendaddrSize) == -1)
	{
		LOG4CXX_ERROR(m_logger, "bind: " << strerror(errno));
		close(sockfd);
		exit(1);
	}

	addr_len = sizeof sendaddr;
	socklen_t * addr_lenPtr = (socklen_t *)&addr_len;
	size_t bufSize = sizeof buf;

	Suspect *suspect = NULL;

	while(1)
	{
		//Do the actual "listen"
		if ((numbytes = recvfrom(sockfd, buf, bufSize, 0,sockaddrPtr, addr_lenPtr)) == -1)
		{
			LOG4CXX_ERROR(m_logger, "recvfrom: " << strerror(errno));
			close(sockfd);
			exit(1);
		}

		//If this is from ourselves, then drop it.
		if(hostAddr.sin_addr.s_addr == sendaddr.sin_addr.s_addr)
		{
			continue;
		}
		//We expect that the ciphertext should be in blocks of AES_BLOCK_SIZE
		//	Also, we assume that there must be mroe than one block. (The IV is the first block)
		if( ((numbytes % AES_BLOCK_SIZE) != 0) && (numbytes > AES_BLOCK_SIZE))
		{
			continue;
		}

		unsigned char *plaintext = (unsigned char *)malloc(numbytes);

		//Decrypt
		AES_cbc_encrypt(buf + AES_BLOCK_SIZE, plaintext, numbytes, &aes_key, buf, 0);

		suspect = new Suspect();

		try
		{
			suspect->deserializeSuspect(plaintext);
			bzero(buf, numbytes);

			LOG4CXX_INFO(m_logger,"Received a Silent Alarm!\n" << suspect->ToString());
			pthread_rwlock_rdlock(&lock);

			for (SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++)
			{
				if(it->second->IP_address.s_addr == suspect->IP_address.s_addr)
				{
					pthread_rwlock_unlock(&lock);
					pthread_rwlock_wrlock(&lock);
					it->second->flaggedByAlarm = true;
					break;
				}
			}
		}
		catch(std::exception e)
		{
			close(sockfd);
			LOG4CXX_INFO(m_logger,"Error interpreting received Silent Alarm: " << string(e.what()));
		}
		free(plaintext);
		delete suspect;
		pthread_rwlock_unlock(&lock);
	}
	close(sockfd);
	LOG4CXX_INFO(m_logger,"Silent Alarm thread ended. Shouldn't get here!!!");
	return NULL;
}

//Forms the normalized kd tree, done once on start up
//will be called again if the a suspect's max value for a feature exceeds the current maximum
void Nova::ClassificationEngine::FormKdTree()
{
	delete kdTree;
	//Normalize the data points
	//Foreach data point
	for(int j = 0;j < dim;j++)
	{
		//Foreach feature within the data point
		for(int i=0;i < nPts;i++)
		{
			if(maxFeatureValues[j] != 0)
			{
				normalizedDataPts[i][j] = (double)((dataPts[i][j] / maxFeatureValues[j]));
			}
			else
			{
				LOG4CXX_INFO(m_logger,"Max Feature Value for feature " << (i+1) << " is 0!");
				break;
			}
		}
	}
	kdTree = new ANNkd_tree(					// build search structure
			normalizedDataPts,					// the data points
					nPts,						// number of points
					dim);						// dimension of space
}

//Performs classification on given suspect
//Where all the magic takes place
void Nova::ClassificationEngine::Classify(Suspect *suspect)
{
	ANNpoint			queryPt;				// query point
	ANNidxArray			nnIdx;					// near neighbor indices
	ANNdistArray		dists;					// near neighbor distances

	queryPt = suspect->annPoint;
	nnIdx = new ANNidx[k];						// allocate near neigh indices
	dists = new ANNdist[k];						// allocate near neighbor dists

	kdTree->annkSearch(							// search
			queryPt,							// query point
			k,									// number of near neighbors
			nnIdx,								// nearest neighbors (returned)
			dists,								// distance (returned)
			eps);								// error bound

	//Determine classification according to weight by distance
	//	.5 + E[(1-Dist) * Class] / 2k (Where Class is -1 or 1)
	//	This will make the classification range from 0 to 1
	double classifyCount = 0;
	for (int i = 0; i < k; i++)
	{
		dists[i] = sqrt(dists[i]);				// unsquare distance

		//If Hostile
		if( dataPtsWithClass[ nnIdx[i] ]->classification == 1)
		{
			classifyCount += (sqrtDIM - dists[i]);
		}
		//If benign
		else if( dataPtsWithClass[ nnIdx[i] ]->classification == 0)
		{
			classifyCount -= (sqrtDIM - dists[i]);
		}
		else
		{
			//error case; Data points must be 0 or 1
			LOG4CXX_ERROR(m_logger,"Data point: " << i << " has an invalid classification of: " <<
					dataPtsWithClass[ nnIdx[i] ]->classification << ". This must be either 0 (benign) or 1 (hostile).");
			suspect->classification = -1;
			delete [] nnIdx;							// clean things up
		    delete [] dists;
		    annClose();
		    return;
		}
	}
	pthread_rwlock_unlock(&lock);
	pthread_rwlock_wrlock(&lock);
	suspect->classification = .5 + (classifyCount / ((2.0 * (double)k) * sqrtDIM ));

	if( suspect->classification > classificationThreshold)
	{
		suspect->isHostile = true;
	}
	else
	{
		suspect->isHostile = false;
	}
	delete [] nnIdx;							// clean things up
    delete [] dists;

    annClose();
	suspect->needs_classification_update = false;
	pthread_rwlock_unlock(&lock);
	pthread_rwlock_rdlock(&lock);
}

//Subroutine to copy the data points in 'suspects' to their respective ANN Points
void Nova::ClassificationEngine::CopyDataToAnnPoints()
{
	//Set the ANN point for each Suspect
	for (SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++)
	{
		//Create the ANN Point, if needed

	}
}

//Calculates normalized data points for suspects
void Nova::ClassificationEngine::NormalizeDataPoints()
{
	//Used to indicate if a max value has changed and the kd tree needs to be reformed
	bool aMaxValChanged = false;

	//Find the max values for each feature
	for (SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++)
	{
		for(int i = 0;i < dim;i++)
		{
			if(it->second->features.features[i] > maxFeatureValues[i])
			{
				//For proper normalization the upper bound for a feature is the max value of the data.
				maxFeatureValues[i] = it->second->features.features[i];
				aMaxValChanged = true;
			}
		}
	}
	if(aMaxValChanged) FormKdTree();

	pthread_rwlock_unlock(&lock);
	pthread_rwlock_wrlock(&lock);
	//Normalize the suspect points
	for (SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++)
	{
		if(it->second->needs_feature_update)
		{

			if(it->second->annPoint == NULL)
			{
				it->second->annPoint = annAllocPt(DIMENSION);
			}

			//If the max is 0, then there's no need to normalize! (Plus it'd be a div by zero)
			for(int i = 0;i < dim;i++)
			{
				if(maxFeatureValues[0] != 0)
				{
					// We don't need a write lock, only this thread uses it.
					it->second->annPoint[i] = (double)(it->second->features.features[i] / maxFeatureValues[i]);
				}
				else
				{
					LOG4CXX_INFO(m_logger,"Max Feature Value for feature " << (i+1) << " is 0!");
				}
			}
			it->second->needs_feature_update = false;
		}
	}
	pthread_rwlock_unlock(&lock);
	pthread_rwlock_rdlock(&lock);
}

//Prints a single ANN point, p, to stream, out
void Nova::ClassificationEngine::printPt(ostream &out, ANNpoint p)
{
	out << "(" << p[0];
	for (int i = 1;i < dim;i++)
	{
		out << ", " << p[i];
	}
	out << ")\n";
}

//Reads into the list of suspects from a file specified by inFilePath
void Nova::ClassificationEngine::LoadDataPointsFromFile(string inFilePath)
{
	ifstream myfile (inFilePath.data());
	string line;
	int i = 0;
	//Count the number of data points for allocation
	if (myfile.is_open())
	{
		while (!myfile.eof())
		{
			if(myfile.peek() == EOF)
			{
				break;
			}
			getline(myfile,line);
			i++;
		}
	}
	else LOG4CXX_ERROR(m_logger, "Unable to open file.");
	myfile.close();
	maxPts = i;

	//Open the file again, allocate the number of points and assign
	myfile.open(inFilePath.data(), ifstream::in);
	dataPts = annAllocPts(maxPts, dim);			// allocate data points
	normalizedDataPts = annAllocPts(maxPts, dim);

	if (myfile.is_open())
	{
		i = 0;

		while (!myfile.eof() && (i < maxPts))
		{
			if(myfile.peek() == EOF)
			{
				break;
			}

			dataPtsWithClass.push_back(new Point());

			for(int j = 0;j < dim;j++)
			{
				getline(myfile,line,' ');
				double temp = strtod(line.data(), NULL);

				dataPtsWithClass[i]->annPoint[j] = temp;
				dataPts[i][j] = temp;

				//Set the max values of each feature. (Used later in normalization)
				if(temp > maxFeatureValues[j])
				{
					maxFeatureValues[j] = temp;
				}
			}
			getline(myfile,line);
			dataPtsWithClass[i]->classification = atoi(line.data());
			i++;
		}
		nPts = i;
	}
	else LOG4CXX_ERROR(m_logger,"Unable to open file.");
	myfile.close();

	//Normalize the data points
	//Foreach data point
	for(int j = 0;j < dim;j++)
	{
		//Foreach feature within the data point
		for(int i=0;i < nPts;i++)
		{
			if(maxFeatureValues[j] != 0)
			{
				normalizedDataPts[i][j] = (double)((dataPts[i][j] / maxFeatureValues[j]));
			}
			else
			{
				LOG4CXX_INFO(m_logger,"Max Feature Value for feature " << (i+1) << " is 0!");
				break;
			}
		}
	}

	kdTree = new ANNkd_tree(					// build search structure
			normalizedDataPts,					// the data points
					nPts,						// number of points
					dim);						// dimension of space
}

//Writes dataPtsWithClass out to a file specified by outFilePath
void Nova::ClassificationEngine::WriteDataPointsToFile(string outFilePath)
{

	ofstream myfile (outFilePath.data(), ios::app);

	if (myfile.is_open())
	{
		pthread_rwlock_rdlock(&lock);
		for ( SuspectHashTable::iterator it = suspects.begin();it != suspects.end();it++ )
		{
			for(int j=0; j < dim; j++)
			{
				myfile << it->second->annPoint[j] << " ";
			}
			myfile << it->second->classification;
			myfile << "\n";
		}
		pthread_rwlock_unlock(&lock);
	}
	else LOG4CXX_ERROR(m_logger, "Unable to open file.");
	myfile.close();

}

//Returns usage tips
string Nova::ClassificationEngine::Usage()
{
	string usageString = "Nova Classification Engine!\n";
	usageString += "\tUsage: ClassificationEngine -l LogConfigPath -n NOVAConfigPath \n";
	usageString += "\t-l: Path to LOG4CXX config xml file.\n";
	usageString += "\t-n: Path to NOVA config txt file.\n";
	return usageString;
}



//Returns a string representation of the specified device's IP address
string Nova::ClassificationEngine::getLocalIP(const char *dev)
{
	static struct ifreq ifreqs[20];
	struct ifconf ifconf;

	uint  nifaces, i;
	memset(&ifconf,0,sizeof(ifconf));

	ifconf.ifc_buf = (char*)(ifreqs);
	ifconf.ifc_len = sizeof(ifreqs);

	int sock, rval;
	sock = socket(AF_INET,SOCK_STREAM,0);

	if(sock < 0)
	{
		LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		close(sock);
		return NULL;
	}

	if((rval = ioctl(sock,SIOCGIFCONF,(char*)&ifconf)) < 0 )
	{
		LOG4CXX_ERROR(m_logger, "ioctl(SIOGIFCONF): " << strerror(errno));
	}

	close(sock);
	nifaces =  ifconf.ifc_len/sizeof(struct ifreq);

	for(i = 0;i < nifaces;i++)
	{
		if( strcmp(ifreqs[i].ifr_name, dev) == 0 )
		{
			char ip_addr [INET_ADDRSTRLEN];
			struct sockaddr_in *b = (struct sockaddr_in *) &(ifreqs[i].ifr_addr);

			inet_ntop(AF_INET,&(b->sin_addr),ip_addr,INET_ADDRSTRLEN);
			return string(ip_addr);
		}
	}
	return string("");
}

//Send a silent alarm about the argument suspect
void Nova::ClassificationEngine::SilentAlarm(Suspect *suspect)
{
	//IV + ciphertext
	unsigned char *ciphertext;
	plaintextLen = suspect->serializeSuspect(data);

	uint padding = AES_BLOCK_SIZE - (plaintextLen % AES_BLOCK_SIZE);
	if( padding == AES_BLOCK_SIZE)
	{
		padding  = 0;
	}

	uint ciphertextLen = AES_BLOCK_SIZE + plaintextLen + padding;

	ciphertext = (unsigned char*)malloc(AES_BLOCK_SIZE + plaintextLen + padding);

	//Put a random IV into the first AES block
	RAND_bytes(ciphertext, AES_BLOCK_SIZE);
	//Append the ciphertext to the rest
	AES_cbc_encrypt(data, ciphertext + AES_BLOCK_SIZE, plaintextLen + padding, &aes_key, ciphertext, 1);

	if ((socketFD = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		close(socketFD);
		free(ciphertext);
		return;
	}

	if (connect(socketFD, alarmRemotePtr, len) == -1)
	{
		LOG4CXX_ERROR(m_logger, "connect: " << strerror(errno));
		close(socketFD);
		free(ciphertext);
		return;
	}

	if (send(socketFD, ciphertext, ciphertextLen, 0) == -1)
	{
		LOG4CXX_ERROR(m_logger, "send: " << strerror(errno));
		close(socketFD);
		free(ciphertext);
		return;
	}
	close(socketFD);

	//Send Silent Alarm to other Nova Instances
	if ((sockfd = socket(AF_INET,SOCK_DGRAM,0)) == -1)
	{
		LOG4CXX_ERROR(m_logger, "socket: " << strerror(errno));
		close(sockfd);
		free(ciphertext);
		return;
	}

	if((setsockopt(sockfd, SOL_SOCKET,SO_BROADCAST, bdPtr, bdsize)) == -1)
	{
		LOG4CXX_ERROR(m_logger, "setsockopt - SO_SOCKET: " << strerror(errno));
		close(sockfd);
		free(ciphertext);
		return;
	}

	if( sendto(sockfd, ciphertext, ciphertextLen, 0, serv_addrPtr, inSocketSize) == -1)
	{
		LOG4CXX_ERROR(m_logger,"Error in UDP Send: " << strerror(errno));
		close(sockfd);
		free(ciphertext);
		return;
	}
	free(ciphertext);
}

///Receive a TrafficEvent from another local component.
/// This is a blocking function. If nothing is received, then wait on this thread for an answer
bool ClassificationEngine::ReceiveTrafficEvent()
{
    //Blocking call
    if ((connectionSocket = accept(IPCsock, remoteSockAddr, sockSizePtr)) == -1)
    {
		LOG4CXX_ERROR(m_logger,"accept: " << strerror(errno));
		close(connectionSocket);
        return false;
    }
    if((bytesRead = recv(connectionSocket, buffer, MAX_MSG_SIZE, 0 )) == -1)
    {
		LOG4CXX_ERROR(m_logger,"recv: " << strerror(errno));
		close(connectionSocket);
        return false;
    }
	try
	{
		event->deserializeEvent(buffer);
		bzero(buffer, bytesRead);
	}
	catch(std::exception e)
	{
		LOG4CXX_ERROR(m_logger,"Error in parsing received TrafficEvent: " << string(e.what()));
		close(connectionSocket);
		bzero(buffer, MAX_MSG_SIZE);
		return false;
	}
	close(connectionSocket);
	return true;
}

/// This is a blocking function. If nothing is received, then wait on this thread for an answer
void ClassificationEngine::ReceiveGUICommand()
{
    struct sockaddr_un msgRemote;
	int socketSize, msgSocket;
	int bytesRead;
	GUIMsg msg = GUIMsg();
	u_char msgBuffer[MAX_GUIMSG_SIZE];

	socketSize = sizeof(msgRemote);

	//Blocking call
	if ((msgSocket = accept(GUISocket, (struct sockaddr *)&msgRemote, (socklen_t*)&socketSize)) == -1)
	{
		LOG4CXX_ERROR(m_logger,"accept: " << strerror(errno));
		close(msgSocket);
	}
	if((bytesRead = recv(msgSocket, msgBuffer, MAX_GUIMSG_SIZE, 0 )) == -1)
	{
		LOG4CXX_ERROR(m_logger,"recv: " << strerror(errno));
		close(msgSocket);
	}

	msg.deserializeMessage(msgBuffer);
	switch(msg.getType())
	{
		case EXIT:
			exit(1);
		case CLEAR_ALL:
			pthread_rwlock_wrlock(&lock);
			suspects.clear();
			pthread_rwlock_unlock(&lock);
			break;
		case CLEAR_SUSPECT:
			//TODO still no functionality for this yet
			break;
		default:
			break;
	}
	close(msgSocket);
}

//Send a silent alarm about the argument suspect
void Nova::ClassificationEngine::SendToUI(Suspect *suspect)
{
	GUIDataLen = suspect->serializeSuspect(GUIData);

	if ((GUISendSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		LOG4CXX_ERROR(m_logger,"socket: " << strerror(errno));
		close(GUISendSocket);
		return;
	}

	if (connect(GUISendSocket, GUISendPtr, GUILen) == -1)
	{
		LOG4CXX_ERROR(m_logger,"connect: " << strerror(errno));
		close(GUISendSocket);
		return;
	}

	if (send(GUISendSocket, GUIData, GUIDataLen, 0) == -1)
	{
		LOG4CXX_ERROR(m_logger,"send: " << strerror(errno));
		close(GUISendSocket);
		return;
	}
	close(GUISendSocket);
}


void ClassificationEngine::LoadConfig(char * input)
{
	//Used to verify all values have been loaded
	bool verify[CONFIG_FILE_LINE_COUNT];
	for(uint i = 0; i < CONFIG_FILE_LINE_COUNT; i++)
		verify[i] = false;

	string line;
	string prefix;
	ifstream config(input);

	if(config.is_open())
	{
		while(config.good())
		{
			getline(config,line);

			prefix = "INTERFACE";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(line.size() > 0)
				{
					hostAddrString = getLocalIP(line.c_str());
					if(hostAddrString.size() == 0)
					{
						LOG4CXX_ERROR(m_logger, "Bad interface, no IP's associated!");
						exit(1);
					}

					inet_pton(AF_INET,hostAddrString.c_str(),&(hostAddr.sin_addr));
					verify[0]=true;
				}
				continue;

			}

			prefix = "USE_TERMINALS";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atoi(line.c_str()) == 0 || atoi(line.c_str()) == 1)
				{
					useTerminals = atoi(line.c_str());
					verify[1]=true;
				}
				continue;
			}

			prefix = "BROADCAST_ADDR";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(line.size() > 6 && line.size() <  16)
				{
					broadcastAddr = line;
					verify[2]=true;
				}
				continue;
			}

			prefix = "SILENT_ALARM_PORT";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atoi(line.c_str()) > 0)
				{
					sAlarmPort = atoi(line.c_str());
					verify[3]=true;
				}
				continue;
			}

			prefix = "K";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atoi(line.c_str()) > 0)
				{
					k = atoi(line.c_str());
					verify[4]=true;
				}
				continue;
			}

			prefix = "EPS";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atof(line.c_str()) >= 0)
				{
					eps = atof(line.c_str());
					verify[5]=true;
				}
				continue;
			}

			prefix = "CLASSIFICATION_TIMEOUT";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atoi(line.c_str()) > 0)
				{
					classificationTimeout = atoi(line.c_str());
					verify[6]=true;
				}
				continue;
			}

			prefix = "IS_TRAINING";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atoi(line.c_str()) == 0 || atoi(line.c_str()) == 1)
				{
					isTraining = atoi(line.c_str());
					verify[7]=true;
				}
				continue;
			}

			prefix = "CLASSIFICATION_THRESHOLD";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(atof(line.c_str()) >= 0)
				{
					classificationThreshold = atof(line.c_str());
					verify[8]=true;
				}
				continue;
			}

			prefix = "AES_SA_KEY";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				for( uint i = 0; i < KEY_SIZE; i+=2 )
				{
					sscanf( line.c_str(), "%2x", SAkey[i] ) ;
				}

				verify[9]=true;

				continue;
			}

			prefix = "DATAFILE";
			if(!line.substr(0,prefix.size()).compare(prefix))
			{
				line = line.substr(prefix.size()+1,line.size());
				if(line.size() > 0 && !line.substr(line.size()-4, line.size()).compare(".txt"))
				{
					dataFile = line;
					verify[10]=true;
				}
				continue;
			}
		}

		//Checks to make sure all values have been set.
		bool v = true;
		for(uint i = 0; i < CONFIG_FILE_LINE_COUNT; i++)
		{
			v &= verify[i];
		}

		if(v == false)
		{
			LOG4CXX_ERROR(m_logger, "One or more values have not been set.");
			exit(1);
		}
		else
		{
			LOG4CXX_INFO(m_logger, "Config loaded successfully.");
		}
	}
	else
	{
		LOG4CXX_INFO(m_logger, "No configuration file detected.");
		exit(1);
	}
	config.close();
}

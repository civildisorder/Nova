#include "ClassificationAggregator.h"


using namespace std;

namespace Nova
{

ClassificationAggregator::ClassificationAggregator()
{
	// This 'could' be done based on config files

	vector<featureIndex> tcpFeatures;
	tcpFeatures.push_back(TCP_RATIO_SYN_ACK);
	tcpFeatures.push_back(TCP_RATIO_SYN_FIN);
	tcpFeatures.push_back(TCP_RATIO_SYN_RST);
	tcpFeatures.push_back(TCP_RATIO_SYN_SYNACK);
	tcpFeatures = removeDisabledFeatures(tcpFeatures);

	engines.push_back(new ClassificationEngine(tcpFeatures));
	engineNames.push_back("TCP Engine");
	engineWeights.push_back(0.5);


	vector<featureIndex> generalFeatures;
	generalFeatures.push_back(IP_TRAFFIC_DISTRIBUTION);
	generalFeatures.push_back(PORT_TRAFFIC_DISTRIBUTION);
	generalFeatures.push_back(DISTINCT_IPS);
	generalFeatures.push_back(DISTINCT_PORTS);
	generalFeatures.push_back(HAYSTACK_EVENT_FREQUENCY);
	generalFeatures.push_back(PACKET_SIZE_MEAN);
	generalFeatures.push_back(PACKET_SIZE_DEVIATION);
	generalFeatures.push_back(PACKET_INTERVAL_MEAN);
	generalFeatures.push_back(PACKET_INTERVAL_DEVIATION);
	generalFeatures = removeDisabledFeatures(generalFeatures);

	engines.push_back(new ClassificationEngine(generalFeatures));
	engineNames.push_back("General Engine");
	engineWeights.push_back(0.5);

	Initialize();
}

double ClassificationAggregator::Classify(Suspect *s)
{
	double classification = 0;
	for (uint i = 0; i < engines.size(); i++)
	{
		double engineVote = engines.at(i)->Classify(s);
		classification += engineVote * engineWeights.at(i);
		cout << "Suspect: " << s->GetIpAddress() << " Engine: " << engineNames.at(i) << " Classification: " << engineVote << endl;
	}
    cout << "Suspect: " << s->GetIpAddress() << " Engine: aggrigated" << " Classification: " << classification << endl;
    cout << endl;
    s->SetClassification(classification);

	return classification;
}

void ClassificationAggregator::Initialize()
{
	// Init the engines
	for (uint i = 0; i < engines.size(); i++)
	{
		engines.at(i)->LoadDataPointsFromFile(Config::Inst()->GetPathTrainingFile());
	}
}

std::vector<featureIndex> ClassificationAggregator::removeDisabledFeatures(std::vector<featureIndex> engineFeatures)
{
	vector<featureIndex> enabledFeaturesOnly;
	for (uint j = 0; j < engineFeatures.size(); j++)
	{
		if (Config::Inst()->IsFeatureEnabled(engineFeatures.at(j)))
		{
			enabledFeaturesOnly.push_back(engineFeatures.at(j));
		}
	}
	return enabledFeaturesOnly;
}

} /* namespace Nova */

//============================================================================
// Name        : ClassificationEngine.h
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
// Description : Suspect classification engine
//============================================================================

#include "ClassificationEngine.h"
#include "SuspectTable.h"
#include "Config.h"

#include <sstream>

using namespace std;
using namespace Nova;

// Normalization method to use on each feature
// TODO: Make this a configuration var somewhere in Novaconfig.txt?
normalizationType ClassificationEngine::m_normalization[] = {
		LINEAR_SHIFT, // Don't normalize IP traffic distribution, already between 0 and 1
		LINEAR_SHIFT,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC,
		LOGARITHMIC
};

ClassificationEngine::ClassificationEngine(std::vector<featureIndex> enabledFeatures)
{
	m_normalizedDataPts = NULL;
	m_dataPts = NULL;


	// TODO DTC
	// TODO Why is the doppelganger here? Needs to be somewhere else, taking SuspectTable reference out
	//m_dopp = new Doppelganger(suspects);
	//m_dopp->InitDoppelganger();

	m_featureIndexes = enabledFeatures;
	m_squrtDim = m_featureIndexes.size();

	for (int i = 0; i < m_featureIndexes.size(); i++)
	{
		cout << m_featureIndexes.at(i) << endl;
	}


}

ClassificationEngine::~ClassificationEngine()
{

}

void ClassificationEngine::FormKdTree()
{
	delete m_kdTree;
	//Normalize the data points
	//Foreach data point
	for(uint j = 0;j < m_featureIndexes.size();j++)
	{
		//Foreach feature within the data point
		for(int i=0;i < m_nPts;i++)
		{
			if(m_maxFeatureValues[j] != 0)
			{
				m_normalizedDataPts[i][j] = Normalize(m_normalization[j], m_dataPts[i][j], m_minFeatureValues[j], m_maxFeatureValues[j]);
			}
			else
			{
				LOG(ERROR,"Problem normalizing training data.",
					"The max value of a feature was 0, the training data file may be corrupt or missing.");
				break;
			}
		}
	}
	m_kdTree = new ANNkd_tree(					// build search structure
			m_normalizedDataPts,					// the data points
					m_nPts,						// number of points
					m_featureIndexes.size());						// dimension of space
}


double ClassificationEngine::Classify(Suspect *suspect)
{
	int k = Config::Inst()->GetK();
	double d;
	featureIndex fi;

	ANNidxArray nnIdx = new ANNidx[k];			// allocate near neigh indices
	ANNdistArray dists = new ANNdist[k];		// allocate near neighbor dists

	//Allocate the ANNpoint;
	ANNpoint aNN = annAllocPt(m_featureIndexes.size());
	FeatureSet fs = suspect->GetFeatureSet(MAIN_FEATURES);
	uint ai = 0;


	//Iterate over the features, asserting the range is [min,max] and normalizing over that range
	for(uint i = 0; i < m_featureIndexes.size();i++)
	{
		if(fs.m_features[m_featureIndexes.at(i)] > m_maxFeatureValues[ai])
		{
			fs.m_features[m_featureIndexes.at(i)] = m_maxFeatureValues[ai];
		}
		else if(fs.m_features[m_featureIndexes.at(i)] < m_minFeatureValues[ai])
		{
			fs.m_features[m_featureIndexes.at(i)] = m_minFeatureValues[ai];
		}
		if(m_maxFeatureValues[ai] != 0)
		{
			aNN[ai] = Normalize(m_normalization[m_featureIndexes.at(i)], suspect->GetFeatureSet(MAIN_FEATURES).m_features[i],
				m_minFeatureValues[ai], m_maxFeatureValues[ai]);
		}
		else
		{
			LOG(ERROR, "Classification engine has encountered an error.",
				"Max value for a feature is 0. Normalization failed.");
		}
		ai++;
	}
	suspect->SetFeatureSet(&fs, MAIN_FEATURES);

	if(aNN == NULL)
	{
		LOG(ERROR, "Classification engine has encountered an error.",
			"Classify had trouble allocating the ANN point. Aborting.");
		return -1;
	}

	m_kdTree->annkSearch(							// search
			aNN,								// query point
			k,									// number of near neighbors
			nnIdx,								// nearest neighbors (returned)
			dists,								// distance (returned)
			Config::Inst()->GetEps());								// error bound

	for(int i = 0; i < DIM; i++)
	{
		fi = (featureIndex)i;
		suspect->SetFeatureAccuracy(fi, 0);
	}
	suspect->SetHostileNeighbors(0);

	//Determine classification according to weight by distance
	//	.5 + E[(1-Dist) * Class] / 2k (Where Class is -1 or 1)
	//	This will make the classification range from 0 to 1
	double classifyCount = 0;

	for(int i = 0; i < k; i++)
	{
		dists[i] = sqrt(dists[i]);				// unsquare distance
		for(uint j = 0; j < m_featureIndexes.size(); j++)
		{
				double distance = (aNN[m_featureIndexes.at(j)] - m_kdTree->thePoints()[nnIdx[i]][m_featureIndexes.at(j)]);

				if(distance < 0)
				{
					distance *= -1;
				}

				fi = (featureIndex)m_featureIndexes.at(j);
				d  = suspect->GetFeatureAccuracy(fi) + distance;
				suspect->SetFeatureAccuracy(fi, d);
		}

		if(nnIdx[i] == -1)
		{
			stringstream ss;
			ss << "Unable to find a nearest neighbor for Data point " << i <<" Try decreasing the Error bound";
			LOG(ERROR, "Classification engine has encountered an error.", ss.str());
		}
		else
		{
			//If Hostile
			if(m_dataPtsWithClass[nnIdx[i]]->m_classification == 1)
			{
				classifyCount += (m_squrtDim - dists[i]);
				suspect->SetHostileNeighbors(suspect->GetHostileNeighbors()+1);
			}
			//If benign
			else if(m_dataPtsWithClass[nnIdx[i]]->m_classification == 0)
			{
				classifyCount -= (m_squrtDim - dists[i]);
			}
			else
			{
				stringstream ss;
				ss << "Data point has invalid classification. Should by 0 or 1, but is " << m_dataPtsWithClass[nnIdx[i]]->m_classification;
				LOG(ERROR, "Classification engine has encountered an error.", ss.str());
				suspect->SetClassification(-1);
				delete [] nnIdx;							// clean things up
				delete [] dists;
				annClose();
				return -1;
			}
		}
	}
	for(int j = 0; j < DIM; j++)
	{
		fi = (featureIndex)j;
		d = suspect->GetFeatureAccuracy(fi) / k;
		suspect->SetFeatureAccuracy(fi, d);
	}

	suspect->SetClassification(.5 + (classifyCount / ((2.0 * (double)k) * m_squrtDim )));

	// Fix for rounding errors caused by double's not being precise enough if DIM is something like 2
	if(suspect->GetClassification() < 0)
	{
		suspect->SetClassification(0);
	}
	else if(suspect->GetClassification() > 1)
	{
		suspect->SetClassification(1);
	}

	if(suspect->GetClassification() > Config::Inst()->GetClassificationThreshold())
	{
		suspect->SetIsHostile(true);
	}
	else
	{
		suspect->SetIsHostile(false);
	}

	delete [] nnIdx;							// clean things up
    delete [] dists;

    annClose();
    annDeallocPt(aNN);
	suspect->SetNeedsClassificationUpdate(false);

	return suspect->GetClassification();
}

void ClassificationEngine::PrintPt(ostream &out, ANNpoint p)
{
	out << "(" << p[0];
	for(uint i = 1;i < m_featureIndexes.size();i++)
	{
		out << ", " << p[i];
	}
	out << ")\n";
}


void ClassificationEngine::LoadDataPointsFromFile(string inFilePath)
{
	ifstream myfile (inFilePath.data());
	string line;

		// Clear max and min values
		for(int i = 0; i < DIM; i++)
		{
			m_maxFeatureValues[i] = 0;
		}

		for(int i = 0; i < DIM; i++)
		{
			m_minFeatureValues[i] = 0;
		}

		for(int i = 0; i < DIM; i++)
		{
			m_meanFeatureValues[i] = 0;
		}

		// Reload the data file
		if(m_dataPts != NULL)
		{
			annDeallocPts(m_dataPts);
		}
		if(m_normalizedDataPts != NULL)
		{
			annDeallocPts(m_normalizedDataPts);
		}

		m_dataPtsWithClass.clear();

	//string array to check whether a line in data.txt file has the right number of fields
	string fieldsCheck[DIM];
	bool valid = true;

	int i = 0;
	int k = 0;
	int badLines = 0;

	//Count the number of data points for allocation
	if(myfile.is_open())
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

	else
	{
		LOG(ERROR,"Classification Engine has encountered a problem",
			"Unable to open the training data file at "+ Config::Inst()->GetPathTrainingFile()+".");
	}

	myfile.close();
	int maxPts = i;

	//Open the file again, allocate the number of points and assign
	myfile.open(inFilePath.data(), ifstream::in);
	m_dataPts = annAllocPts(maxPts, m_featureIndexes.size()); // allocate data points
	m_normalizedDataPts = annAllocPts(maxPts, m_featureIndexes.size());


	if(myfile.is_open())
	{
		i = 0;

		while (!myfile.eof() && (i < maxPts))
		{
			k = 0;

			if(myfile.peek() == EOF)
			{
				break;
			}

			//initializes fieldsCheck to have all array indices contain the string "NotPresent"
			for(int j = 0; j < DIM; j++)
			{
				fieldsCheck[j] = "NotPresent";
			}

			//this will grab a line of values up to a newline or until DIM values have been taken in.
			while(myfile.peek() != '\n' && k < DIM)
			{
				getline(myfile, fieldsCheck[k], ' ');
				k++;
			}


			//starting from the end of fieldsCheck, if NotPresent is still inside the array, then
			//the line of the data.txt file is incorrect, set valid to false. Note that this
			//only works in regards to the 9 data points preceding the classification,
			//not the classification itself.
			for(int m = DIM - 1; m >= 0 && valid; m--)
			{
				if(!fieldsCheck[m].compare("NotPresent"))
				{
					valid = false;
				}
			}

			//if the next character is a newline after extracting as many data points as possible,
			//then the classification is not present. For now, I will merely discard the line;
			//there may be a more elegant way to do it. (i.e. pass the data to Classify or something)
			if(myfile.peek() == '\n' || myfile.peek() == ' ')
			{
				valid = false;
			}

			//if the line is valid, continue as normal
			if(valid)
			{
				m_dataPtsWithClass.push_back(new Point(m_featureIndexes.size()));

				// Used for matching the 0->DIM index with the 0->Config::Inst()->getEnabledFeatureCount() index
				int actualDimension = 0;
				for(uint defaultDimension = 0;defaultDimension < m_featureIndexes.size();defaultDimension++)
				{
					double temp = strtod(fieldsCheck[m_featureIndexes.at(defaultDimension)].data(), NULL);

					m_dataPtsWithClass[i]->m_annPoint[actualDimension] = temp;
					m_dataPts[i][actualDimension] = temp;

					//Set the max values of each feature. (Used later in normalization)
					if(temp > m_maxFeatureValues[actualDimension])
					{
						m_maxFeatureValues[actualDimension] = temp;
					}
					if(temp < m_minFeatureValues[actualDimension])
					{
						m_minFeatureValues[actualDimension] = temp;
					}

					m_meanFeatureValues[actualDimension] += temp;

					actualDimension++;
				}
				getline(myfile,line);
				m_dataPtsWithClass[i]->m_classification = atoi(line.data());
				i++;
			}
			//but if it isn't, just get to the next line without incrementing i.
			//this way every correct line will be inserted in sequence
			//without any gaps due to perhaps multiple line failures, etc.
			else
			{
				getline(myfile,line);
				badLines++;
			}
		}
		m_nPts = i;

		for(int j = 0; j < DIM; j++)
			m_meanFeatureValues[j] /= m_nPts;
	}
	else
	{
		LOG(ERROR,"Classification Engine has encountered a problem",
			"Unable to open the training data file at "+inFilePath+".");
	}
	myfile.close();

	//Normalize the data points

	//Foreach feature within the data point
	for(uint j = 0;j < m_featureIndexes.size();j++)
	{
		//Foreach data point
		for(int i=0;i < m_nPts;i++)
		{
			m_normalizedDataPts[i][j] = Normalize(m_normalization[j], m_dataPts[i][j], m_minFeatureValues[j], m_maxFeatureValues[j]);
		}
	}

	m_kdTree = new ANNkd_tree(					// build search structure
			m_normalizedDataPts,					// the data points
					m_nPts,						// number of points
					m_featureIndexes.size());						// dimension of space
}

double ClassificationEngine::Normalize(normalizationType type, double value, double min, double max)
{
	switch (type)
	{
		case LINEAR:
		{
			return value / max;
		}
		case LINEAR_SHIFT:
		{
			return (value -min) / (max - min);
		}
		case LOGARITHMIC:
		{
			if(!value || !max)
				return 0;
			else return(log(value)/log(max));
			//return (log(value - min + 1)) / (log(max - min + 1));
		}
		case NONORM:
		{
			return value;
		}
		default:
		{
			//logger->Logging(ERROR, "Normalization failed: Normalization type unkown");
			return 0;
		}

		// TODO: A sigmoid normalization function could be very useful,
		// especially if we could somehow use it interactively to set the center and smoothing
		// while looking at the data visualizations to see what works best for a feature
	}
}


void ClassificationEngine::WriteDataPointsToFile(string outFilePath, ANNkd_tree* tree)
{
	ofstream myfile (outFilePath.data());

	if(myfile.is_open())
	{
		for(int i = 0; i < tree->nPoints(); i++ )
		{
			for(int j=0; j < tree->theDim(); j++)
			{
				myfile << tree->thePoints()[i][j] << " ";
			}
			myfile << m_dataPtsWithClass[i]->m_classification;
			myfile << "\n";
		}
	}
	else
	{
		LOG(ERROR,"Classification Engine has encountered a problem",
			"Unable to open the training data file at "+outFilePath+".");
	}
	myfile.close();

}

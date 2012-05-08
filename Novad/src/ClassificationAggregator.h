#ifndef CLASSIFICATIONAGGREGATOR_H_
#define CLASSIFICATIONAGGREGATOR_H_

#include "ClassificationEngine.h"

namespace Nova
{

class ClassificationAggregator
{
public:
	ClassificationAggregator();

	// TODO: Make this a single struct/class
	std::vector<ClassificationEngine*> engines;
	std::vector<std::string> engineNames;
	std::vector<double> engineWeights;

	double Classify(Suspect *s);



};

} /* namespace Nova */
#endif /* CLASSIFICATIONAGGREGATOR_H_ */

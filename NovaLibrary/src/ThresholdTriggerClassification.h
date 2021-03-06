//============================================================================
// Name        : ThresholdTriggerClassification.h
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
// Description : TODO: Description here
//============================================================================

#ifndef THRESHOLDTRIGGERCLASSIFICATION_H_
#define THRESHOLDTRIGGERCLASSIFICATION_H_

#include "ClassificationEngine.h"

namespace Nova
{

struct HostileThreshold
{
	double m_maxValueTrigger;
	double m_minValueTrigger;
	bool m_hasMinValueTrigger;
	bool m_hasMaxValueTrigger;
};

class ThresholdTriggerClassification: public Nova::ClassificationEngine
{
public:
	ThresholdTriggerClassification();

	double Classify(Suspect *suspect);
};

} /* namespace Nova */

#endif /* THRESHOLDTRIGGERCLASSIFICATION_H_ */

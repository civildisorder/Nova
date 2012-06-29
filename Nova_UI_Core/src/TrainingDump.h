//============================================================================
// Name        : TrainingData.h
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

#ifndef TRAININGDUMP_H_
#define TRAININGDUMP_H_

#include <string>
#include <vector>

#include "HashMapStructs.h"

class TrainingDump
{
public:
	// Parse a CE dump file
	TrainingDump(std::string pathDumpFile);

	bool SetDescription(std::string uid, std::string description);
	bool SetIsHostile(std::string uid, bool isHostile);
	bool SetIsIncluded(std::string uid, bool isIncluded);

	// Removes consecutive points who's squared distance is less than a specified distance
	void ThinTrainingPoints(double distanceThreshhold);

private:
	trainingDumpMap* trainingTable;

};

#endif /* TRAININGDUMP_H_ */

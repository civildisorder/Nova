#include "gtest/gtest.h"

#include "FeatureSet.h"

using namespace Nova;

class FeatureSetTest : public ::testing::Test {
protected:
	FeatureSet fset;
	virtual void SetUp() {
		Config::Inst()->SetEnabledFeatures("111111111");

		// First test packet
		Evidence p1;
		p1.m_evidencePacket.ip_p = 6;
		// These are just made up input values that make the math easy
		p1.m_evidencePacket.dst_port = 80;
		p1.m_evidencePacket.ip_dst = 1;
		p1.m_evidencePacket.ip_src = 123456;

		// Note: the byte order gets flipped for this
		p1.m_evidencePacket.ip_len = (u_short)1;
		p1.m_evidencePacket.ts = 10;


		// Second test packet
		Evidence p2;
		p2.m_evidencePacket.ip_p = 6;
		p2.m_evidencePacket.dst_port = 20;
		p2.m_evidencePacket.ip_dst = 2;
		p2.m_evidencePacket.ip_src = 98765;

		// Note: the byte order gets flipped for this
		p2.m_evidencePacket.ip_len = (u_short)1;
		p2.m_evidencePacket.ts = 20;


		fset.UpdateEvidence(p1);
		fset.UpdateEvidence(p2);
		fset.CalculateAll();
	}

};


// Check copy constructor and equality operator
TEST_F(FeatureSetTest, test_CopyAndAssignmentEquality)
{
	FeatureSet assignment = fset;
	EXPECT_TRUE(assignment == fset);

	FeatureSet copy(fset);
	EXPECT_TRUE(copy == fset);
}


// Simple check on the + and - operators
TEST_F(FeatureSetTest, test_ArithmiticEquality)
{
	FeatureSet temp = fset;
	temp += fset;
	temp.CalculateAll();

	temp -= fset;
	temp.CalculateAll();

	// We should end up back where we started
	EXPECT_TRUE(temp == fset);
}


// Check serialization/deserialization of the FeatureSet
TEST_F(FeatureSetTest, test_Serialization)
{
	// Serialize our featureSet to a buffer
	u_char buffer[MAX_MSG_SIZE];
	bzero(buffer, MAX_MSG_SIZE);
	EXPECT_NO_FATAL_FAILURE(fset.SerializeFeatureData(&buffer[0]));

	// Deserialize it and see if we end up with an exact copy
	FeatureSet deserializedCopy;
	EXPECT_NO_FATAL_FAILURE(deserializedCopy.DeserializeFeatureData(buffer));
	EXPECT_NO_FATAL_FAILURE(deserializedCopy.CalculateAll());

	// TODO: Make the FeatureSet equality operator compare the timestamps as well, see issue #73
	EXPECT_TRUE(fset == deserializedCopy);
}


// Check if the features got computed correctly
TEST_F(FeatureSetTest, test_Calculate)
{
	EXPECT_EQ(fset.m_features[IP_TRAFFIC_DISTRIBUTION], 1);
	EXPECT_EQ(fset.m_features[PORT_TRAFFIC_DISTRIBUTION], 1);
	EXPECT_EQ(fset.m_features[HAYSTACK_EVENT_FREQUENCY], 0.1);
	EXPECT_EQ(fset.m_features[PACKET_SIZE_DEVIATION], 0);
	EXPECT_EQ(fset.m_features[PACKET_SIZE_MEAN], 256);
	EXPECT_EQ(fset.m_features[DISTINCT_IPS], 2);
	EXPECT_EQ(fset.m_features[DISTINCT_PORTS], 2);
	EXPECT_EQ(fset.m_features[PACKET_INTERVAL_MEAN], 10);
	EXPECT_EQ(fset.m_features[PACKET_INTERVAL_DEVIATION], 0);
}

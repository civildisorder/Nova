// ============================================================================
// Name        : tseter_SuspectTable.h
// Copyright   : DataSoft Corporation 2011-2012
// 	Nova is free software: you can redistribute it and/or modify
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
//   along with Nova.  If not, see <http:// www.gnu.org/licenses/>.
// Description : This file contains unit tests for the class SuspectTable
// ============================================================================/*

#include "gtest/gtest.h"
#include "SuspectTable.h"
#include "Suspect.h"

/*

// The fixture for testing class SuspectTable.
class SuspectTableTest : public ::testing::Test, public SuspectTable {
protected:
	SuspectTable table;
	Suspect *s1, *s2;

	SuspectTableTest() {}

	void InitSuspects()
	{
		s1 = new Suspect();
		s1->SetIpAddress(1);
		s1->SetIsHostile(false);

		s2 = new Suspect();
		s2->SetIpAddress(2);
		s2->SetIsHostile(true);

		table.AddNewSuspect(s1);
		table.AddNewSuspect(s2);
	}
};


TEST_F(SuspectTableTest, Size) {
	// Test for proper result on an empty table
	EXPECT_EQ((uint)0, table.Size());

	InitSuspects();
	EXPECT_EQ((uint)2, table.Size());
}

TEST_F(SuspectTableTest, GetKeys) {
	// Test for proper result on an empty table
	EXPECT_EQ((uint)0, table.GetKeys_of_BenignSuspects().size());
	EXPECT_EQ((uint)0, table.GetKeys_of_HostileSuspects().size());

	InitSuspects();

	vector<Nova::SuspectIdentifier> goodKeys = table.GetKeys_of_BenignSuspects();
	vector<Nova::SuspectIdentifier> badKeys = table.GetKeys_of_HostileSuspects();

	EXPECT_EQ((uint)1, goodKeys.size());
	EXPECT_EQ((uint)1, goodKeys.at(0));

	EXPECT_EQ((uint)1, badKeys.size());
	EXPECT_EQ((uint)2, badKeys.at(0));
}


TEST_F(SuspectTableTest, IsValidKey) {
	// Test for proper result on an empty table
	SuspectIdentifier id;
	id.m_ip = 0;

	EXPECT_FALSE(table.IsValidKey(id));

	id.m_ip = 42;
	EXPECT_FALSE(table.IsValidKey(id));

	InitSuspects();
	id.m_ip = 0;
	EXPECT_FALSE(table.IsValidKey(id));
	id.m_ip = 42;
	EXPECT_FALSE(table.IsValidKey(id));
	id.m_ip = 1;
	EXPECT_TRUE(table.IsValidKey(id));
	id.m_ip = 2;
	EXPECT_TRUE(table.IsValidKey(id));
}

TEST_F(SuspectTableTest, Erase) {
	SuspectIdentifier id;
	id.m_ip = 42;
	// Test for proper result on an empty table
	EXPECT_FALSE(table.Erase(id));

	InitSuspects();
	EXPECT_FALSE(table.Erase(id));

	//EXPECT_EQ(SUSPECT_NOT_CHECKED_OUT , table.Erase(1));
	//table.CheckOut(1);

	id.m_ip = 1;
	EXPECT_TRUE(table.Erase(id.m_ip));
}


TEST_F(SuspectTableTest, CheckInAndOut) {
	Suspect *s = new Suspect();
	s->SetIpAddress(42);

	// Check in a suspect that wasn't in the table
	EXPECT_EQ(SUSPECT_KEY_INVALID, table.CheckIn(s));
	EXPECT_EQ(SUSPECT_KEY_INVALID, table.CheckIn(s));

	// Test for proper result on an empty table
	EXPECT_EQ(table.m_emptySuspect.GetIpAddress(), table.CheckOut((uint64_t)42).GetIpAddress());
	EXPECT_EQ(table.m_emptySuspect.GetIpAddress(), table.CheckOut((in_addr_t)42).GetIpAddress());

	// Check in a suspect that wasn't in the table
	EXPECT_EQ(SUSPECT_KEY_INVALID, table.CheckIn(s));

	InitSuspects();

	// Make sure invalid keys still fail
	EXPECT_EQ(table.m_emptySuspect.GetIpAddress(), table.CheckOut((uint64_t)42).GetIpAddress());
	EXPECT_EQ(table.m_emptySuspect.GetIpAddress(), table.CheckOut((in_addr_t)42).GetIpAddress());

	// Check some suspects out
	Suspect checkedOutS1 = table.CheckOut((in_addr_t)1);
	Suspect checkedOutS2 = table.CheckOut((in_addr_t)2);
	EXPECT_EQ(s1->GetIpAddress(), checkedOutS1.GetIpAddress());
	EXPECT_EQ(s2->GetIpAddress(), checkedOutS2.GetIpAddress());

	// Check them back in
	EXPECT_EQ(SUSPECT_TABLE_CALL_SUCCESS, table.CheckIn(&checkedOutS1));
	EXPECT_EQ(SUSPECT_NOT_CHECKED_OUT, table.CheckIn(&checkedOutS1));

	// Make sure we can't check out the same suspect more than once
	// xxx: Apparently this is allowed. Make sure the desired functionality is to allow multiple CheckIns in a row

	// ^^^ regarding above - This is allowed, it performs a manual check out if the suspect isn't already checked out
	// however this call will fail if another thread beats you to it or the suspect is erased so don't expect it in that case.
	// - Dave S


	EXPECT_EQ(SUSPECT_TABLE_CALL_SUCCESS, table.CheckIn(&checkedOutS2));

}

// TODO: Enable this test again when ticket
TEST_F(SuspectTableTest, GetSuspect) {
	// Test for proper result on an empty table
	EXPECT_EQ(table.m_emptySuspect.GetIpAddress(), table.GetSuspect(42).GetIpAddress());

	InitSuspects();
	EXPECT_EQ(table.m_emptySuspect.GetIpAddress(), table.GetSuspect(42).GetIpAddress());
	EXPECT_EQ((uint)1, table.GetSuspect(1).GetIpAddress());
	EXPECT_EQ((uint)2, table.GetSuspect(2).GetIpAddress());
}

*/

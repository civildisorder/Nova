//============================================================================
// Name        : novaconfig.cpp
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
// Description : NOVA preferences/configuration window
//============================================================================

#include <QtGui/QRadioButton>
#include <boost/foreach.hpp>
#include <QtGui/QFileDialog>
#include <netinet/in.h>
#include <QtCore/QDir>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <errno.h>
#include <fstream>

#include "Logger.h"
#include "NovaUtil.h"
#include "novaconfig.h"
#include "subnetPopup.h"
#include "HoneydConfiguration/NodeManager.h"
#include "NovaComplexDialog.h"

using boost::property_tree::ptree;
using namespace Nova;
using namespace std;

/************************************************
  Construct and Initialize GUI
 ************************************************/

NovaConfig::NovaConfig(QWidget *parent)
    : QMainWindow(parent)
{
	m_portMenu = new QMenu(this);
	m_profileTreeMenu = new QMenu(this);
	m_nodeTreeMenu = new QMenu(this);
	m_loading = new QMutex(QMutex::NonRecursive);

	m_honeydConfig = new HoneydConfiguration();

    //Keys used to maintain and lookup current selections
    m_currentProfile = "";
    m_currentNode = "";
    m_currentSubnet = "";

    //flag to avoid GUI signal conflicts
    m_editingItems = false;
    m_selectedSubnet = false;
    m_loadingDefaultActions = false;

	//Store parent and load UI
	m_mainwindow = (NovaGUI*)parent;
	//Set up a Reference to the dialog prompter
	m_prompter = m_mainwindow->m_prompter;
	// Set up the GUI
	ui.setupUi(this);

	SetInputValidators();
	m_loading->lock();
	m_radioButtons = new QButtonGroup(ui.loopbackGroupBox);
	m_interfaceCheckBoxes = new QButtonGroup(ui.interfaceGroupBox);

	//Read NOVAConfig, pull honeyd info from parent, populate GUI
	LoadNovadPreferences();
	m_honeydConfig->LoadAllTemplates();
	LoadHaystackConfiguration();

	vector<string> nodeGroupOptions = m_honeydConfig->m_groups;
	QStringList groups;
	int selectedIndex = 0;
	for(uint i = 0; i < nodeGroupOptions.size(); i++)
	{
		groups << QString::fromStdString(nodeGroupOptions.at(i));

		if(nodeGroupOptions[i] == Config::Inst()->GetGroup())
		{
			selectedIndex = i;
		}
	}
	ui.haystackGroupComboBox->addItems(groups);
	ui.haystackGroupComboBox->setCurrentIndex(selectedIndex);

	m_loading->unlock();
	// Populate the dialog menu
	for(uint i = 0; i < m_mainwindow->m_prompter->m_registeredMessageTypes.size(); i++)
	{
		ui.msgTypeListWidget->insertItem(i, new QListWidgetItem(QString::fromStdString(
				m_mainwindow->m_prompter->m_registeredMessageTypes[i].descriptionUID)));
	}

	ui.menuTreeWidget->expandAll();

	ui.featureList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(ui.featureList, SIGNAL(customContextMenuRequested(const QPoint &)), this,
			SLOT(onFeatureClick(const QPoint &)));
	connect(m_interfaceCheckBoxes, SIGNAL(buttonReleased(QAbstractButton *)), this,
			SLOT(interfaceCheckBoxes_buttonClicked(QAbstractButton *)));
}

NovaConfig::~NovaConfig()
{

}

void NovaConfig::interfaceCheckBoxes_buttonClicked(QAbstractButton *button)
{
	if(m_interfaceCheckBoxes->checkedButton() == NULL)
	{
		((QCheckBox *)button)->setChecked(true);
	}
}

void NovaConfig::contextMenuEvent(QContextMenuEvent *event)
{
	if(ui.portTreeWidget->hasFocus() || ui.portTreeWidget->underMouse())
	{
		m_portMenu->clear();
		m_portMenu->addAction(ui.actionAddPort);
		if(ui.portTreeWidget->topLevelItemCount())
		{
			m_portMenu->addSeparator();
			m_portMenu->addAction(ui.actionToggle_Inherited);
			m_portMenu->addAction(ui.actionDeletePort);
		}
		QPoint globalPos = event->globalPos();
		m_portMenu->popup(globalPos);
	}
	else if(ui.profileTreeWidget->hasFocus() || ui.profileTreeWidget->underMouse())
	{
		m_profileTreeMenu->clear();
		m_profileTreeMenu->addAction(ui.actionProfileAdd);
		m_profileTreeMenu->addSeparator();
		m_profileTreeMenu->addAction(ui.actionProfileClone);
		m_profileTreeMenu->addSeparator();
		m_profileTreeMenu->addAction(ui.actionProfileDelete);

		QPoint globalPos = event->globalPos();
		m_profileTreeMenu->popup(globalPos);
	}
	else if(ui.nodeTreeWidget->hasFocus() || ui.nodeTreeWidget->underMouse())
	{
		m_nodeTreeMenu->clear();
		m_nodeTreeMenu->addAction(ui.actionSubnetAdd);
		m_nodeTreeMenu->addSeparator();
		m_nodeTreeMenu->addAction(ui.actionNodeEdit);
		if(m_selectedSubnet)
		{
			ui.actionNodeEdit->setText("Edit Subnet");
			ui.actionNodeEnable->setText("Enable Subnet");
			ui.actionNodeDisable->setText("Disable Subnet");
			m_nodeTreeMenu->addAction(ui.actionNodeAdd);
			m_nodeTreeMenu->addSeparator();
		}
		else
		{
			ui.actionNodeEdit->setText("Edit Node");
			ui.actionNodeEnable->setText("Enable Node");
			ui.actionNodeDisable->setText("Disable Node");
			m_nodeTreeMenu->addAction(ui.actionNodeCustomizeProfile);
			m_nodeTreeMenu->addSeparator();
			m_nodeTreeMenu->addAction(ui.actionNodeAdd);
			m_nodeTreeMenu->addAction(ui.actionNodeClone);
			m_nodeTreeMenu->addSeparator();

		}
		m_nodeTreeMenu->addAction(ui.actionNodeDelete);
		m_nodeTreeMenu->addSeparator();
		m_nodeTreeMenu->addAction(ui.actionNodeEnable);
		m_nodeTreeMenu->addAction(ui.actionNodeDisable);

		QPoint globalPos = event->globalPos();
		m_nodeTreeMenu->popup(globalPos);
	}
}

void NovaConfig::on_actionNo_Action_triggered()
{
	return;
}

void NovaConfig::on_actionToggle_Inherited_triggered()
{
	if(!m_loading->tryLock())
	{
		return;
	}
	if(!ui.portTreeWidget->selectedItems().empty())
	{
		Port *prt = NULL;
		for(PortTable::iterator it = m_honeydConfig->m_ports.begin(); it != m_honeydConfig->m_ports.end(); it++)
		{
			if(IsPortTreeWidgetItem(it->second.m_portName, ui.portTreeWidget->currentItem()))
			{
				//iterators are copies not the actual items
				prt = &m_honeydConfig->m_ports[it->second.m_portName];
				break;
			}
		}
		NodeProfile *p = &m_honeydConfig->m_profiles[m_currentProfile];
		for(uint i = 0; i < p->m_ports.size(); i++)
		{
			if(!p->m_ports[i].first.compare(prt->m_portName))
			{
				//If the port is inherited we can just make it explicit
				if(p->m_ports[i].second.first)
				{
					p->m_ports[i].second.first = false;
				}

				//If the port isn't inherited and the profile has parents
				else if(p->m_parentProfile.compare(""))
				{
					NodeProfile *parent = &m_honeydConfig->m_profiles[p->m_parentProfile];
					uint j = 0;
					//check for the inherited port
					for(j = 0; j < parent->m_ports.size(); j++)
					{
						Port temp = m_honeydConfig->m_ports[parent->m_ports[j].first];
						if(!prt->m_portNum.compare(temp.m_portNum) && !prt->m_type.compare(temp.m_type))
						{
							p->m_ports[i].first = temp.m_portName;
							p->m_ports[i].second.first = true;
						}
					}
					if(!p->m_ports[i].second.first)
					{
						m_loading->unlock();
						m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->CANNOT_INHERIT_PORT, "The selected port cannot be inherited"
								" from any ancestors, would you like to keep it?", ui.actionNo_Action, ui.actionDeletePort, this);
						m_loading->lock();
					}
				}
				//If the port isn't inherited and the profile has no parent
				else
				{
					LOG(ERROR, "Cannot inherit without any ancestors.", "");
					m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->NO_ANCESTORS,
						"Cannot inherit without any ancestors.");
				}
				break;
			}
		}
		LoadProfileSettings();
		SaveProfileSettings();
	}
	m_loading->unlock();
}

void NovaConfig::on_actionAddPort_triggered()
{
	if(m_loading->tryLock())
	{
		if(m_honeydConfig->m_profiles.keyExists(m_currentProfile))
		{
			NodeProfile p = m_honeydConfig->m_profiles[m_currentProfile];

			Port pr;
			pr.m_portNum = "1";
			pr.m_type = "TCP";
			pr.m_behavior = "open";
			pr.m_portName = "1_TCP_open";
			pr.m_scriptName = "";

			//These don't need to be deleted because the clear function
			// and destructor of the tree widget does that already.
			QTreeWidgetItem *item = new QTreeWidgetItem(0);
			item->setText(0,(QString)pr.m_portNum.c_str());
			item->setText(1,(QString)pr.m_type.c_str());
			if(!pr.m_behavior.compare("script"))
			{
				item->setText(2, (QString)pr.m_scriptName.c_str());
			}
			else
			{
				item->setText(2,(QString)pr.m_behavior.c_str());
			}
			ui.portTreeWidget->addTopLevelItem(item);

			TreeItemComboBox *typeBox = new TreeItemComboBox(this, item);
			typeBox->addItem("TCP");
			typeBox->addItem("UDP");
			typeBox->setItemText(0, "TCP");
			typeBox->setItemText(1, "UDP");
			connect(typeBox, SIGNAL(notifyParent(QTreeWidgetItem *, bool)), this, SLOT(portTreeWidget_comboBoxChanged(QTreeWidgetItem *, bool)));

			TreeItemComboBox *behaviorBox = new TreeItemComboBox(this, item);
			behaviorBox->addItem("reset");
			behaviorBox->addItem("open");
			behaviorBox->addItem("block");
			behaviorBox->insertSeparator(3);

			vector<string> scriptNames = m_honeydConfig->GetScriptNames();
			for(vector<string>::iterator it = scriptNames.begin(); it != scriptNames.end(); it++)
			{
				behaviorBox->addItem((QString)(*it).c_str());
			}

			connect(behaviorBox, SIGNAL(notifyParent(QTreeWidgetItem *, bool)), this, SLOT(portTreeWidget_comboBoxChanged(QTreeWidgetItem *, bool)));

			item->setFlags(item->flags() | Qt::ItemIsEditable);

			typeBox->setCurrentIndex(typeBox->findText(pr.m_type.c_str()));

			behaviorBox->setCurrentIndex(behaviorBox->findText(pr.m_behavior.c_str()));

			ui.portTreeWidget->setItemWidget(item, 1, typeBox);
			ui.portTreeWidget->setItemWidget(item, 2, behaviorBox);
			ui.portTreeWidget->setCurrentItem(item);
			pair<string, pair<bool, double> > portPair;
			portPair.first = pr.m_portName;
			portPair.second.first = false;
			portPair.second.second = 0;

			bool conflict = false;
			for(uint i = 0; i < p.m_ports.size(); i++)
			{
				Port temp = m_honeydConfig->m_ports[p.m_ports[i].first];
				if(!pr.m_portNum.compare(temp.m_portNum) && !pr.m_type.compare(temp.m_type))
				{
					conflict = true;
				}
			}
			if(!conflict)
			{
				p.m_ports.insert(p.m_ports.begin(),portPair);
				m_honeydConfig->m_ports[pr.m_portName] = pr;
				m_honeydConfig->m_profiles[p.m_name] = p;

				portPair.second.first = true;
				vector<NodeProfile> profList;
				for(ProfileTable::iterator it = m_honeydConfig->m_profiles.begin(); it != m_honeydConfig->m_profiles.end(); it++)
				{
					NodeProfile ptemp = it->second;
					while(ptemp.m_parentProfile.compare("") && ptemp.m_parentProfile.compare(p.m_name))
					{
						ptemp = m_honeydConfig->m_profiles[ptemp.m_parentProfile];
					}
					if(!ptemp.m_parentProfile.compare(p.m_name))
					{
						ptemp = it->second;
						conflict = false;
						for(uint i = 0; i < ptemp.m_ports.size(); i++)
						{
							Port temp = m_honeydConfig->m_ports[ptemp.m_ports[i].first];
							if(!pr.m_portNum.compare(temp.m_portNum) && !pr.m_type.compare(temp.m_type))
							{
								conflict = true;
							}
						}
						if(!conflict)
						{
							ptemp.m_ports.insert(ptemp.m_ports.begin(),portPair);
						}
					}
					m_honeydConfig->m_profiles[ptemp.m_name] = ptemp;
				}
			}
			LoadProfileSettings();
			SaveProfileSettings();
		}
		m_loading->unlock();
	}
}

void NovaConfig::on_actionDeletePort_triggered()
{
	if(!m_loading->tryLock())
	{
		return;
	}
	if(!ui.portTreeWidget->selectedItems().empty())
	{
		Port *prt = NULL;
		for(PortTable::iterator it = m_honeydConfig->m_ports.begin(); it != m_honeydConfig->m_ports.end(); it++)
		{
			if(IsPortTreeWidgetItem(it->second.m_portName, ui.portTreeWidget->currentItem()))
			{
				//iterators are copies not the actual items
				prt = &m_honeydConfig->m_ports[it->second.m_portName];
				break;
			}
		}
		NodeProfile *p = &m_honeydConfig->m_profiles[m_currentProfile];
		uint i;
		for(i = 0; i < p->m_ports.size(); i++)
		{
			if(!p->m_ports[i].first.compare(prt->m_portName))
			{
				//Check for inheritance on the deleted port.
				//If valid parent
				if(p->m_parentProfile.compare(""))
				{
					NodeProfile *parent = &m_honeydConfig->m_profiles[p->m_parentProfile];
					bool matched = false;
					//check for the inherited port
					for(uint j = 0; j < parent->m_ports.size(); j++)
					{
						if((!prt->m_type.compare(m_honeydConfig->m_ports[parent->m_ports[j].first].m_type))
								&& (!prt->m_portNum.compare(m_honeydConfig->m_ports[parent->m_ports[j].first].m_portNum)))
						{
							p->m_ports[i].second.first = true;
							p->m_ports[i].first = parent->m_ports[j].first;
							matched = true;
						}
					}
					if(!matched)
					{
						p->m_ports.erase(p->m_ports.begin()+i);
					}
				}
				//If no parent.
				else
				{
					p->m_ports.erase(p->m_ports.begin()+i);
				}

				//Check for children with inherited port.
				for(ProfileTable::iterator it = m_honeydConfig->m_profiles.begin(); it != m_honeydConfig->m_profiles.end(); it++)
				{
					if(!it->second.m_parentProfile.compare(p->m_name))
					{
						for(uint j = 0; j < it->second.m_ports.size(); j++)
						{
							if(!it->second.m_ports[j].first.compare(prt->m_portName) && it->second.m_ports[j].second.first)
							{
								it->second.m_ports.erase(it->second.m_ports.begin()+j);
								break;
							}
						}
					}
				}
				break;
			}
			if(i == p->m_ports.size())
			{
				LOG(ERROR, "Port "+prt->m_portName+" could not be found in profile " + p->m_name+".", "");
				m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->CANNOT_DELETE_PORT, "Port " + prt->m_portName
					+ " cannot be found.");
			}
		}
		LoadProfileSettings();
		SaveProfileSettings();
	}
	m_loading->unlock();
}

void NovaConfig::on_msgTypeListWidget_currentRowChanged()
{
	m_loadingDefaultActions = true;
	int item = ui.msgTypeListWidget->currentRow();

	ui.defaultActionListWidget->clear();
	ui.defaultActionListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
	QListWidgetItem *listItem;

	switch(m_mainwindow->m_prompter->m_registeredMessageTypes[item].type)
	{
		case notifyPrompt:
		case warningPrompt:
		case errorPrompt:
		{
			listItem = new QListWidgetItem("Always Show");
			ui.defaultActionListWidget->insertItem(0, listItem);

			if(m_mainwindow->m_prompter->m_registeredMessageTypes[item].action == CHOICE_SHOW)
			{
				listItem->setSelected(true);
			}

			listItem = new QListWidgetItem("Always Hide");
			ui.defaultActionListWidget->insertItem(1, listItem);

			if(m_mainwindow->m_prompter->m_registeredMessageTypes[item].action == CHOICE_HIDE)
			{
				listItem->setSelected(true);
			}
			break;
		}
		case warningPreventablePrompt:
		case notifyActionPrompt:
		case warningActionPrompt:
		{
			listItem = new QListWidgetItem("Always Show");
			ui.defaultActionListWidget->insertItem(0, listItem);

			if(m_mainwindow->m_prompter->m_registeredMessageTypes[item].action == CHOICE_SHOW)
			{
				listItem->setSelected(true);
			}

			listItem = new QListWidgetItem("Always Yes");
			ui.defaultActionListWidget->insertItem(1, listItem);

			if(m_mainwindow->m_prompter->m_registeredMessageTypes[item].action == CHOICE_DEFAULT)
			{
				listItem->setSelected(true);
			}

			listItem = new QListWidgetItem("Always No");
			ui.defaultActionListWidget->insertItem(2, listItem);

			if(m_mainwindow->m_prompter->m_registeredMessageTypes[item].action == CHOICE_ALT)
			{
				listItem->setSelected(true);
			}
			break;
		}
		case notSet:
		{
			break;
		}
		default:
		{
			break;
		}
	}

	m_loadingDefaultActions = false;
}

void NovaConfig::on_defaultActionListWidget_currentRowChanged()
{
	// If we're still populating the list
	if(m_loadingDefaultActions)
	{
		return;
	}

	QString selected = 	ui.defaultActionListWidget->currentItem()->text();
	messageHandle msgType = (messageHandle)ui.msgTypeListWidget->currentRow();

	if(!selected.compare("Always Show"))
	{
		m_mainwindow->m_prompter->SetDefaultAction(msgType, CHOICE_SHOW);
	}
	else if(!selected.compare("Always Hide"))
	{
		m_mainwindow->m_prompter->SetDefaultAction(msgType, CHOICE_HIDE);
	}
	else if(!selected.compare("Always Yes"))
	{
		m_mainwindow->m_prompter->SetDefaultAction(msgType, CHOICE_DEFAULT);
	}
	else if(!selected.compare("Always No"))
	{
		m_mainwindow->m_prompter->SetDefaultAction(msgType, CHOICE_ALT);
	}
	else
	{
		LOG(ERROR, "Invalid user dialog default action selected, shouldn't get here.", "");
	}
}

// Feature enable/disable stuff
void NovaConfig::AdvanceFeatureSelection()
{
	int nextRow = ui.featureList->currentRow() + 1;
	if(nextRow >= ui.featureList->count())
	{
		nextRow = 0;
	}
	ui.featureList->setCurrentRow(nextRow);
}

void NovaConfig::on_featureEnableButton_clicked()
{
	UpdateFeatureListItem(ui.featureList->currentItem(), '1');
	AdvanceFeatureSelection();
}

void NovaConfig::on_featureDisableButton_clicked()
{
	UpdateFeatureListItem(ui.featureList->currentItem(), '0');
	AdvanceFeatureSelection();
}

void NovaConfig::on_ethernetCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_uptimeCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_personalityCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_dropRateCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_tcpCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_udpCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_icmpCheckBox_stateChanged()
{
	if(m_loading->tryLock())
	{
		SaveProfileSettings();
		LoadProfileSettings();
		m_loading->unlock();
	}
}

void NovaConfig::on_portTreeWidget_itemChanged(QTreeWidgetItem *item)
{
	portTreeWidget_comboBoxChanged(item, true);
}

void NovaConfig::portTreeWidget_comboBoxChanged(QTreeWidgetItem *item,  bool edited)
{
	if(!m_loading->tryLock())
	{
		return;
	}
	//On user action with a valid port, and the user actually changed something
	if((item != NULL) && edited)
	{
		//Ensure the signaling item is selected
		ui.portTreeWidget->setCurrentItem(item);
		NodeProfile p = m_honeydConfig->m_profiles[m_currentProfile];
		string oldPort;
		Port oldPrt;

		oldPort = item->text(0).toStdString()+ "_" + item->text(1).toStdString() + "_" + item->text(2).toStdString();
		oldPrt = m_honeydConfig->m_ports[oldPort];


		//Use the combo boxes to update the hidden text underneath them.
		TreeItemComboBox *qTypeBox = (TreeItemComboBox*)ui.portTreeWidget->itemWidget(item, 1);
		item->setText(1, qTypeBox->currentText());

		TreeItemComboBox *qBehavBox = (TreeItemComboBox*)ui.portTreeWidget->itemWidget(item, 2);
		item->setText(2, qBehavBox->currentText());

		//Generate a unique identifier for a particular protocol, number and behavior combination
		// this is pulled from the recently updated hidden text and reflects any changes
		string portName = item->text(0).toStdString() + "_" + item->text(1).toStdString()
				+ "_" + item->text(2).toStdString();

		Port prt;
		//Locate the port in the table or create the port if it doesn't exist
		if(!m_honeydConfig->m_ports.keyExists(portName))
		{
			prt.m_portName = portName;
			prt.m_portNum = item->text(0).toStdString();
			prt.m_type = item->text(1).toStdString();
			prt.m_behavior = item->text(2).toStdString();
			//If the behavior is actually a script name
			if(prt.m_behavior.compare("open") && prt.m_behavior.compare("reset") && prt.m_behavior.compare("block"))
			{
				prt.m_scriptName = prt.m_behavior;
				prt.m_behavior = "script";
			}
			m_honeydConfig->m_ports[portName] = prt;
		}
		else
		{
			prt = m_honeydConfig->m_ports[portName];
		}

		//Check for port conflicts
		for(uint i = 0; i < p.m_ports.size(); i++)
		{
			Port temp = m_honeydConfig->m_ports[p.m_ports[i].first];
			//If theres a conflict other than with the old port
			if((!(temp.m_portNum.compare(prt.m_portNum))) && (!(temp.m_type.compare(prt.m_type)))
					&& temp.m_portName.compare(oldPort))
			{
				LOG(ERROR, "WARNING: Port number and protocol already used.", "");

				portName = "";
			}
		}

		//If there were no conflicts and the port will be included, insert in sorted location
		if(portName.compare(""))
		{
			uint index;
			pair<string, pair<bool, double> > portPair;

			//Erase the old port from the current profile
			for(index = 0; index < p.m_ports.size(); index++)
			{
				if(!p.m_ports[index].first.compare(oldPort))
				{
					p.m_ports.erase(p.m_ports.begin()+index);
				}
			}

			//If the port number or protocol is different, check for inherited ports
			if((prt.m_portNum.compare(oldPrt.m_portNum) || prt.m_type.compare(oldPrt.m_type))
				&& (m_honeydConfig->m_profiles.keyExists(p.m_parentProfile)))
			{
				NodeProfile parent = m_honeydConfig->m_profiles[p.m_parentProfile];
				for(uint i = 0; i < parent.m_ports.size(); i++)
				{
					Port temp = m_honeydConfig->m_ports[parent.m_ports[i].first];
					//If a parent's port matches the number and protocol of the old port being removed
					if((!(temp.m_portNum.compare(oldPrt.m_portNum))) && (!(temp.m_type.compare(oldPrt.m_type))))
					{
						portPair.first = temp.m_portName;
						portPair.second.first = true;
						portPair.second.second = 0;
						if(index < p.m_ports.size())
						{
							p.m_ports.insert(p.m_ports.begin() + index, portPair);
						}
						else
						{
							p.m_ports.push_back(portPair);
						}
						break;
					}
				}
			}

			//Create the vector item for the profile
			portPair.first = portName;
			portPair.second.first = false;

			//Insert it at the numerically sorted position.
			uint i = 0;
			for(i = 0; i < p.m_ports.size(); i++)
			{
				Port temp = m_honeydConfig->m_ports[p.m_ports[i].first];
				if((atoi(temp.m_portNum.c_str())) < (atoi(prt.m_portNum.c_str())))
				{
					continue;
				}
				break;
			}
			if(i < p.m_ports.size())
			{
				p.m_ports.insert(p.m_ports.begin()+i, portPair);
			}
			else
			{
				p.m_ports.push_back(portPair);
			}

			m_honeydConfig->m_profiles[p.m_name] = p;
			//Check for children who inherit the port
			vector<string> updateList;
			updateList.push_back(m_currentProfile);
			bool changed = true;
			bool valid;

			//While any changes have been made
			while(changed)
			{
				//In this while, no changes have been found
				changed = false;
				//Check profile table for children of currentProfile at it's children and so on
				for(ProfileTable::iterator it = m_honeydConfig->m_profiles.begin(); it != m_honeydConfig->m_profiles.end(); it++)
				{
					//Profile invalid to add to start
					valid = false;
					for(uint i = 0; i < updateList.size(); i++)
					{
						//If the parent is being updated this profile is tenatively valid
						if(!it->second.m_parentProfile.compare(updateList[i]))
						{
							valid = true;
						}
						//If this profile is already in the list, this profile shouldn't be included
						if(!it->second.m_name.compare(updateList[i]))
						{
							valid = false;
							break;
						}
					}
					//A valid profile is stored in the list
					if(valid)
					{
						NodeProfile ptemp = it->second;
						//Check if we inherited the old port, and delete it if so
						for(uint i = 0; i < ptemp.m_ports.size(); i++)
						{
							if(!ptemp.m_ports[i].first.compare(oldPort) && ptemp.m_ports[i].second.first)
							{
								ptemp.m_ports.erase(ptemp.m_ports.begin()+i);
							}
						}

						NodeProfile parentTemp = m_honeydConfig->m_profiles[ptemp.m_parentProfile];

						//insert any ports the parent has that doesn't conflict
						for(uint i = 0; i < parentTemp.m_ports.size(); i++)
						{
							bool conflict = false;
							Port pr = m_honeydConfig->m_ports[parentTemp.m_ports[i].first];
							//Check the child for conflicts
							for(uint j = 0; j < ptemp.m_ports.size(); j++)
							{
								Port temp = m_honeydConfig->m_ports[ptemp.m_ports[j].first];
								if(!temp.m_portNum.compare(pr.m_portNum) && !temp.m_type.compare(pr.m_type))
								{
									conflict = true;
								}
							}
							if(!conflict)
							{
								portPair.first = pr.m_portName;
								portPair.second.first = true;
								//Insert it at the numerically sorted position.
								uint j = 0;
								for(j = 0; j < ptemp.m_ports.size(); j++)
								{
									Port temp = m_honeydConfig->m_ports[ptemp.m_ports[j].first];
									if((atoi(temp.m_portNum.c_str())) < (atoi(pr.m_portNum.c_str())))
										continue;

									break;
								}
								if(j < ptemp.m_ports.size())
								{
									ptemp.m_ports.insert(ptemp.m_ports.begin()+j, portPair);
								}
								else
								{
									ptemp.m_ports.push_back(portPair);
								}
							}
						}
						updateList.push_back(ptemp.m_name);
						m_honeydConfig->m_profiles[ptemp.m_name] = ptemp;
						//Since we found at least one profile this iteration flag as changed
						// so we can check for it's children
						changed = true;
						break;
					}
				}
			}
		}
		LoadProfileSettings();
		SaveProfileSettings();
		ui.portTreeWidget->setFocus(Qt::OtherFocusReason);
		m_loading->unlock();
		LoadAllProfiles();
	}
	//On user interaction with a valid port item without any changes
	else if(!edited && (item != NULL))
	{
		//Select the valid port item under the associated combo box
		ui.portTreeWidget->setFocus(Qt::OtherFocusReason);
		ui.portTreeWidget->setCurrentItem(item);
		m_loading->unlock();
	}
	else
	{
		m_loading->unlock();
	}
}

QListWidgetItem *NovaConfig::GetFeatureListItem(QString name, char enabled)
{
	QListWidgetItem *newFeatureEntry = new QListWidgetItem();
	name.prepend("+  ");
	newFeatureEntry->setText(name);
	UpdateFeatureListItem(newFeatureEntry, enabled);

	return newFeatureEntry;
}

void NovaConfig::onFeatureClick(const QPoint & pos)
{
	QPoint globalPos = ui.featureList->mapToGlobal(pos);
	QModelIndex t = ui.featureList->indexAt(pos);

	// Handle clicking on a row that isn't populated
	if(t.row() < 0 || t.row() >= DIM)
	{
		return;
	}

	// Make the menu
    QMenu myMenu(this);
    myMenu.addAction(new QAction("Enable", this));
    myMenu.addAction(new QAction("Disable", this));

    // Figure out what the user selected
    QAction *selectedItem = myMenu.exec(globalPos);
    if(selectedItem)
    {
		if(selectedItem->text() == "Enable")
		{
			UpdateFeatureListItem(ui.featureList->item(t.row()), '1');
		}
		else if(selectedItem->text() == "Disable")
		{
			UpdateFeatureListItem(ui.featureList->item(t.row()), '0');
		}
    }
}

void NovaConfig::UpdateFeatureListItem(QListWidgetItem *newFeatureEntry, char enabled)
{
	QBrush *enabledColor = new QBrush(QColor("black"));
	QBrush *disabledColor = new QBrush(QColor("grey"));
	QString name = newFeatureEntry->text().remove(0, 2);

	if(enabled == '1')
	{
		name.prepend(QString("+ "));
		newFeatureEntry->setForeground(*enabledColor);
	}
	else
	{
		name.prepend(QString("- "));
		newFeatureEntry->setForeground(*disabledColor);
	}
	newFeatureEntry->setText(name);
}


//Load Personality choices from nmap fingerprints file
void NovaConfig::DisplayNmapPersonalityWindow()
{
	m_retVal = "";
	NovaComplexDialog *NmapPersonalityWindow = new NovaComplexDialog(PersonalityDialog, &m_retVal, this);
	NmapPersonalityWindow->exec();
	if(m_retVal.compare(""))
	{
		ui.personalityEdit->setText((QString)m_retVal.c_str());
	}
}


//Load MAC vendor prefix choices from nmap mac prefix file
bool NovaConfig::DisplayMACPrefixWindow()
{
	/*m_retVal = "";
	NovaComplexDialog *MACPrefixWindow = new NovaComplexDialog(
			MACDialog, &m_retVal, this, ui.ethernetEdit->text().toStdString());
	MACPrefixWindow->exec();
	if(m_retVal.compare(""))
	{
		ui.ethernetEdit->setText((QString)m_retVal.c_str());

		//If there is no change in vendor, nothing left to be done.
		for(uint i = 0; i < m_honeydConfig->m_profiles[m_currentProfile].m_ethernetVendors.size(); i++)
		{
			//If we find the vendor in the existing vector
			if(!m_honeydConfig->m_profiles[m_currentProfile].m_ethernetVendors[i].first.compare(m_retVal))
			{
				return true;
			}
		}
		m_honeydConfig->UpdateMacAddressesOfProfileNodes(m_currentProfile);
	}*/
	return false;
}

void NovaConfig::LoadNovadPreferences()
{
	stringstream ss;

	//Clear old buttons
	QList<QAbstractButton *> radioButtons = m_radioButtons->buttons();
	while(!radioButtons.isEmpty())
	{
		delete radioButtons.takeFirst();
	}
	QList<QAbstractButton *> checkBoxes = m_interfaceCheckBoxes->buttons();
	while(!checkBoxes.isEmpty())
	{
		delete checkBoxes.takeFirst();
	}
	delete m_radioButtons;
	delete m_interfaceCheckBoxes;

	//Set up button groups
	m_radioButtons = new QButtonGroup(ui.loopbackGroupBox);
	m_radioButtons->setExclusive(true);
	m_interfaceCheckBoxes = new QButtonGroup(ui.interfaceGroupBox);

	vector<string> loopbackList = Config::Inst()->GetIPv4LoopbackInterfaceList();
	for(uint i = 0; i < loopbackList.size(); i++)
	{
		QRadioButton *radioButton = new QRadioButton(QString(loopbackList[i].c_str()), ui.loopbackGroupBox);
		radioButton->setObjectName(QString(loopbackList[i].c_str()));
		m_radioButtons->addButton(radioButton);
		ui.loopbackGroupBoxVLayout->addWidget(radioButton);
		radioButtons.push_back(radioButton);
	}
	vector<string> hostInterfaceList = Config::Inst()->GetIPv4HostInterfaceList();
	for(uint i = 0; i < hostInterfaceList.size(); i++)
	{
		QCheckBox *checkBox = new QCheckBox(QString(hostInterfaceList[i].c_str()), ui.interfaceGroupBox);
		checkBox->setObjectName(QString(hostInterfaceList[i].c_str()));
		m_interfaceCheckBoxes->addButton(checkBox);
		ui.interfaceGroupBoxVLayout->addWidget(checkBox);
		checkBoxes.push_back(checkBox);
	}

	//Set mutual exclusion for forcing at least one interface selection
	if(checkBoxes.size() >= 1)
	{
		m_interfaceCheckBoxes->setExclusive(false);
	}
	else
	{
		m_interfaceCheckBoxes->setExclusive(true);
	}

	//Select the loopback
	{
		QString doppIf = QString::fromStdString(Config::Inst()->GetDoppelInterface());
		QRadioButton *checkObj = ui.loopbackGroupBox->findChild<QRadioButton *>(doppIf);
		if(checkObj != NULL)
		{
			checkObj->setChecked(true);
		}
	}
	vector<string> ifList = Config::Inst()->GetInterfaces();
	while(!ifList.empty())
	{
		QCheckBox *checkObj = ui.interfaceGroupBox->findChild<QCheckBox *>(QString::fromStdString(ifList.back()));
		if(checkObj != NULL)
		{
			checkObj->setChecked(true);
		}
		ifList.pop_back();
	}

	ui.useAllIfCheckBox->setChecked(Config::Inst()->GetUseAllInterfaces());
	ui.useAnyLoopbackCheckBox->setChecked(Config::Inst()->GetUseAnyLoopback());
	ui.dataEdit->setText(QString::fromStdString(Config::Inst()->GetPathTrainingFile()));
	ui.saAttemptsMaxEdit->setText(QString::number(Config::Inst()->GetSaMaxAttempts()));
	ui.saAttemptsTimeEdit->setText(QString::number(Config::Inst()->GetSaSleepDuration()));
	ui.saPortEdit->setText(QString::number(Config::Inst()->GetSaPort()));
	ui.ceIntensityEdit->setText(QString::number(Config::Inst()->GetK()));
	ui.ceErrorEdit->setText(QString::number(Config::Inst()->GetEps()));
	ui.ceFrequencyEdit->setText(QString::number(Config::Inst()->GetClassificationTimeout()));
	ui.ceThresholdEdit->setText(QString::number(Config::Inst()->GetClassificationThreshold()));
	ui.tcpTimeoutEdit->setText(QString::number(Config::Inst()->GetTcpTimout()));
	ui.tcpFrequencyEdit->setText(QString::number(Config::Inst()->GetTcpCheckFreq()));

	ui.trainingCheckBox->setChecked(Config::Inst()->GetIsTraining());
	switch(Config::Inst()->GetHaystackStorage())
	{
		case 'M':
		{
			ui.hsSaveTypeComboBox->setCurrentIndex(1);
			ui.hsSummaryGroupBox->setEnabled(false);
			ui.nodesGroupBox->setEnabled(false);
			ui.profileGroupBox->setEnabled(false);
			ui.hsConfigEdit->setText((QString)Config::Inst()->GetPathConfigHoneydUser().c_str());
			break;
		}
		default:
		{
			ui.hsSaveTypeComboBox->setCurrentIndex(0);
			ui.hsConfigEdit->setText((QString)Config::Inst()->GetPathConfigHoneydHS().c_str());
			break;
		}
	}

	//Set first ip byte
	string ip = Config::Inst()->GetDoppelIp();
	int index = ip.find_first_of('.');
	ui.dmIPSpinBox_0->setValue(atoi(ip.substr(0,ip.find_first_of('.')).c_str()));

	//Set second ip byte
	ip = ip.substr(index+1, ip.size());
	index = ip.find_first_of('.');
	ui.dmIPSpinBox_1->setValue(atoi(ip.substr(0,ip.find_first_of('.')).c_str()));

	//Set third ip byte
	ip = ip.substr(index+1, ip.size());
	index = ip.find_first_of('.');
	ui.dmIPSpinBox_2->setValue(atoi(ip.substr(0,ip.find_first_of('.')).c_str()));

	//Set fourth ip byte
	ip = ip.substr(index+1, ip.size());
	index = ip.find_first_of('.');
	ui.dmIPSpinBox_3->setValue(atoi(ip.c_str()));

	ui.pcapCheckBox->setChecked(Config::Inst()->GetReadPcap());
	ui.pcapGroupBox->setEnabled(ui.pcapCheckBox->isChecked());
	ui.pcapEdit->setText((QString)Config::Inst()->GetPathPcapFile().c_str());
	ui.liveCapCheckBox->setChecked(Config::Inst()->GetGotoLive());
	{
		string featuresEnabled = Config::Inst()->GetEnabledFeatures();
		ui.featureList->clear();
		// Populate the list, row order is based on dimension macros

		for(int i = 0; i < DIM; i++)
		{
			ui.featureList->insertItem(i, GetFeatureListItem(QString::fromStdString(FeatureSet::m_featureNames[i]),featuresEnabled.at(i)));
		}
	}
}

//Draws the current honeyd configuration
void NovaConfig::LoadHaystackConfiguration()
{
	//Draws all profile heriarchy
	m_loading->unlock();
	LoadAllProfiles();
	//Draws all node heirarchy
	LoadAllNodes();
	ui.nodeTreeWidget->expandAll();
	ui.hsNodeTreeWidget->expandAll();
}

/************************************************
  Browse file system dialog box signals
 ************************************************/

void NovaConfig::on_pcapButton_clicked()
{
	//Gets the current path location
	QDir path = QDir::current();
	//Opens a cross-platform dialog box
	QString fileName = QFileDialog::getExistingDirectory(this, tr("Open Packet Capture"),  path.path());

	//Gets the relative path using the absolute path in fileName and the current path
	if(fileName != NULL)
	{
		fileName = path.relativeFilePath(fileName);
		ui.pcapEdit->setText(fileName);
	}
}

void NovaConfig::on_dataButton_clicked()
{
	//Gets the current path location
	QDir path = QDir::current();
	//Opens a cross-platform dialog box
	QString fileName = QFileDialog::getOpenFileName(this, tr("Open Data File"),  path.path(), tr("Text Files (*.txt)"));

	//Gets the relative path using the absolute path in fileName and the current path
	if(fileName != NULL)
	{
		fileName = path.relativeFilePath(fileName);
		ui.dataEdit->setText(fileName);
	}
}

void NovaConfig::on_hsConfigButton_clicked()
{
	//Gets the current path location
	QDir path = QDir::current();
	//Opens a cross-platform dialog box
	QString fileName = QFileDialog::getSaveFileName(this, tr("Choose a File Location"),
			path.path(), tr("Text Files (*.config)"));
	QFile file(fileName);
	//Gets the relative path using the absolute path in fileName and the current path
	if(!file.exists())
	{
		file.open(file.ReadWrite);
		file.close();
	}
	fileName = file.fileName();
	fileName = path.relativeFilePath(fileName);
	ui.hsConfigEdit->setText(fileName);
	//LoadHaystackConfiguration();
}

/************************************************
  General Preferences GUI Signals
 ************************************************/

//Stores all changes and closes the window
void NovaConfig::on_okButton_clicked()
{
	m_loading->lock();
	SaveProfileSettings();
	if(!SaveConfigurationToFile())
	{
		LOG(ERROR, "Error writing to Nova config file.", "");
		m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->CONFIG_WRITE_FAIL,
			"Error: Unable to write to NOVA configuration file");
		this->close();
	}
	LoadNovadPreferences();

	//Clean up unused ports
	m_honeydConfig->CleanPorts();
	//Saves the current configuration to XML files
	m_honeydConfig->SaveAllTemplates();
	m_honeydConfig->WriteHoneydConfiguration(Config::Inst()->GetUserPath());

	LoadHaystackConfiguration();
	m_mainwindow->InitConfiguration();

	m_loading->unlock();
	this->close();
}

//Stores all changes the repopulates the window
void NovaConfig::on_applyButton_clicked()
{
	m_loading->lock();
	SaveProfileSettings();
	if(!SaveConfigurationToFile())
	{
		LOG(ERROR, "Error writing to Nova config file.", "");

		m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->CONFIG_WRITE_FAIL,
			"Error: Unable to write to NOVA configuration file ");
		this->close();
	}
	//Reloads NOVAConfig preferences to assert concurrency
	LoadNovadPreferences();

	//Clean up unused ports
	m_honeydConfig->CleanPorts();
	//Saves the current configuration to XML files
	m_honeydConfig->SaveAllTemplates();
	m_honeydConfig->WriteHoneydConfiguration(Config::Inst()->GetUserPath());

	//Reloads honeyd configuration to assert concurrency
	LoadHaystackConfiguration();
	m_mainwindow->InitConfiguration();
	m_loading->unlock();
}

void NovaConfig::SetSelectedNode(string node)
{
	m_currentNode = node;
}

bool NovaConfig::SaveConfigurationToFile()
{
	string line, prefix;
	stringstream ss;
	for(uint i = 0; i < DIM; i++)
	{
		char state = ui.featureList->item(i)->text().at(0).toAscii();
		if(state == '+')
		{
			ss << 1;
		}
		else
		{
			ss << 0;
		}
	}
	Config::Inst()->SetIsDmEnabled(ui.dmCheckBox->isChecked());
	Config::Inst()->SetIsTraining(ui.trainingCheckBox->isChecked());
	Config::Inst()->SetPathTrainingFile(this->ui.dataEdit->displayText().toStdString());
	Config::Inst()->SetSaSleepDuration(this->ui.saAttemptsTimeEdit->displayText().toDouble());
	Config::Inst()->SetSaMaxAttempts(this->ui.saAttemptsMaxEdit->displayText().toInt());
	Config::Inst()->SetSaPort(this->ui.saPortEdit->displayText().toInt());
	Config::Inst()->SetK(this->ui.ceIntensityEdit->displayText().toInt());
	Config::Inst()->SetEps(this->ui.ceErrorEdit->displayText().toDouble());
	Config::Inst()->SetClassificationTimeout(this->ui.ceFrequencyEdit->displayText().toInt());
	Config::Inst()->SetClassificationThreshold(this->ui.ceThresholdEdit->displayText().toDouble());
	Config::Inst()->SetEnabledFeatures(ss.str());

	QList<QAbstractButton *> qButtonList = m_interfaceCheckBoxes->buttons();
	Config::Inst()->ClearInterfaces();
	Config::Inst()->SetUseAllInterfaces(ui.useAllIfCheckBox->isChecked());

	for(int i = 0; i < qButtonList.size(); i++)
	{
		QCheckBox *checkBoxPtr = (QCheckBox *)qButtonList.at(i);
		if(checkBoxPtr->isChecked())
		{
			Config::Inst()->AddInterface(checkBoxPtr->text().toStdString());
		}
	}

	qButtonList = m_radioButtons->buttons();

	Config::Inst()->SetUseAnyLoopback(ui.useAnyLoopbackCheckBox->isChecked());
	for(int i = 0; i < qButtonList.size(); i++)
	{
		QRadioButton * radioBtnPtr = (QRadioButton *)qButtonList.at(i);
		if(radioBtnPtr->isChecked())
		{
			Config::Inst()->SetDoppelInterface(radioBtnPtr->text().toStdString());
		}
	}

	ss.str("");
	ss << ui.dmIPSpinBox_0->value() << "." << ui.dmIPSpinBox_1->value() << "." << ui.dmIPSpinBox_2->value() << "."
		<< ui.dmIPSpinBox_3->value();
	Config::Inst()->SetDoppelIp(ss.str());

	switch(ui.hsSaveTypeComboBox->currentIndex())
	{
		case 1:
		{
			Config::Inst()->SetHaystackStorage('M');
			Config::Inst()->SetPathConfigHoneydUser(this->ui.hsConfigEdit->displayText().toStdString());
			break;
		}
		case 0:
		default:
		{
			Config::Inst()->SetHaystackStorage('I');
			Config::Inst()->SetPathConfigHoneydHs(this->ui.hsConfigEdit->displayText().toStdString());
			break;
		}
	}

	Config::Inst()->SetTcpTimout(this->ui.tcpTimeoutEdit->displayText().toInt());
	Config::Inst()->SetTcpCheckFreq(this->ui.tcpFrequencyEdit->displayText().toInt());
	Config::Inst()->SetPathPcapFile(ui.pcapEdit->displayText().toStdString());
	Config::Inst()->SetReadPcap(ui.pcapCheckBox->isChecked());
	Config::Inst()->SetGotoLive(ui.liveCapCheckBox->isChecked());

	return (Config::Inst()->SaveUserConfig() && Config::Inst()->SaveConfig());
}

//Exit the window and ignore any changes since opening or apply was pressed
void NovaConfig::on_cancelButton_clicked()
{
	//Reloads from NOVAConfig
	LoadNovadPreferences();
	this->close();
}

void NovaConfig::on_defaultsButton_clicked()
{
	//Currently just restores to last save changes
	//We should really identify default values and write those while maintaining
	//Critical values that might cause nova to break if changed.

	//Reloads from NOVAConfig
	LoadNovadPreferences();
	//Has NovaGUI reload honeyd configuration from XML files
	m_honeydConfig->LoadAllTemplates();
	m_loading->lock();
	//Populates honeyd configuration pulled
	LoadHaystackConfiguration();
	m_loading->unlock();
}

void NovaConfig::on_menuTreeWidget_itemSelectionChanged()
{
	QTreeWidgetItem *item = ui.menuTreeWidget->selectedItems().first();

	//If last window was the profile window, save any changes
	if(m_editingItems && m_honeydConfig->m_profiles.size())
	{
		SaveProfileSettings();
	}
	m_editingItems = false;

	//If it's a top level item the page corresponds to their index in the tree
	//Any new top level item should be inserted at the corresponding index and the defines
	// for the lower level items will need to be adjusted appropriately.
	int i = ui.menuTreeWidget->indexOfTopLevelItem(item);

	if(i != -1)
	{
		ui.stackedWidget->setCurrentIndex(i);
	}
	//If the item is a child of a top level item, find out what type of item it is
	else
	{
		//Find the parent and keep getting parents until we have a top level item
		QTreeWidgetItem *parent = item->parent();
		while(ui.menuTreeWidget->indexOfTopLevelItem(parent) == -1)
		{
			parent = parent->parent();
		}
		if(ui.menuTreeWidget->indexOfTopLevelItem(parent) == HAYSTACK_MENU_INDEX)
		{
			//If the 'Nodes' Item
			if(parent->child(NODE_INDEX) == item)
			{
				ui.stackedWidget->setCurrentIndex(ui.menuTreeWidget->topLevelItemCount());
			}
			//If the 'Profiles' item
			else if(parent->child(PROFILE_INDEX) == item)
			{
				m_editingItems = true;
				ui.stackedWidget->setCurrentIndex(ui.menuTreeWidget->topLevelItemCount()+1);
			}
		}
		else
		{
			LOG(ERROR, "Unable to set stackedWidget page index from menuTreeWidgetItem", "");
		}
	}
}

/************************************************
  Preference Menu Specific GUI Signals
 ************************************************/

//Enable DM checkbox, action syncs Node displayed in haystack as disabled/enabled
void NovaConfig::on_dmCheckBox_stateChanged(int state)
{
	if(!m_loading->tryLock())
	{
		return;
	}

	if(state)
	{
		m_honeydConfig->EnableNode("Doppelganger");
		Config::Inst()->SetIsDmEnabled(true);
	}
	else
	{
		m_honeydConfig->DisableNode("Doppelganger");
		Config::Inst()->SetIsDmEnabled(false);
	}

	m_loading->unlock();
	LoadAllNodes();
}

/**********************
  Pcap Menu GUI Signals
 **********************/

// Enables or disables options specific for reading from pcap file
void NovaConfig::on_pcapCheckBox_stateChanged(int state)
{
	ui.pcapGroupBox->setEnabled(state);
}

/******************************************
  Profile Menu GUI Functions
 ******************************************/

//Combo box signal for changing the uptime behavior
void NovaConfig::on_uptimeBehaviorComboBox_currentIndexChanged(int index)
{
	//If uptime behavior = random in range
	if(index)
	{
		ui.uptimeRangeEdit->setText(ui.uptimeEdit->displayText());
	}

	ui.uptimeRangeLabel->setVisible(index);
	ui.uptimeRangeEdit->setVisible(index);
}

void NovaConfig::SaveProfileSettings()
{
	QTreeWidgetItem *item = NULL;
	struct Port pr;

	//Saves any modifications to the last selected profile object.
	if(m_honeydConfig->m_profiles.keyExists(m_currentProfile))
	{
		NodeProfile p = m_honeydConfig->m_profiles[m_currentProfile];
		//currentProfile->name is set in updateProfile
		//XXX implement multiple ethernet vendor support
		//p.m_ethernetVendors[0].first = ui.ethernetEdit->displayText().toStdString();
		//p.m_ethernetVendors[0].second = 100;
		p.m_tcpAction = ui.tcpActionComboBox->currentText().toStdString();
		p.m_udpAction = ui.udpActionComboBox->currentText().toStdString();
		p.m_icmpAction = ui.icmpActionComboBox->currentText().toStdString();


		p.m_uptimeMin = ui.uptimeEdit->displayText().toStdString();
		//If random in range behavior
		if(ui.uptimeBehaviorComboBox->currentIndex())
		{
			p.m_uptimeMax = ui.uptimeRangeEdit->displayText().toStdString();
		}
		//If flat behavior
		else
		{
			p.m_uptimeMax = p.m_uptimeMin;
		}
		p.m_personality = ui.personalityEdit->displayText().toStdString();
		stringstream ss;
		ss << ui.dropRateSlider->value();
		p.m_dropRate = ss.str();

		//Save the port table
		for(int i = 0; i < ui.portTreeWidget->topLevelItemCount(); i++)
		{
			pr = m_honeydConfig->m_ports[p.m_ports[i].first];
			item = ui.portTreeWidget->topLevelItem(i);
			pr.m_portNum = item->text(0).toStdString();
			TreeItemComboBox *qTypeBox = (TreeItemComboBox*)ui.portTreeWidget->itemWidget(item, 1);
			TreeItemComboBox *qBehavBox = (TreeItemComboBox*)ui.portTreeWidget->itemWidget(item, 2);
			pr.m_type = qTypeBox->currentText().toStdString();
			if(!pr.m_portNum.compare(""))
			{
				continue;
			}
			pr.m_behavior = qBehavBox->currentText().toStdString();
			//If the behavior names a script
			if(pr.m_behavior.compare("open") && pr.m_behavior.compare("reset") && pr.m_behavior.compare("block"))
			{
				pr.m_behavior = "script";
				pr.m_scriptName = qBehavBox->currentText().toStdString();
			}
			if(!pr.m_behavior.compare("script"))
			{
				pr.m_portName = pr.m_portNum + "_" +pr.m_type + "_" + pr.m_scriptName;
			}
			else
			{
				pr.m_portName = pr.m_portNum + "_" +pr.m_type + "_" + pr.m_behavior;
			}

			p.m_ports[i].first = pr.m_portName;
			m_honeydConfig->m_ports[p.m_ports[i].first] = pr;
			p.m_ports[i].second.first = item->font(0).italic();
			p.m_ports[i].second.second = ((QSlider *)ui.portTreeWidget->itemWidget(item, 3))->value();
		}
		m_honeydConfig->m_profiles[m_currentProfile] = p;
		SaveInheritedProfileSettings();
		m_honeydConfig->UpdateProfile(m_currentProfile);
	}
}

void NovaConfig::SaveInheritedProfileSettings()
{
	NodeProfile p = m_honeydConfig->m_profiles[m_currentProfile];

	p.m_inherited[TCP_ACTION] = ui.tcpCheckBox->isChecked();
	if(ui.tcpCheckBox->isChecked())
	{
		p.m_tcpAction = m_honeydConfig->m_profiles[p.m_parentProfile].m_tcpAction;
	}
	p.m_inherited[UDP_ACTION] = ui.udpCheckBox->isChecked();
	if(ui.udpCheckBox->isChecked())
	{
		p.m_udpAction = m_honeydConfig->m_profiles[p.m_parentProfile].m_udpAction;
	}
	p.m_inherited[ICMP_ACTION] = ui.icmpCheckBox->isChecked();
	if(ui.icmpCheckBox->isChecked())
	{
		p.m_icmpAction = m_honeydConfig->m_profiles[p.m_parentProfile].m_icmpAction;
	}
	p.m_inherited[ETHERNET] = ui.ethernetCheckBox->isChecked();
	if(ui.ethernetCheckBox->isChecked())
	{
		p.m_ethernetVendors = m_honeydConfig->m_profiles[p.m_parentProfile].m_ethernetVendors;
	}
	p.m_inherited[UPTIME] = ui.uptimeCheckBox->isChecked();
	if(ui.uptimeCheckBox->isChecked())
	{
		p.m_uptimeMin = m_honeydConfig->m_profiles[p.m_parentProfile].m_uptimeMin;
		p.m_uptimeMax = m_honeydConfig->m_profiles[p.m_parentProfile].m_uptimeMax;
		if(m_honeydConfig->m_profiles[p.m_parentProfile].m_uptimeMin == m_honeydConfig->m_profiles[p.m_parentProfile].m_uptimeMax)
		{
			ui.uptimeBehaviorComboBox->setCurrentIndex(1);
		}
		else
		{
			ui.uptimeBehaviorComboBox->setCurrentIndex(0);
		}
	}

	p.m_inherited[PERSONALITY] = ui.personalityCheckBox->isChecked();
	if(ui.personalityCheckBox->isChecked())
	{
		p.m_personality = m_honeydConfig->m_profiles[p.m_parentProfile].m_personality;
	}
	p.m_inherited[DROP_RATE] = ui.dropRateCheckBox->isChecked();
	if(ui.dropRateCheckBox->isChecked())
	{
		p.m_dropRate = m_honeydConfig->m_profiles[p.m_parentProfile].m_dropRate;
	}
	m_honeydConfig->m_profiles[m_currentProfile] = p;

}
//Removes a profile, all of it's children and any nodes that currently use it
void NovaConfig::DeleteProfile(string name)
{
	QTreeWidgetItem *item = NULL, *temp = NULL;
	//If there is at least one other profile after deleting all children
	if(m_honeydConfig->m_profiles.size() > 1)
	{
		//Get the current profile item
		item = GetProfileTreeWidgetItem(name);
		//Try to find another profile below it
		temp = ui.profileTreeWidget->itemBelow(item);

		//If no profile below, find a profile above
		if(temp == NULL)
		{
			item = ui.profileTreeWidget->itemAbove(item);
		}
		else
		{
			item = temp; //if profile below was found
		}
	}
	//Remove the current profiles tree widget items
	ui.profileTreeWidget->removeItemWidget(GetProfileTreeWidgetItem(name), 0);
	ui.hsProfileTreeWidget->removeItemWidget(GetProfileHsTreeWidgetItem(name), 0);
	//If an item was found for a new selection
	if(item != NULL)
	{	//Set the current selection
		m_currentProfile = item->text(0).toStdString();
	}
	//If no profiles remain
	else
	{	//No selection
		m_currentProfile = "";
	}
	m_honeydConfig->DeleteProfile(name);

	//Redraw honeyd configuration to reflect changes
	LoadHaystackConfiguration();
}

//Populates the window with the selected profile's options
void NovaConfig::LoadProfileSettings()
{
	Port pr;
	QTreeWidgetItem *item = NULL;
	//If the selected profile can be found
	if(m_honeydConfig->m_profiles.keyExists(m_currentProfile))
	{
		LoadInheritedProfileSettings();
		//Clear the tree widget and load new selections
		QTreeWidgetItem *portCurrentItem;
		portCurrentItem = ui.portTreeWidget->currentItem();
		string portCurrentString = "";
		if(portCurrentItem != NULL)
		{
			portCurrentString = portCurrentItem->text(0).toStdString()+ "_"
				+ portCurrentItem->text(1).toStdString() + "_" + portCurrentItem->text(2).toStdString();
		}

		ui.portTreeWidget->clear();

		NodeProfile *p = &m_honeydConfig->m_profiles[m_currentProfile];
		//Set the variables of the profile
		ui.profileEdit->setText((QString)p->m_name.c_str());
		ui.profileEdit->setEnabled(true);

		ui.tcpActionComboBox->setCurrentIndex(ui.tcpActionComboBox->findText(p->m_tcpAction.c_str()));
		ui.udpActionComboBox->setCurrentIndex(ui.udpActionComboBox->findText(p->m_udpAction.c_str()));
		ui.icmpActionComboBox->setCurrentIndex(ui.icmpActionComboBox->findText(p->m_icmpAction.c_str()));
		ui.uptimeEdit->setText((QString)p->m_uptimeMin.c_str());
		if(p->m_uptimeMax != p->m_uptimeMin)
		{
			ui.uptimeBehaviorComboBox->setCurrentIndex(1);
			ui.uptimeRangeEdit->setText((QString)p->m_uptimeMax.c_str());
			ui.uptimeRangeLabel->setVisible(true);
			ui.uptimeRangeEdit->setVisible(true);
		}
		else
		{
			ui.uptimeBehaviorComboBox->setCurrentIndex(0);
			ui.uptimeRangeLabel->setVisible(false);
			ui.uptimeRangeEdit->setVisible(false);
		}
		ui.personalityEdit->setText((QString)p->m_personality.c_str());
		if(p->m_dropRate.size())
		{
			ui.dropRateSlider->setValue(atoi(p->m_dropRate.c_str()));
			stringstream ss;
			ss << ui.dropRateSlider->value() << "%";
			ui.dropRateSetting->setText((QString)ss.str().c_str());
		}
		else
		{
			ui.dropRateSetting->setText("0%");
			ui.dropRateSlider->setValue(0);
		}
		ui.numNodesSpinBox->setValue(p->m_nodeKeys.size());

		ui.ethVendorTableWidget->clearContents();
		//Populate the Ethernet Vender Table
		for(uint i = 0; i < p->m_ethernetVendors.size(); i++)
		{
			if((ui.ethVendorTableWidget->rowCount()-1) < (int)i)
			{
				ui.ethVendorTableWidget->insertRow(i);
			}
			//*WARNING - 'item' is a QTreeWidgetItem pointer in the next scope up, watch var usage
			QTableWidgetItem *item = new QTableWidgetItem();
			item->setText((QString)p->m_ethernetVendors[i].first.c_str());
			ui.ethVendorTableWidget->setItem(i, 0, item);

			item = new QTableWidgetItem();
			ui.ethVendorTableWidget->setItem(i, 1, item);

			QSpinBox *numNodesSpinBox = new QSpinBox();
			numNodesSpinBox->setMaximum(100);
			numNodesSpinBox->setMinimum(0);
			numNodesSpinBox->setValue(((uint)(p->m_nodeKeys.size()*(p->m_ethernetVendors[i].second/((double)100)))));
			numNodesSpinBox->setAlignment(Qt::AlignCenter);
			ui.ethVendorTableWidget->setCellWidget(i, 1, numNodesSpinBox);

			item = new QTableWidgetItem();
			ui.ethVendorTableWidget->setItem(i, 2, item);

			QSlider *ethDistribSlider = new QSlider();
			ethDistribSlider->setMaximum(100);
			ethDistribSlider->setMinimum(0);
			ethDistribSlider->setOrientation(Qt::Horizontal);
			ethDistribSlider->setValue((uint)p->m_ethernetVendors[i].second);
			ui.ethVendorTableWidget->setCellWidget(i, 2, ethDistribSlider);
		}
		while(ui.ethVendorTableWidget->rowCount() > (int)p->m_ethernetVendors.size())
		{
			ui.ethVendorTableWidget->removeRow(ui.ethVendorTableWidget->rowCount()-1);
		}
		for(int i = 0; i < ui.ethVendorTableWidget->columnCount(); i++)
		{
			ui.ethVendorTableWidget->resizeColumnToContents(i);
		}

		//Populate the port table
		for(uint i = 0; i < p->m_ports.size(); i++)
		{
			pr = m_honeydConfig->m_ports[p->m_ports[i].first];

			//These don't need to be deleted because the clear function
			// and destructor of the tree widget does that already.
			item = new QTreeWidgetItem();
			ui.portTreeWidget->insertTopLevelItem(i, item);

			item->setText(0,(QString)pr.m_portNum.c_str());
			item->setText(1,(QString)pr.m_type.c_str());
			item->setTextAlignment(1, Qt::AlignCenter);
			item->setTextAlignment(2, Qt::AlignCenter);
			item->setTextAlignment(3, Qt::AlignCenter);

			if(!pr.m_behavior.compare("script"))
			{
				item->setText(2, (QString)pr.m_scriptName.c_str());
			}
			else
			{
				item->setText(2,(QString)pr.m_behavior.c_str());
			}

			QFont tempFont;
			tempFont = QFont(item->font(0));
			tempFont.setItalic(p->m_ports[i].second.first);
			item->setFont(0,tempFont);
			item->setFont(1,tempFont);
			item->setFont(2,tempFont);

			TreeItemComboBox *typeBox = new TreeItemComboBox(this, item);
			typeBox->addItem("TCP");
			typeBox->addItem("UDP");
			typeBox->setItemText(0, "TCP");
			typeBox->setItemText(1, "UDP");
			typeBox->setFont(tempFont);

			connect(typeBox, SIGNAL(notifyParent(QTreeWidgetItem *, bool)), this, SLOT(portTreeWidget_comboBoxChanged(QTreeWidgetItem *, bool)));

			TreeItemComboBox *behaviorBox = new TreeItemComboBox(this, item);
			behaviorBox->addItem("reset");
			behaviorBox->addItem("open");
			behaviorBox->addItem("block");
			behaviorBox->insertSeparator(3);

			vector<string> scriptNames = m_honeydConfig->GetScriptNames();
			for(vector<string>::iterator it = scriptNames.begin(); it != scriptNames.end(); it++)
			{
				behaviorBox->addItem((QString)(*it).c_str());
			}

			behaviorBox->setFont(tempFont);
			connect(behaviorBox, SIGNAL(notifyParent(QTreeWidgetItem *, bool)), this, SLOT(portTreeWidget_comboBoxChanged(QTreeWidgetItem *, bool)));

			if(p->m_ports[i].second.first)
			{
				item->setFlags(item->flags() & ~Qt::ItemIsEditable);
			}
			else
			{
				item->setFlags(item->flags() | Qt::ItemIsEditable);
			}

			typeBox->setCurrentIndex(typeBox->findText(pr.m_type.c_str()));

			if(!pr.m_behavior.compare("script"))
			{
				behaviorBox->setCurrentIndex(behaviorBox->findText(pr.m_scriptName.c_str()));
			}
			else
			{
				behaviorBox->setCurrentIndex(behaviorBox->findText(pr.m_behavior.c_str()));
			}

			ui.portTreeWidget->setItemWidget(item, 1, typeBox);
			ui.portTreeWidget->setItemWidget(item, 2, behaviorBox);

			QSlider *portDistribSlider = new QSlider();
			portDistribSlider->setRange(0,100);
			portDistribSlider->setOrientation(Qt::Horizontal);
			ui.portTreeWidget->setItemWidget(item, 3, portDistribSlider);
			portDistribSlider->setValue(p->m_ports[i].second.second/1);

			if(!portCurrentString.compare(pr.m_portName))
			{
				ui.portTreeWidget->setCurrentItem(item);
			}
		}
		for(int i = 0; i < ui.portTreeWidget->columnCount(); i++)
		{
			ui.portTreeWidget->resizeColumnToContents(i);
		}

		// ~~~~ ASSOCIATED NODES TABLE WIDGET ~~~~
		//Clear the cells but not the headers
		ui.associatedNodesTableWidget->clearContents();

		//Set up port columns
		NodeProfile *np = &m_honeydConfig->m_profiles[m_currentProfile];

		//Insert each node for the profile
		while(ui.associatedNodesTableWidget->columnCount() > 3)
		{
			ui.associatedNodesTableWidget->removeColumn(3);
		}
		for(uint i = 0; i < np->m_ports.size(); i++)
		{
			ui.associatedNodesTableWidget->insertColumn(ui.associatedNodesTableWidget->columnCount());
			//*WARNING - 'item' is a QTreeWidgetItem pointer in the next scope up, watch var usage
			QTableWidgetItem * item = new QTableWidgetItem();
			item->setText(QString(np->m_ports[i].first.c_str()));
			item->setTextAlignment(Qt::AlignCenter);
			ui.associatedNodesTableWidget->setHorizontalHeaderItem(ui.associatedNodesTableWidget->columnCount()-1, item);
		}
		int i = 0;
		for(NodeTable::iterator it = m_honeydConfig->m_nodes.begin(); it != m_honeydConfig->m_nodes.end(); it++)
		{
			if(!it->second.m_pfile.compare(m_currentProfile))
			{
				AddNodeToProfileTable(it->first, i);
				i++;
			}
		}
		//Set number of nodes using profile
		ui.numNodesSpinBox->setValue(i);

		//Remove excess rows
		while(i < ui.associatedNodesTableWidget->rowCount())
		{
			ui.associatedNodesTableWidget->removeRow(i);
		}
		for(int i = 0; i < ui.associatedNodesTableWidget->columnCount(); i++)
		{
			ui.associatedNodesTableWidget->resizeColumnToContents(i);
		}

		// ~~~~ CHILDREN PROFILE TREE WIDGET ~~~~

		ui.childrenProfileTreeWidget->clear();

		for(ProfileTable::iterator it = m_honeydConfig->m_profiles.begin(); it != m_honeydConfig->m_profiles.end(); it++)
		{
			if(!it->second.m_parentProfile.compare(m_currentProfile))
			{

				QTreeWidgetItem *item = new QTreeWidgetItem();
				item->setText(0, QString(it->first.c_str()));
				ui.childrenProfileTreeWidget->addTopLevelItem(item);

				QSpinBox *numNodesSpinBox = new QSpinBox();
				numNodesSpinBox->setMaximum(100);
				numNodesSpinBox->setMinimum(0);
				numNodesSpinBox->setValue(it->second.m_nodeKeys.size());
				numNodesSpinBox->setAlignment(Qt::AlignCenter);
				ui.childrenProfileTreeWidget->setItemWidget(item, 1, numNodesSpinBox);

				QSlider *nodeDistribSlider = new QSlider();
				nodeDistribSlider->setMaximum(100);
				nodeDistribSlider->setMinimum(0);
				nodeDistribSlider->setOrientation(Qt::Horizontal);
				if((it->second.m_distribution < 0) || (it->second.m_distribution > 100))
				{
					nodeDistribSlider->setValue(0);
				}
				else
				{
					nodeDistribSlider->setValue((int)(it->second.m_distribution/1));
				}
				ui.childrenProfileTreeWidget->setItemWidget(item, 2, nodeDistribSlider);
			}
		}
		for(int i = 0; i < ui.childrenProfileTreeWidget->topLevelItemCount(); i++)
		{

		}
		for(int i = 0; i < ui.childrenProfileTreeWidget->columnCount(); i++)
		{
			ui.childrenProfileTreeWidget->resizeColumnToContents(i);
		}
	}
	else
	{
		ui.portTreeWidget->clear();

		//Set the variables of the profile
		ui.profileEdit->clear();
		//ui.ethernetEdit->clear();
		ui.tcpActionComboBox->setCurrentIndex(0);
		ui.udpActionComboBox->setCurrentIndex(0);
		ui.icmpActionComboBox->setCurrentIndex(0);
		ui.uptimeEdit->clear();
		ui.personalityEdit->clear();
		ui.uptimeBehaviorComboBox->setCurrentIndex(0);
		ui.uptimeRangeLabel->setVisible(false);
		ui.uptimeRangeEdit->setVisible(false);
		ui.dropRateSlider->setValue(0);
		ui.dropRateSetting->setText("0%");
		ui.profileEdit->setEnabled(false);
		//ui.ethernetEdit->setEnabled(false);
		ui.tcpActionComboBox->setEnabled(false);
		ui.udpActionComboBox->setEnabled(false);
		ui.icmpActionComboBox->setEnabled(false);
		ui.uptimeEdit->setEnabled(false);
		ui.personalityEdit->setEnabled(false);
		ui.uptimeBehaviorComboBox->setEnabled(false);
		ui.dropRateSlider->setEnabled(false);

		//Clear childrenProfileTree
		ui.childrenProfileTreeWidget->clear();

		//Clear associated nodes table
		ui.associatedNodesTableWidget->clearContents();
		for(int i = 3; i < ui.associatedNodesTableWidget->columnCount(); i++)
		{
			ui.associatedNodesTableWidget->removeColumn(i);
		}
		while(ui.associatedNodesTableWidget->rowCount())
		{
			ui.associatedNodesTableWidget->removeRow(0);
		}
	}
}

void NovaConfig::LoadInheritedProfileSettings()
{
	QFont tempFont;
	NodeProfile *p = &m_honeydConfig->m_profiles[m_currentProfile];

	ui.udpActionComboBox->setCurrentIndex( ui.udpActionComboBox->findText(p->m_udpAction.c_str() ) );
	ui.icmpActionComboBox->setCurrentIndex( ui.icmpActionComboBox->findText(p->m_icmpAction.c_str() ) );

	ui.tcpCheckBox->setChecked(p->m_inherited[ICMP_ACTION]);
	ui.tcpCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.tcpCheckBox->setChecked(p->m_inherited[TCP_ACTION]);

	tempFont = QFont(ui.tcpActionComboBox->font());
	tempFont.setItalic(p->m_inherited[TCP_ACTION]);
	ui.tcpActionComboBox->setFont(tempFont);
	ui.tcpActionLabel->setFont(tempFont);
	ui.tcpActionComboBox->setEnabled(!p->m_inherited[TCP_ACTION]);

	ui.udpCheckBox->setChecked(p->m_inherited[UDP_ACTION]);
	ui.udpCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.udpCheckBox->setChecked(p->m_inherited[UDP_ACTION]);

	tempFont = QFont(ui.udpActionComboBox->font());
	tempFont.setItalic(p->m_inherited[UDP_ACTION]);
	ui.udpActionComboBox->setFont(tempFont);
	ui.udpActionLabel->setFont(tempFont);
	ui.udpActionComboBox->setEnabled(!p->m_inherited[UDP_ACTION]);

	ui.icmpCheckBox->setChecked(p->m_inherited[ICMP_ACTION]);
	ui.icmpCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.icmpCheckBox->setChecked(p->m_inherited[ICMP_ACTION]);

	tempFont = QFont(ui.icmpActionComboBox->font());
	tempFont.setItalic(p->m_inherited[ICMP_ACTION]);
	ui.icmpActionComboBox->setFont(tempFont);
	ui.icmpActionLabel->setFont(tempFont);
	ui.icmpActionComboBox->setEnabled(!p->m_inherited[ICMP_ACTION]);

	ui.ethernetCheckBox->setChecked(p->m_inherited[ETHERNET]);
	ui.ethernetCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.ethernetCheckBox->setChecked(p->m_inherited[ETHERNET]);

	ui.uptimeCheckBox->setChecked(p->m_inherited[UPTIME]);
	ui.uptimeCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.uptimeCheckBox->setChecked(p->m_inherited[UPTIME]);
	tempFont = QFont(ui.uptimeLabel->font());
	tempFont.setItalic(p->m_inherited[UPTIME]);
	ui.uptimeLabel->setFont(tempFont);
	ui.uptimeEdit->setFont(tempFont);
	ui.uptimeRangeEdit->setFont(tempFont);
	ui.uptimeRangeLabel->setFont(tempFont);
	ui.uptimeEdit->setEnabled(!p->m_inherited[UPTIME]);

	ui.uptimeBehaviorComboBox->setFont(tempFont);
	ui.uptimeBehaviorComboBox->setEnabled(!p->m_inherited[UPTIME]);

	ui.personalityCheckBox->setChecked(p->m_inherited[PERSONALITY]);
	ui.personalityCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.personalityCheckBox->setChecked(p->m_inherited[PERSONALITY]);
	tempFont = QFont(ui.personalityLabel->font());
	tempFont.setItalic(p->m_inherited[PERSONALITY]);
	ui.personalityLabel->setFont(tempFont);
	ui.personalityEdit->setFont(tempFont);
	ui.personalityEdit->setEnabled(!p->m_inherited[PERSONALITY]);
	ui.setPersonalityButton->setEnabled(!p->m_inherited[PERSONALITY]);

	ui.dropRateCheckBox->setChecked(p->m_inherited[DROP_RATE]);
	ui.dropRateCheckBox->setEnabled(p->m_parentProfile.compare(""));
	//We set again incase the checkbox was disabled (previous selection was root profile)
	ui.dropRateCheckBox->setChecked(p->m_inherited[DROP_RATE]);
	tempFont = QFont(ui.dropRateLabel->font());
	tempFont.setItalic(p->m_inherited[DROP_RATE]);
	ui.dropRateLabel->setFont(tempFont);
	ui.dropRateSetting->setFont(tempFont);
	ui.dropRateSlider->setEnabled(!p->m_inherited[DROP_RATE]);

	if(ui.ethernetCheckBox->isChecked())
	{
		p->m_ethernetVendors = m_honeydConfig->m_profiles[p->m_parentProfile].m_ethernetVendors;
	}

	if(ui.uptimeCheckBox->isChecked())
	{
		p->m_uptimeMin = m_honeydConfig->m_profiles[p->m_parentProfile].m_uptimeMin;
		p->m_uptimeMax = m_honeydConfig->m_profiles[p->m_parentProfile].m_uptimeMax;
		if(m_honeydConfig->m_profiles[p->m_parentProfile].m_uptimeMin == m_honeydConfig->m_profiles[p->m_parentProfile].m_uptimeMax)
		{
			ui.uptimeBehaviorComboBox->setCurrentIndex(1);
		}
		else
		{
			ui.uptimeBehaviorComboBox->setCurrentIndex(0);
		}
	}

	if(ui.personalityCheckBox->isChecked())
	{
		p->m_personality = m_honeydConfig->m_profiles[p->m_parentProfile].m_personality;
	}
	if(ui.dropRateCheckBox->isChecked())
	{
		p->m_dropRate = m_honeydConfig->m_profiles[p->m_parentProfile].m_dropRate;
	}
	if(ui.tcpCheckBox->isChecked())
	{
		p->m_tcpAction = m_honeydConfig->m_profiles[p->m_parentProfile].m_tcpAction;
	}
	if(ui.udpCheckBox->isChecked())
	{
		p->m_udpAction = m_honeydConfig->m_profiles[p->m_parentProfile].m_udpAction;
	}
	if(ui.icmpCheckBox->isChecked())
	{
		p->m_icmpAction = m_honeydConfig->m_profiles[p->m_parentProfile].m_icmpAction;
	}
}

//Draws all profile heirarchy in the tree widget
void NovaConfig::LoadAllProfiles()
{
	m_loading->lock();
	ui.hsProfileTreeWidget->clear();
	ui.profileTreeWidget->clear();
	ui.hsProfileTreeWidget->sortByColumn(0,Qt::AscendingOrder);
	ui.profileTreeWidget->sortByColumn(0,Qt::AscendingOrder);

	if(m_honeydConfig->m_profiles.size())
	{
		//calls createProfileItem on every profile, this will first assert that all ancestors have items
		// and create them if not to draw the table correctly, thus the need for the NULL pointer as a flag
		for(ProfileTable::iterator it = m_honeydConfig->m_profiles.begin(); it != m_honeydConfig->m_profiles.end(); it++)
		{
			if(!m_currentProfile.compare(""))
			{
				m_currentProfile = it->first;
			}
			CreateProfileItem(it->first);
		}
		//ui.profileTreeWidget->setCurrentItem(ui.profileTreeWidget->find(QString(m_currentProfile.c_str())));
		//Sets the current selection to the original selection
		//populates the window and expand the profile heirarchy
		ui.hsProfileTreeWidget->expandAll();
		ui.profileTreeWidget->expandAll();
		LoadProfileSettings();
	}
	//If no profiles exist, do nothing and ensure no profile is selected
	else
	{
		m_currentProfile = "";
	}
	m_loading->unlock();
}



QTreeWidgetItem *NovaConfig::GetProfileTreeWidgetItem(string profileName)
{
	QList<QTreeWidgetItem*> items = ui.profileTreeWidget->findItems(QString(profileName.c_str()),
		Qt::MatchExactly | Qt::MatchFixedString | Qt::MatchRecursive, 0);
	if(items.empty())
	{
		return NULL;
	}
	return items.first();
}

QTreeWidgetItem *NovaConfig::GetProfileHsTreeWidgetItem(string profileName)
{
	QList<QTreeWidgetItem*> items = ui.hsProfileTreeWidget->findItems(QString(profileName.c_str()),
			Qt::MatchExactly | Qt::MatchFixedString | Qt::MatchRecursive, 1);
	if(items.empty())
	{
		return NULL;
	}
	return items.first();
}


QTreeWidgetItem *NovaConfig::GetSubnetTreeWidgetItem(string subnetName)
{
	QList<QTreeWidgetItem*> items = ui.nodeTreeWidget->findItems(QString(subnetName.c_str()),
		Qt::MatchEndsWith, 1);
	if(items.empty())
	{
		return NULL;
	}
	return items.first();
}



QTreeWidgetItem *NovaConfig::GetSubnetHsTreeWidgetItem(string subnetName)
{
	QList<QTreeWidgetItem*> items = ui.hsNodeTreeWidget->findItems(QString(subnetName.c_str()),
		Qt::MatchEndsWith, 0);
	if(items.empty())
	{
		return NULL;
	}
	return items.first();
}

QTreeWidgetItem *NovaConfig::GetNodeTreeWidgetItem(string nodeName)
{
	QList<QTreeWidgetItem*> items = ui.nodeTreeWidget->findItems(QString(nodeName.c_str()),
		Qt::MatchExactly | Qt::MatchFixedString | Qt::MatchRecursive, 0);
	if(items.empty())
	{
		return NULL;
	}
	return items.first();
}

QTreeWidgetItem *NovaConfig::GetNodeHsTreeWidgetItem(string nodeName)
{
	QList<QTreeWidgetItem*> items = ui.hsNodeTreeWidget->findItems(QString(nodeName.c_str()),
		Qt::MatchExactly | Qt::MatchFixedString | Qt::MatchRecursive, 0);
	if(items.empty())
	{
		return NULL;
	}
	return items.first();
}


bool NovaConfig::IsPortTreeWidgetItem(std::string port, QTreeWidgetItem *item)
{
	stringstream ss;
	ss << item->text(0).toStdString() << "_";
	ss << item->text(1).toStdString() << "_";
	ss << item->text(2).toStdString();
	return (ss.str() == port);
}

bool NovaConfig::AddNodeToProfileTable(std::string nodeName, int row)
{
	if(m_honeydConfig->m_nodes.keyExists(nodeName))
	{
		while(row >= ui.associatedNodesTableWidget->rowCount())
		{
			ui.associatedNodesTableWidget->insertRow(row);
		}
		Node curNode = m_honeydConfig->m_nodes[nodeName];
		NodeProfile curProfile = m_honeydConfig->m_profiles[curNode.m_pfile];

		//Node name
		QTableWidgetItem *item = new QTableWidgetItem();
		item->setText(QString(nodeName.c_str()));
		ui.associatedNodesTableWidget->setItem(row, 0, item);

		//Interface List
		vector<string> hostInterfaceList = Config::Inst()->GetIPv4HostInterfaceList();
		item = new QTableWidgetItem();
		TableItemComboBox *nodeIFBox = new TableItemComboBox(this, item);
		if(nodeName.compare("Doppelganger"))
		{
			for(uint i = 0; i < hostInterfaceList.size(); i++)
			{
				nodeIFBox->addItem(QString(hostInterfaceList[i].c_str()));
			}
		}
		else
		{
			vector<string> loopbackInterfaces = Config::Inst()->GetIPv4LoopbackInterfaceList();
			for(uint i = 0; i < loopbackInterfaces.size(); i++)
			{
				nodeIFBox->addItem(QString(loopbackInterfaces[i].c_str()));
			}
		}
		nodeIFBox->setCurrentIndex(nodeIFBox->findText(curNode.m_interface.c_str()));
		item->setText(QString(curNode.m_interface.c_str()));

		ui.associatedNodesTableWidget->setItem(row, 1, item);
		ui.associatedNodesTableWidget->setCellWidget(row, 1, nodeIFBox);

		//Ethernet Vendors
		VendorMacDb vendDB;
		vendDB.LoadPrefixFile();
		item = new QTableWidgetItem();
		TableItemComboBox *nodeEthBox = new TableItemComboBox(this, item);

		for(uint i = 0; i < curProfile.m_ethernetVendors.size(); i++)
		{
			nodeEthBox->addItem(QString(curProfile.m_ethernetVendors[i].first.c_str()));
		}

		uint rawPrefix = vendDB.AtoMACPrefix(curNode.m_MAC);
		string vendorString = vendDB.LookupVendor(rawPrefix);
		Trim(vendorString);

		nodeEthBox->setCurrentIndex(nodeEthBox->findText(vendorString.c_str()));
		if(nodeEthBox->currentIndex() == -1)
		{
			nodeEthBox->setCurrentIndex(0);
		}

		item->setText(QString(m_honeydConfig->m_profiles[curNode.m_pfile].m_ethernetVendors[0].first.c_str()));
		ui.associatedNodesTableWidget->setItem(row, 2, item);
		ui.associatedNodesTableWidget->setCellWidget(row, 2, nodeEthBox);

		//Ports
		for(int j = 3; j < ui.associatedNodesTableWidget->columnCount(); j++)
		{
			item = new QTableWidgetItem();
			QCheckBox *usePortBox = new QCheckBox();
			usePortBox->setText("Use Port");
			usePortBox->setChecked(false);
			item->setText("N");
			for(uint i = 0; i < curNode.m_ports.size(); i++)
			{
				if(!curNode.m_ports[i].compare(ui.associatedNodesTableWidget->horizontalHeaderItem(j)->text().toStdString()))
				{
					usePortBox->setChecked(true);
					item->setText("Y");
					break;
				}
			}
			ui.associatedNodesTableWidget->setItem(row, j, item);
			ui.associatedNodesTableWidget->setCellWidget(row, j, usePortBox);
		}
		return true;
	}
	return false;
}

//Creates tree widget items for a profile and all ancestors if they need one.
void NovaConfig::CreateProfileItem(string pstr)
{
	NodeProfile p = m_honeydConfig->m_profiles[pstr];
	//If the profile hasn't had an item created yet
	if(GetProfileTreeWidgetItem(p.m_name) == NULL)
	{
		QTreeWidgetItem *item = NULL;
		//get the name
		string profileStr = p.m_name;
		//if the profile has no parents create the item at the top level
		if(!p.m_parentProfile.compare(""))
		{
			/*NOTE*/
			//These items don't need to be deleted because the clear function
			// and destructor of the tree widget does that already.
			item = new QTreeWidgetItem(ui.hsProfileTreeWidget,0);
			item->setText(0, (QString)profileStr.c_str());

			//Create the profile item for the profile tree
			item = new QTreeWidgetItem(ui.profileTreeWidget,0);
			item->setText(0, (QString)profileStr.c_str());
		}
		//if the profile has ancestors
		else
		{
			//find the parent and assert that they have an item
			if(m_honeydConfig->m_profiles.keyExists(p.m_parentProfile))
			{
				NodeProfile parent = m_honeydConfig->m_profiles[p.m_parentProfile];

				if(GetProfileTreeWidgetItem(parent.m_name) == NULL)
				{
					//if parent has no item recursively ascend until all parents do
					CreateProfileItem(p.m_parentProfile);
					parent = m_honeydConfig->m_profiles[p.m_parentProfile];
				}
				//Now that all ancestors have items, create the profile's item

				//*NOTE*
				//These items don't need to be deleted because the clear function
				// and destructor of the tree widget does that already.
				item = new QTreeWidgetItem(item = GetProfileHsTreeWidgetItem(parent.m_name), 0);
				item->setText(0, (QString)profileStr.c_str());

				//Create the profile item for the profile tree
				item = new QTreeWidgetItem(item = GetProfileTreeWidgetItem(parent.m_name), 0);
				item->setText(0, (QString)profileStr.c_str());
			}
		}
		m_honeydConfig->m_profiles[p.m_name] = p;
	}
}

void NovaConfig::SetInputValidators()
{
	// Allows positive integers
	QIntValidator *uintValidator = new QIntValidator();
	uintValidator->setBottom(0);

	// Allows positive doubles
	QDoubleValidator *udoubleValidator = new QDoubleValidator();
	udoubleValidator->setBottom(0);

	// Set up input validators so user can't enter obviously bad data in the QLineEdits
	// General settings
	ui.saAttemptsMaxEdit->setValidator(uintValidator);
	ui.saAttemptsTimeEdit->setValidator(udoubleValidator);
	ui.saPortEdit->setValidator(uintValidator);
	ui.tcpTimeoutEdit->setValidator(uintValidator);
	ui.tcpFrequencyEdit->setValidator(uintValidator);

	// Profile page
	ui.uptimeEdit->setValidator(uintValidator);
	ui.uptimeRangeEdit->setValidator(uintValidator);

	// Classification engine settings
	ui.ceIntensityEdit->setValidator(uintValidator);
	ui.ceFrequencyEdit->setValidator(uintValidator);
	ui.ceThresholdEdit->setValidator(udoubleValidator);
	ui.ceErrorEdit->setValidator(udoubleValidator);

	// Doppelganger
	// TODO: Make a custom validator for ipv4 and ipv6 IP addresses
	// For now we just make sure someone doesn't enter whitespace
	// XXX ui.dmIPEdit->setValidator(noSpaceValidator);
}

/*************************
  Profile Menu GUI Signals
 *************************/

// This loads the profile config menu for the profile selected
void NovaConfig::on_profileTreeWidget_itemSelectionChanged()
{
	if(!m_loading->tryLock())
	{
		return;
	}
	if(m_honeydConfig->m_profiles.size())
	{
		//Save old profile
		SaveProfileSettings();
		if(!ui.profileTreeWidget->selectedItems().isEmpty())
		{
			QTreeWidgetItem *item = ui.profileTreeWidget->selectedItems().first();
			m_currentProfile = item->text(0).toStdString();
		}
		LoadProfileSettings();
		m_loading->unlock();
		return;
	}
	m_loading->unlock();
}

void NovaConfig::on_actionProfileDelete_triggered()
{
	if((!ui.profileTreeWidget->selectedItems().isEmpty()) && m_currentProfile.compare("default")
		&& (m_honeydConfig->m_profiles.keyExists(m_currentProfile)))
	{
		if( m_honeydConfig->IsProfileUsed(m_currentProfile))
		{
			LOG(ERROR, "ERROR: A Node is currently using this profile.","");
			if(m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->CANNOT_DELETE_ITEM, "Profile "
				+m_currentProfile+" cannot be deleted because some nodes are currently using it, would you like to "
				"disable all nodes currently using it?",ui.actionNo_Action, ui.actionNo_Action, this) == CHOICE_DEFAULT)
			{
				m_honeydConfig->DisableProfileNodes(m_currentProfile);
			}
		}
		//TODO appropriate display prompt here
		else
		{
			DeleteProfile(m_currentProfile);
		}
	}
	else if(!m_currentProfile.compare("default"))
	{
		LOG(INFO, "Notify: Cannot delete the default profile.","");

		if(m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->CANNOT_DELETE_ITEM, "Cannot delete the default profile, "
			"would you like to disable the Haystack instead?",ui.actionNo_Action, ui.actionNo_Action, this) == CHOICE_DEFAULT)
		{
			m_loading->lock();
			bool tempSelBool = m_selectedSubnet;
			m_selectedSubnet = true;
			string tempNode = m_currentNode;
			m_currentNode = "";
			string tempNet = m_currentSubnet;
			for(SubnetTable::iterator it = m_honeydConfig->m_subnets.begin(); it != m_honeydConfig->m_subnets.end(); it++)
			{
				m_currentSubnet = it->second.m_name;
				on_actionNodeDisable_triggered();
				m_loading->lock();
			}
			m_selectedSubnet = tempSelBool;
			m_currentNode = tempNode;
			m_currentSubnet = tempNet;
			m_loading->unlock();
		}
	}
	m_loading->unlock();
	LoadAllNodes();
}

void NovaConfig::on_actionProfileAdd_triggered()
{
	m_loading->lock();
	struct NodeProfile temp;
	temp.m_name = "New Profile";

	stringstream ss;
	uint i = 0, j = 0;
	j = ~j; // 2^32-1

	//Finds a unique identifier
	while((m_honeydConfig->m_profiles.keyExists(temp.m_name)) && (i < j))
	{
		i++;
		ss.str("");
		ss << "New Profile-" << i;
		temp.m_name = ss.str();
	}
	//If there is currently a selected profile, that profile will be the parent of the new profile
	if(m_honeydConfig->m_profiles.keyExists(m_currentProfile))
	{
		string tempName = temp.m_name;
		temp = m_honeydConfig->m_profiles[m_currentProfile];
		temp.m_name = tempName;
		temp.m_parentProfile = m_currentProfile;
		for(uint i = 0; i < INHERITED_MAX; i++)
		{
			temp.m_inherited[i] = true;
		}
		for(uint i = 0; i < temp.m_ports.size(); i++)
		{
			temp.m_ports[i].second.first = true;
		}
		m_currentProfile = temp.m_name;
	}
	//If no profile is selected the profile is a root node
	else
	{
		temp.m_parentProfile = "";
		temp.m_ethernetVendors.clear();
		temp.m_personality = "";
		temp.m_tcpAction = "reset";
		temp.m_udpAction = "reset";
		temp.m_icmpAction = "reset";
		temp.m_uptimeMin = "0";
		temp.m_uptimeMax = "0";
		temp.m_dropRate = "0";
		temp.m_ports.clear();
		m_currentProfile = temp.m_name;
		for(uint i = 0; i < INHERITED_MAX; i++)
		{
			temp.m_inherited[i] = false;
		}
	}
	//Puts the profile in the table, creates a ptree and loads the new configuration
	m_honeydConfig->AddProfile(temp);
	m_loading->unlock();
	LoadAllProfiles();
	LoadAllNodes();
}

void NovaConfig::on_actionProfileClone_triggered()
{

	//Do nothing if no profiles
	if(m_honeydConfig->m_profiles.size())
	{
		m_loading->lock();
		QTreeWidgetItem *item = ui.profileTreeWidget->selectedItems().first();
		string profileStr = item->text(0).toStdString();
		NodeProfile p = m_honeydConfig->m_profiles[m_currentProfile];

		stringstream ss;
		uint i = 1, j = 0;
		j = ~j; //2^32-1

		//Since we are cloning, it will already be a duplicate
		ss.str("");
		ss << profileStr << "-" << i;
		p.m_name = ss.str();

		//Check for name in use, if so increase number until unique name is found
		while((m_honeydConfig->m_profiles.keyExists(p.m_name)) && (i < j))
		{
			ss.str("");
			i++;
			ss << profileStr << "-" << i;
			p.m_name = ss.str();
		}
		p.m_tree.put<std::string>("name",p.m_name);
		//Change the profile name and put in the table, update the current profile
		//Extract all descendants, create a ptree, update with new configuration
		m_honeydConfig->m_profiles[p.m_name] = p;
		if(!(m_honeydConfig->LoadProfilesFromTree(p.m_name)))
		{
			m_mainwindow->m_prompter->DisplayPrompt(m_mainwindow->HONEYD_LOAD_FAIL, "Problem loading Profiles. See log for details.");
		}
		m_honeydConfig->UpdateProfile(p.m_name);
		m_loading->unlock();
		LoadAllProfiles();
		LoadAllNodes();
	}
}

void NovaConfig::on_profileEdit_editingFinished()
{
	if(!m_loading->tryLock())
	{
		return;
	}
	else if(!m_currentProfile.compare("Doppelganger"))
	{
		ui.profileEdit->setText("Doppelganger");
	}
	else if(!m_honeydConfig->m_profiles.empty())
	{
		QString newName = ui.profileEdit->displayText();
		if(newName.toStdString().compare("Doppelganger"))
		{
			GetProfileTreeWidgetItem(m_currentProfile)->setText(0, newName);
			QTreeWidgetItem *item = GetProfileHsTreeWidgetItem(m_currentProfile);
			if(item != NULL)
			{
				item->setText(0, newName);
			}

			//If the name has changed we need to move it in the profile hash table and point all
			//nodes that use the profile to the new location.
			if(m_honeydConfig->RenameProfile(m_currentProfile, newName.toStdString()))
			{
				m_currentProfile = newName.toStdString();
			}
			//If we're not simply trying to rename it to itself
			else if(m_currentProfile.compare(newName.toStdString()))
			{
				LOG(ERROR, "Unable to rename profile '" + m_currentProfile+ "' to new name " + newName.toStdString() + ".", "Unable to rename profile '"
					+ m_currentProfile+ "' to new name " + newName.toStdString() + ". Check 'profiles.xml'");
			}
			SaveProfileSettings();
			LoadProfileSettings();
			m_loading->unlock();
			LoadAllNodes();
			return;
		}
		else
		{
			LOG(ERROR, "Unable to use the reserved 'Doppelganger' name for custom profiles.", "");
			ui.profileEdit->setText(QString(m_currentProfile.c_str()));
		}
	}
	m_loading->unlock();
}

/************************
 Node Menu GUI Functions
 ************************/

void NovaConfig::LoadAllNodes()
{
	m_loading->lock();
	QBrush whitebrush(QColor(255, 255, 255, 255));
	QBrush greybrush(QColor(100, 100, 100, 255));
	greybrush.setStyle(Qt::SolidPattern);
	QBrush blackbrush(QColor(0, 0, 0, 255));
	blackbrush.setStyle(Qt::NoBrush);
	struct Node *n = NULL;

	QTreeWidgetItem *item = NULL;
	QTreeWidgetItem *hsItem = NULL;
	ui.nodeTreeWidget->clear();
	ui.hsNodeTreeWidget->clear();

	QTreeWidgetItem *subnetItem = NULL;
	QTreeWidgetItem *subnetHsItem = NULL;

	for(SubnetTable::iterator it = m_honeydConfig->m_subnets.begin(); it != m_honeydConfig->m_subnets.end(); it++)
	{
		//create the subnet item for the Haystack menu tree
		subnetHsItem  = new QTreeWidgetItem(ui.hsNodeTreeWidget, 0);
		subnetHsItem ->setText(0, (QString)it->second.m_address.c_str());
		if(it->second.m_isRealDevice)
		{
			subnetHsItem ->setText(1, (QString)"Physical Device - "+it->second.m_name.c_str());
		}
		else
		{
			subnetHsItem ->setText(1, (QString)"Virtual Interface - "+it->second.m_name.c_str());
		}

		//create the subnet item for the node edit tree
		subnetItem = new QTreeWidgetItem(ui.nodeTreeWidget, 0);
		subnetItem->setText(0, (QString)it->second.m_address.c_str());
		if(it->second.m_isRealDevice)
		{
			subnetItem->setText(1, (QString)"Physical Device - "+it->second.m_name.c_str());
		}
		else
		{
			subnetItem->setText(1, (QString)"Virtual Interface - "+it->second.m_name.c_str());
		}

		if(!it->second.m_enabled)
		{
			whitebrush.setStyle(Qt::NoBrush);
			subnetItem->setBackground(0,greybrush);
			subnetItem->setForeground(0,whitebrush);
			subnetHsItem->setBackground(0,greybrush);
			subnetHsItem->setForeground(0,whitebrush);
		}

		//Pre-create a list of profiles for the node profile selection
		QStringList *profileStrings = new QStringList();
		{
			for(ProfileTable::iterator it = m_honeydConfig->m_profiles.begin(); it != m_honeydConfig->m_profiles.end(); it++)
			{
				profileStrings->append(QString(it->first.c_str()));
			}
		}
		profileStrings->sort();

		for(uint i = 0; i < it->second.m_nodes.size(); i++)
		{

			n = &m_honeydConfig->m_nodes[it->second.m_nodes[i]];

			//Create the node item for the Haystack tree
			item = new QTreeWidgetItem(subnetHsItem, 0);
			item->setText(0, (QString)n->m_name.c_str());
			item->setText(1, (QString)n->m_pfile.c_str());

			//Create the node item for the node edit tree
			item = new QTreeWidgetItem(subnetItem, 0);
			item->setText(0, (QString)n->m_name.c_str());
			item->setText(1, (QString)n->m_pfile.c_str());

			TreeItemComboBox *pfileBox = new TreeItemComboBox(this, item);
			pfileBox->addItems(*profileStrings);
			QObject::connect(pfileBox, SIGNAL(notifyParent(QTreeWidgetItem *, bool)), this, SLOT(nodeTreeWidget_comboBoxChanged(QTreeWidgetItem *, bool)));
			pfileBox->setCurrentIndex(pfileBox->findText(n->m_pfile.c_str()));

			ui.nodeTreeWidget->setItemWidget(item, 1, pfileBox);
			if(!n->m_name.compare("Doppelganger"))
			{
				ui.dmCheckBox->setChecked(Config::Inst()->GetIsDmEnabled());
				//Enable the loopback subnet as well if DM is enabled
				m_honeydConfig->m_subnets[n->m_sub].m_enabled |= Config::Inst()->GetIsDmEnabled();
			}
			if(!n->m_enabled)
			{
				hsItem = GetNodeHsTreeWidgetItem(n->m_name);
				item = GetNodeTreeWidgetItem(n->m_name);
				whitebrush.setStyle(Qt::NoBrush);
				hsItem->setBackground(0,greybrush);
				hsItem->setForeground(0,whitebrush);
				item->setBackground(0,greybrush);
				item->setForeground(0,whitebrush);
			}
		}
		delete profileStrings;
	}
	ui.nodeTreeWidget->expandAll();

	// Reselect the last selected node if need be
	QTreeWidgetItem *nodeItem = GetNodeTreeWidgetItem(m_currentNode);
	if(nodeItem != NULL)
	{
		nodeItem->setSelected(true);
	}
	else
	{
		if(m_honeydConfig->m_subnets.size())
		{
			if(m_honeydConfig->m_subnets.keyExists(m_currentSubnet))
			{
				if(GetSubnetTreeWidgetItem(m_currentSubnet) != NULL)
				{
					ui.nodeTreeWidget->setCurrentItem(GetSubnetTreeWidgetItem(m_currentSubnet));
				}
			}
		}
	}
	m_loading->unlock();
}

//Function called when delete button
void NovaConfig::DeleteNodes()
{
	m_loading->lock();
	string name = "";
	if((m_selectedSubnet && (!m_currentSubnet.compare(""))) || (!m_selectedSubnet && (!m_currentNode.compare(""))))
	{
		LOG(CRITICAL,"No items in the UI to delete!",
			"Should never get here. Attempting to delete a GUI item, but there are no GUI items left.");
		m_loading->unlock();
		return;
	}
	//If we have a subnet selected
	if(m_selectedSubnet)
	{
		//Get the subnet
		Subnet *s = &m_honeydConfig->m_subnets[m_currentSubnet];

		//Remove all nodes in the subnet
		while(!s->m_nodes.empty())
		{
			//If we are unable to delete the node
			if(!m_honeydConfig->DeleteNode(s->m_nodes.back()))
			{
				LOG(WARNING, string("Unabled to delete node:") + s->m_nodes.back(), "");
			}
		}

		//If this subnet is going to be removed
		if(!m_honeydConfig->m_subnets[m_currentSubnet].m_isRealDevice)
		{
			//remove the subnet from the config
			m_honeydConfig->m_subnets.erase(m_currentSubnet);

			//Get the current item in the list
			QTreeWidgetItem *cur = ui.nodeTreeWidget->selectedItems().first();
			//Get the subnet below it
			int index = ui.nodeTreeWidget->indexOfTopLevelItem(cur) + 1;
			//If there is no subnet below it, get the subnet above it
			if(index == ui.nodeTreeWidget->topLevelItemCount())
			{
				index -= 2;
			}

			//If we have the index of a valid subnet set it as the next selection
			if((index >= 0) && (index < ui.nodeTreeWidget->topLevelItemCount()))
			{
				QTreeWidgetItem *next = ui.nodeTreeWidget->topLevelItem(index);
				m_currentSubnet = next->text(1).toStdString();
				m_currentSubnet = m_currentSubnet.substr(m_currentSubnet.find("-"), m_currentSubnet.size());
			}
			//No valid selection
			else
			{
				m_currentSubnet = "";
			}
		}
		//If this subnet will remain
		else
		{
			m_loading->unlock();
			on_actionNodeDisable_triggered();
			m_loading->lock();
		}
	}
	//If we have a node selected
	else
	{
		//Get the current item
		QTreeWidgetItem *cur = ui.nodeTreeWidget->selectedItems().first();
		//Get the next item in the list
		QTreeWidgetItem *next = ui.nodeTreeWidget->itemBelow(cur);

		//If the next item is a subnet or doesn't exist
		if((next == NULL) || (ui.nodeTreeWidget->indexOfTopLevelItem(next) != -1))
		{
			//Get the previous item
			next = ui.nodeTreeWidget->itemAbove(cur);
		}
		//If there is no valid item above the node then theres a real problem
		if(next == NULL)
		{
			LOG(CRITICAL, "Qt UI Node list may be corrupted.",
				"Node is alone in the tree widget, this shouldn't be possible!");
			m_loading->unlock();
			return;
		}
		//If we are unable to delete the node
		if(!m_honeydConfig->DeleteNode(m_currentNode))
		{
			LOG(ERROR, string("Unabled to delete node:") + m_currentNode, "");
			m_loading->unlock();
			return;
		}

		//If we have a node selected
		if(ui.nodeTreeWidget->indexOfTopLevelItem(next) == -1)
		{
			m_currentNode = next->text(0).toStdString();
			m_currentSubnet = next->parent()->text(1).toStdString();
			m_currentSubnet = m_currentSubnet.substr(m_currentSubnet.find("-")+2, m_currentSubnet.size());
		}
		//If we have a subnet selected
		else
		{
			//flag the selection as that of a subnet
			m_selectedSubnet = true;
			m_currentNode = "";
			m_currentSubnet = next->text(1).toStdString();
			m_currentSubnet = m_currentSubnet.substr(m_currentSubnet.find("-")+2, m_currentSubnet.size());
		}
	}
	//Unlock and redraw the list
	m_loading->unlock();
	LoadAllNodes();
}

/*********************
 Node Menu GUI Signals
 *********************/

//The current selection in the node list
void NovaConfig::on_nodeTreeWidget_itemSelectionChanged()
{
	//If the user is changing the selection AND something is selected
	if(m_loading->tryLock())
	{
		if(!ui.nodeTreeWidget->selectedItems().isEmpty())
		{
			QTreeWidgetItem *item = ui.nodeTreeWidget->selectedItems().first();
			//If it's not a top level item (which means it's a node)
			if(ui.nodeTreeWidget->indexOfTopLevelItem(item) == -1)
			{
				m_currentNode = item->text(0).toStdString();
				m_currentSubnet = m_honeydConfig->GetNodeSubnet(m_currentNode);
				m_selectedSubnet = false;
			}
			else //If it's a subnet
			{
				m_currentSubnet = item->text(1).toStdString();
				m_currentSubnet = m_currentSubnet.substr(m_currentSubnet.find("-")+2, m_currentSubnet.size());
				m_selectedSubnet = true;
			}
		}
		m_loading->unlock();
	}
}

void NovaConfig::on_associatedNodesTableWidget_itemSelectionChanged()
{

}

void NovaConfig::nodeTreeWidget_comboBoxChanged(QTreeWidgetItem *item, bool edited)
{
	if(m_loading->tryLock())
	{
		if(edited)
		{
			ui.nodeTreeWidget->setCurrentItem(item);
			string oldPfile;
			if(!ui.nodeTreeWidget->selectedItems().isEmpty())
			{
				Node *n = &m_honeydConfig->m_nodes[item->text(0).toStdString()];
				oldPfile = n->m_pfile;
				TreeItemComboBox *pfileBox = (TreeItemComboBox* )ui.nodeTreeWidget->itemWidget(item, 1);
				n->m_pfile = pfileBox->currentText().toStdString();
			}

			m_loading->unlock();
			LoadAllNodes();
		}
		else
		{
			ui.nodeTreeWidget->setFocus(Qt::OtherFocusReason);
			ui.nodeTreeWidget->setCurrentItem(item);
			m_loading->unlock();
		}
	}
}
void NovaConfig::on_actionSubnetAdd_triggered()
{
	if(m_currentSubnet.compare(""))
	{
		m_loading->lock();
		Subnet s = m_honeydConfig->m_subnets[m_currentSubnet];
		s.m_name = m_currentSubnet + "-1";
		s.m_address = "0.0.0.0/24";
		s.m_nodes.clear();
		s.m_enabled = false;
		s.m_base = 0;
		s.m_maskBits = 24;
		s.m_max = 255;
		s.m_isRealDevice = false;
		m_honeydConfig->m_subnets[s.m_name] = s;
		m_currentSubnet = s.m_name;
		m_loading->unlock();
		subnetPopup *editSubnet = new subnetPopup(this, &m_honeydConfig->m_subnets[m_currentSubnet]);
		editSubnet->show();
	}
}
// Right click menus for the Node tree
void NovaConfig::on_actionNodeAdd_triggered()
{
	if(m_currentSubnet.compare(""))
	{
		Node n;
		n.m_sub = m_currentSubnet;
		n.m_interface = m_honeydConfig->m_subnets[m_currentSubnet].m_name;
		n.m_realIP = m_honeydConfig->m_subnets[m_currentSubnet].m_base;
		n.m_pfile = "default";
		nodePopup *editNode =  new nodePopup(this, &n);
		editNode->show();
	}
}

void NovaConfig::on_actionNodeDelete_triggered()
{
	DeleteNodes();
}

void NovaConfig::on_actionNodeClone_triggered()
{
	if(m_currentNode.compare(""))
	{
		m_loading->lock();
		Node n = m_honeydConfig->m_nodes[m_currentNode];
		m_loading->unlock();

		// Can't clone the doppelganger, only allowed one right now
		if (n.m_name == "Doppelganger")
		{
			return;
		}

		nodePopup *editNode =  new nodePopup(this, &n);
		editNode->show();
	}
}

void  NovaConfig::on_actionNodeEdit_triggered()
{
	if(!m_selectedSubnet)
	{
		// Can't change the doppel IP here, you change it in the doppel settings
		if(m_currentNode == "Doppelganger")
		{
			return;
		}

		nodePopup *editNode =  new nodePopup(this, &m_honeydConfig->m_nodes[m_currentNode], true);
		editNode->show();
	}
	else
	{
		subnetPopup *editSubnet = new subnetPopup(this, &m_honeydConfig->m_subnets[m_currentSubnet]);
		editSubnet->show();
	}
}

void NovaConfig::on_actionNodeCustomizeProfile_triggered()
{
	m_loading->lock();
	m_currentProfile = m_honeydConfig->m_nodes[m_currentNode].m_pfile;
	ui.stackedWidget->setCurrentIndex(ui.menuTreeWidget->topLevelItemCount()+1);
	QTreeWidgetItem *item = ui.menuTreeWidget->topLevelItem(HAYSTACK_MENU_INDEX);
	ui.menuTreeWidget->setCurrentItem(item->child(PROFILE_INDEX));
	ui.profileTreeWidget->setCurrentItem(GetProfileTreeWidgetItem(m_currentProfile));
	m_loading->unlock();
	Q_EMIT on_actionProfileAdd_triggered();
	m_honeydConfig->m_nodes[m_currentNode].m_pfile = m_currentProfile;
	LoadHaystackConfiguration();
}

void NovaConfig::on_actionNodeEnable_triggered()
{
	if(m_selectedSubnet)
	{
		Subnet s = m_honeydConfig->m_subnets[m_currentSubnet];
		for(uint i = 0; i < s.m_nodes.size(); i++)
		{
			m_honeydConfig->EnableNode(s.m_nodes[i]);

		}
		s.m_enabled = true;
		m_honeydConfig->m_subnets[m_currentSubnet] = s;
	}
	else
	{
		m_honeydConfig->EnableNode(m_currentNode);
	}

	//Draw the nodes and restore selection
	m_loading->unlock();
	LoadAllNodes();
	m_loading->lock();
	if(m_selectedSubnet)
	{
		if(GetSubnetTreeWidgetItem(m_currentSubnet) != NULL)
		{
			ui.nodeTreeWidget->setCurrentItem(GetSubnetTreeWidgetItem(m_currentSubnet));
		}
	}
	else
	{
		//ui.nodeTreeWidget->setCurrentItem(m_honeydConfig->m_nodes[m_currentNode].nodeItem);
	}
	m_loading->unlock();
}

void NovaConfig::on_actionNodeDisable_triggered()
{
	if(m_selectedSubnet)
	{
		Subnet s = m_honeydConfig->m_subnets[m_currentSubnet];
		for(uint i = 0; i < s.m_nodes.size(); i++)
		{

			m_honeydConfig->DisableNode(s.m_nodes[i]);
		}
		s.m_enabled = false;
		m_honeydConfig->m_subnets[m_currentSubnet] = s;
	}
	else
	{
		m_honeydConfig->DisableNode(m_currentNode);
	}

	//Draw the nodes and restore selection
	m_loading->unlock();
	LoadAllNodes();
	m_loading->lock();
	if(m_selectedSubnet)
	{
		if(GetSubnetTreeWidgetItem(m_currentSubnet) != NULL)
		{
			ui.nodeTreeWidget->setCurrentItem(GetSubnetTreeWidgetItem(m_currentSubnet));
		}
	}
	else
	{
		//ui.nodeTreeWidget->setCurrentItem(m_honeydConfig->m_nodes[m_currentNode].nodeItem);
	}
	m_loading->unlock();
}

//Not currently used, will be implemented in the new GUI design TODO
//Pops up the node edit window selecting the current node
void NovaConfig::on_nodeEditButton_clicked()
{
	Q_EMIT on_actionNodeEdit_triggered();
}
//Not currently used, will be implemented in the new GUI design TODO
//Creates a copy of the current node and pops up the edit window
void NovaConfig::on_nodeCloneButton_clicked()
{
	on_actionNodeClone_triggered();
}
//Not currently used, will be implemented in the new GUI design TODO
//Creates a new node and pops up the edit window
void NovaConfig::on_nodeAddButton_clicked()
{
	on_actionNodeAdd_triggered();
}

//Calls the function(s) to remove the node(s)
void NovaConfig::on_nodeDeleteButton_clicked()
{
	Q_EMIT on_actionNodeDelete_triggered();
}

//Enables a node or a subnet in honeyd
void NovaConfig::on_nodeEnableButton_clicked()
{
	Q_EMIT on_actionNodeEnable_triggered();
}

//Disables a node or a subnet in honeyd
void NovaConfig::on_nodeDisableButton_clicked()
{
	Q_EMIT on_actionNodeDisable_triggered();
}

void NovaConfig::on_setPersonalityButton_clicked()
{
	DisplayNmapPersonalityWindow();
}

void NovaConfig::on_dropRateSlider_valueChanged()
{
	stringstream ss;
	ss << ui.dropRateSlider->value() << "%";
	ui.dropRateSetting->setText((QString)ss.str().c_str());
}


bool NovaConfig::IsDoppelIPValid()
{
	stringstream ss;
	ss 		<< ui.dmIPSpinBox_0->value() << "."
			<< ui.dmIPSpinBox_1->value() << "."
			<< ui.dmIPSpinBox_2->value() << "."
			<< ui.dmIPSpinBox_3->value();

	if(m_honeydConfig->IsIPUsed(ss.str()) && (ss.str().compare(Config::Inst()->GetDoppelIp())))
	{
		return false;
	}
	return true;
}
//Doppelganger IP Address Spin boxes
void NovaConfig::on_dmIPSpinBox_0_valueChanged()
{
	if(!IsDoppelIPValid())
	{
		LOG(WARNING, "Current IP address conflicts with a statically address Haystack node.", "");
	}
}

void NovaConfig::on_dmIPSpinBox_1_valueChanged()
{
	if(!IsDoppelIPValid())
	{
		LOG(WARNING, "Current IP address conflicts with a statically address Haystack node.", "");
	}
}

void NovaConfig::on_dmIPSpinBox_2_valueChanged()
{
	if(!IsDoppelIPValid())
	{
		LOG(WARNING, "Current IP address conflicts with a statically address Haystack node.", "");
	}
}

void NovaConfig::on_dmIPSpinBox_3_valueChanged()
{
	if(!IsDoppelIPValid())
	{
		LOG(WARNING, "Current IP address conflicts with a statically address Haystack node.", "");
	}
}

//Haystack storage type combo box
void NovaConfig::on_hsSaveTypeComboBox_currentIndexChanged(int index)
{
	switch(index)
	{
		case 1:
		{
			ui.hsConfigEdit->setText((QString)Config::Inst()->GetPathConfigHoneydUser().c_str());
			ui.hsSummaryGroupBox->setEnabled(false);
			ui.nodesGroupBox->setEnabled(false);
			ui.profileGroupBox->setEnabled(false);
			break;
		}
		case 0:
		{
			ui.hsConfigEdit->setText((QString)Config::Inst()->GetPathConfigHoneydHS().c_str());
			ui.hsSummaryGroupBox->setEnabled(true);
			ui.nodesGroupBox->setEnabled(true);
			ui.profileGroupBox->setEnabled(true);
			break;
		}
		default:
		{
			LOG(ERROR, "Haystack save type set to undefined index!", "");
			break;
		}
	}
}

void NovaConfig::on_useAllIfCheckBox_stateChanged()
{
	ui.interfaceGroupBox->setEnabled(!ui.useAllIfCheckBox->isChecked());
}

void NovaConfig::on_useAnyLoopbackCheckBox_stateChanged()
{
	ui.loopbackGroupBox->setEnabled(!ui.useAnyLoopbackCheckBox->isChecked());
}

void  NovaConfig::on_numNodesSpinBox_editingFinished()
{
	m_loading->lock();
	/*NodeManager nodeGen = NodeManager(m_honeydConfig);
	NodeProfile *p = &m_honeydConfig->m_profiles[m_currentProfile];
	p->m_generated = true;
	nodeGen.GenerateProfileCounters(&m_honeydConfig->m_profiles[m_currentProfile]);
	int nodeChange = ui.numNodesSpinBox->value() - m_honeydConfig->m_profiles[m_currentProfile].m_nodeKeys.size();
	nodeGen.GenerateNodes(nodeChange);
	p->m_generated = false*/;
	SaveProfileSettings();
	LoadProfileSettings();
	m_loading->unlock();
}

void NovaConfig::on_haystackGroupComboBox_currentIndexChanged()
{
	if(!m_loading->tryLock())
	{
		return;
	}

	Config::Inst()->SetGroup(ui.haystackGroupComboBox->currentText().toStdString());

	m_honeydConfig->LoadAllTemplates();
	LoadHaystackConfiguration();
	m_loading->unlock();
}

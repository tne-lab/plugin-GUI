/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2018 Translational NeuroEngineering Laboratory

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "NetCom.h"
#include <Nlx_DataTypes.h>
#include <string>  // to interact with NetCom API
#include <cstring> // also to interact with NetCom API

#ifdef WIN32
#include <processthreadsapi.h>
#endif

// NetCom constants
const static String CSC_TYPE(NlxDataTypes::NetComCSCDataType);
const static String EVT_TYPE(NlxDataTypes::NetComEventDataType);
const static short EVT_ID_ACQ(NlxDataTypes::EventRecID::DataAcquisitionSoftware);
const static char* EVT_ACQ_START("Starting Acquisition");
const static char* EVT_ACQ_STOP("Stopping Acquisition");
const static char* EVT_REC_START("Starting Recording");
const static char* EVT_REC_STOP("Stopping Recording");

/* ------- NetComListener ----- */

NetComListener::NetComListener()
{
    ClientHandle handle;
    handle->addListener(this);

    connectionStatus.referTo(handle->getConnectionStatusValue());
    connectionStatus.addListener(this);

    acquisitionStatus.referTo(handle->getAcquisitionStatusValue());
    acquisitionStatus.addListener(this);
}

NetComListener::~NetComListener()
{
    ClientHandle()->removeListener(this);
}

void NetComListener::netComConnectionChanged(const String& status) {}
void NetComListener::netComAcquisitionChanged(int status) {}

void NetComListener::netComDataReceived(NlxDataTypes::CRRec* records,
    int numRecords, const String& objectName) {}
void NetComListener::netComEventsReceived(NlxDataTypes::EventRec* records,
    int numRecords, const String& objectName) {}

void NetComListener::valueChanged(Value& value)
{
    if (value == connectionStatus)
    {
        netComConnectionChanged(value.getValue());
    }
    else if (value == acquisitionStatus)
    {
        netComAcquisitionChanged(value.getValue());
    }
}

/* ------- Client ------- */

ClientHandle::Client::Client()
    : i_client          (getNewClient())
    , connectionStatus  (var(""))
    , acquisitionStatus (var(UNKNOWN))
{}

ClientHandle::Client::~Client()
{
    // try to disconnect before deleting
    if (i_client->AreWeConnected()) { i_client->DisconnectFromServer(); }
    DeleteNetComClient(i_client);
}

void ClientHandle::Client::addListener(NetComListener* listener)
{
    listeners.add(listener);
}

void ClientHandle::Client::removeListener(NetComListener* listener)
{
    listeners.remove(listener);
}

const Value& ClientHandle::Client::getConnectionStatusValue()
{
    return connectionStatus;
}

const Value& ClientHandle::Client::getAcquisitionStatusValue()
{
    return acquisitionStatus;
}

bool ClientHandle::Client::connectToServer(const String& nameOrAddress)
{
    disconnect();
    bool success = i_client->ConnectToServer(nameOrAddress.toWideCharPointer());
    if (!success)
    {
        return false;
    }

    if (!updateAcquisitionStatus())
    {
        disconnect();
        return false;
    }    

    // subscribe to all continuous channels and events
    

    connectionStatus = nameOrAddress;
    return true;
}

void ClientHandle::Client::disconnect()
{
    connectionStatus = "";
    if (i_client->AreWeConnected() && !i_client->DisconnectFromServer())
    {
        // undetermined state - make new client object
        reallocateClient();
    }
}

bool ClientHandle::Client::sendCommand(const StringArray& args, StringArray* reply, bool hasErrCode)
{
    return sendCommand(args.joinIntoString(" "), reply, hasErrCode);
}

bool ClientHandle::Client::sendCommand(const String& command, StringArray* reply, bool hasErrCode)
{
    std::wstring rawReply;
    bool success = i_client->SendCommand(command.toWideCharPointer(), rawReply);

    if (!success || reply == nullptr)
    {
        return success;
    }

    StringArray replyTemp = StringArray::fromTokens(String(rawReply.c_str()), true);
    if (hasErrCode)
    {
        if (replyTemp.size() == 0)
        {
            jassertfalse;
            return false;
        }

        if (replyTemp[0] != "0")
        {
            return false;
        }

        replyTemp.remove(0);
    }

    reply->swapWith(replyTemp);
    return true;
}

// private

NlxNetCom::NetComClient* getNewClient()
{
    NlxNetCom::NetComClient* newClient = NlxNetCom::GetNewNetComClient();
    String appName = "Open Ephys";
#ifdef WIN32 // should always be true, this is a windows-only plugin...
    uint32 pid = GetCurrentProcessId();
    appName += " (PID:";
    appName += pid;
    appName += ")";
#endif
    newClient->SetApplicationName(appName.toWideCharPointer());
}

void ClientHandle::Client::reallocateClient()
{
    NlxNetCom::NetComClient* oldClient = i_client;
    i_client = getNewClient();
    NlxNetCom::DeleteNetComClient(oldClient);
}

bool ClientHandle::Client::updateAcquisitionStatus()
{
    StringArray reply;
    // See: Cheetah reference guide, "Cheetah Commands/General Commands"
    if (sendCommand("-GetDASState", &reply, true))
    {
        if (reply.size() == 0)
        {
            jassertfalse;
        }
        else if (reply[0] == "Idle")
        {
            acquisitionStatus = IDLE;
            return true;
        }
        else if (reply[0] == "Acquiring")
        {
            acquisitionStatus = ACQUIRING;
            return true;
        }
        else if (reply[0] == "Recording")
        {
            acquisitionStatus = RECORDING;
            return true;
        }
        else
        {
            jassertfalse;
        }
    }

    acquisitionStatus = UNKNOWN;
    return false;
}

// NetCom callbacks

void ClientHandle::Client::handleConnectionLost(void* client)
{
    auto thisClient = static_cast<Client*>(client);
    ScopedLock statusLock(criticalSection);
    thisClient->connectionStatus = "";
}

void ClientHandle::Client::handleData(void* client, NlxDataTypes::CRRec* records,
    int numRecords, const wchar_t objectName[])
{
    auto thisClient = static_cast<Client*>(client);
    thisClient->listeners.call(NetComListener::netComDataReceived, records, numRecords, objectName);
}

void ClientHandle::Client::handleEvents(void* client, NlxDataTypes::EventRec* records,
    int numRecords, const wchar_t* const objectName)
{
    auto thisClient = static_cast<Client*>(client);

    // check for acquisition status update
    int latestStatus = thisClient->acquisitionStatus.getValue();
    for (int i = 0; i < numRecords; ++i)
    {
        if (records[i].nevent_id == EVT_ID_ACQ)
        {
            if ((latestStatus != RECORDING &&
                strcmp(records[i].EventString, EVT_ACQ_START) == 0) ||
                (latestStatus == RECORDING &&
                strcmp(records[i].EventString, EVT_REC_STOP) == 0))
            {
                latestStatus = ACQUIRING;
            }
            else if (strcmp(records[i].EventString, EVT_ACQ_STOP) == 0)
            {
                latestStatus = IDLE;
            }
            else if (strcmp(records[i].EventString, EVT_REC_START) == 0)
            {
                latestStatus = RECORDING;
            }
        }
    }
    thisClient->acquisitionStatus = latestStatus;

    thisClient->listeners.call(NetComListener::netComEventsReceived, records, numRecords, objectName);
}

/* -------- ClientHandle ------- */

ClientHandle::ClientHandle()
    : lock(criticalSection)
{}

ClientHandle::Client* ClientHandle::operator->()
{
    return &client;
}

// static member initializations
ClientHandle::Client ClientHandle::client;
CriticalSection ClientHandle::criticalSection;
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

#include "ClientHandle.h"

/* ------- NetComListener ----- */

NetComListener::Listener()
{
    ClientHandle()->addListener(this);
}

NetComListener::~Listener()
{
    ClientHandle()->removeListener(this);
}

void NetComListener::netComConnectionChanged(const String& status) {}
void NetComListener::netComDataReceived(NlxDataTypes::CRRec* records,
    int numRecords, const String& objectName) {}
void NetComListener::netComEventsReceived(NlxDataTypes::EventRec* records,
    int numRecords, const String& objectName) {}

void NetComListener::valueChanged(Value& value)
{
    netComConnectionChanged(value.toString());
}

/* ------- Client ------- */

ClientHandle::Client::Client()
    : i_client          (NlxNetCom::GetNewNetComClient())
    , connectionStatus  (String(""))
{}

ClientHandle::Client::~Client()
{
    // try to disconnect before deleting
    if (i_client->AreWeConnected()) { i_client->DisconnectFromServer(); }
    DeleteNetComClient(i_client);
}

void ClientHandle::Client::reallocateClient()
{
    NlxNetCom::NetComClient* oldClient = i_client;
    i_client = NlxNetCom::GetNewNetComClient();
    NlxNetCom::DeleteNetComClient(oldClient);
}

void ClientHandle::Client::addListener(NetComListener* listener)
{
    connectionStatus.addListener(listener);
    listeners.add(listener);
}

void ClientHandle::Client::removeListener(NetComListener* listener)
{
    connectionStatus.removeListener(listener);
    listeners.remove(listener);
}

bool ClientHandle::Client::connectToServer(const String& nameOrAddress)
{
    disconnect();
    bool success = i_client->ConnectToServer(nameOrAddress.toWideCharPointer());
    if (success)
    {
        connectionStatus = nameOrAddress;
    }
    return success;
}

void ClientHandle::Client::disconnect()
{
    if (connectionStatus != "")
    {
        connectionStatus = "";
    }

    if (i_client->AreWeConnected() && !i_client->DisconnectFromServer())
    {
        // undetermined state - make new client object
        reallocateClient();
    }
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
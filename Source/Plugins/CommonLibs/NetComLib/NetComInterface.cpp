/*
------------------------------------------------------------------

This file is part of a library for the Open Ephys GUI
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

#include "NetComInterface.h"

namespace
{
    using namespace NlxNetCom;

    // values for connectionStatus
    const var NOT_CONNECTED("Not connected");
    var CONNECTED_TO(const String& address)
    {
        return "Connected to " + address;
    }

    /* Provides thread-safe access to the client's methods. While any instance is
    * instantiated, it has exclusive access to the client.
    * On return, public methods should maintain the invariants that:
    * - client.connectionStatus reflects the actual connection status of the client
    */
    class ClientHandle
    {
    public:
        ClientHandle() : lock(criticalSection) {}

        void addListener(NetComListener* listener)
        {
            connectionStatus.addListener(listener);
        }

        void removeListener(NetComListener* listener)
        {
            connectionStatus.removeListener(listener);
        }

        // makes sure the client is disconnected, creating a new client if necessary.
        void ensureDisconnected()
        {
            if (client->AreWeConnected() && !client->DisconnectFromServer())
            {
                // undetermined state - make new client object
                client.reallocateClient();
                jassert(!client->AreWeConnected());
            }

            if (connectionStatus != Value(NOT_CONNECTED))
            {
                connectionStatus = NOT_CONNECTED;
            }
        }

        // Returns true if successful.
        bool attemptConnection(const String& nameOrAddress)
        {
            ensureDisconnected();
            bool success = client->ConnectToServer(nameOrAddress.toWideCharPointer());
            if (success)
            {
                connectionStatus = CONNECTED_TO(nameOrAddress);
            }
            return success;
        }

        // access client methods
        NetComClient* operator->()
        {
            return client.i_client;
        }

    private:

        // encapuslates allocating and deallocating actual client using factory methods.
        // after returning from any public method, should refer to a valid client object.
        class Client
        {
        public:
            Client() : i_client(GetNewNetComClient()) {}
            ~Client()
            {
                // try to disconnect before deleting
                if (i_client->AreWeConnected()) { i_client->DisconnectFromServer(); }
                DeleteNetComClient(i_client);
            }

            // delete and reallocate the client (for exceptional circumstances)
            void reallocateClient()
            {
                NetComClient* oldClient = i_client;
                i_client = GetNewNetComClient();
                DeleteNetComClient(oldClient);
            }

            // access client methods
            NetComClient* operator->()
            {
                return i_client;
            }

            NetComClient* i_client;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Client);
        };

        // when you have a ClientHandle, this guarantees you have the *only* ClientHandle.
        ScopedLock lock;

        // the one client object (abstracts over any connection problems, to the extent possible...)
        static Client client;

        // contains a string reporting the current connection status of client
        static Value connectionStatus;

        // the one critical section controlling access to client and connectionStatus
        static CriticalSection criticalSection;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClientHandle);
    };

    // static member initializations
    ClientHandle::Client ClientHandle::client;
    Value ClientHandle::connectionStatus(NOT_CONNECTED);
    CriticalSection ClientHandle::criticalSection;
}

/* ------ NetComListener -------- */

NetComListener::NetComListener()
{
    ClientHandle().addListener(this);
}

NetComListener::~NetComListener()
{
    ClientHandle().removeListener(this);
}

void NetComListener::valueChanged(Value& value)
{
    netComConnectionChanged(value.toString());
}

/* ------- NetComInterface ---------- */

bool NetComInterface::connect(const String& nameOrAddress)
{
    return ClientHandle().attemptConnection(nameOrAddress);
}

void NetComInterface::disconnect()
{
    ClientHandle().ensureDisconnected();
}

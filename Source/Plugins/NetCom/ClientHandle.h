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

#ifndef CLIENT_HANDLE_H_INCLUDED
#define CLIENT_HANDLE_H_INCLUDED

/* Provides thread-safe access to the client's methods. While any instance is
*  instantiated, it has exclusive access to the client.
*  If calling a single method, you can use a temporary instead of declaring an instance
*  until the end of the scope, for example:
*
*  ClientHandle().attemptConnection("localhost");
*/

#include "../../../JuceLibraryCode/JuceHeader.h"
#include <NetComClient.h>

class ClientHandle
{
public:
    class Listener : public Value::Listener
    {
    public:
        /* automatically registers itself with the client */
        Listener();

        /* deregisters itself */
        virtual ~Listener();

        /* status is an empty string if disconnected, else the connected name/address. */
        virtual void netComConnectionChanged(const String& status);
        virtual void netComDataReceived(NlxDataTypes::CRRec* records, int numRecords, const String& objectName);
        virtual void netComEventsReceived(NlxDataTypes::EventRec* records, int numRecords, const String& objectName);

    private:
        // implements Value::Listener
        void valueChanged(Value& value) override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Listener);
    };

private:
    /* encapuslates allocating and deallocating actual client using factory methods.
     * after returning from any public method, should refer to a valid client object.
     */
    class Client
    {
    public:
        Client();
        ~Client();

        // delete and reallocate the client (for exceptional circumstances)
        void reallocateClient();

        void addListener(Listener* listener);
        void removeListener(Listener* listener);

        // tries to connect to a computer name or IP address. returns true if successful.
        bool connectToServer(const String& nameOrAddress);

        // makes sure the client is disconnected, creating a new client if necessary.
        void disconnect();

        // returns whether we are connected, and along the way updates connection status if necessary.
        bool checkConnection();

    private:
        NlxNetCom::NetComClient* i_client;

        // contains a string reporting the address we are connected to, or empty if not connected.
        Value connectionStatus;

        ListenerList<Listener, Array<Listener*, CriticalSection>> listeners;

        // NetCom callbacks
        static void handleConnectionLost(void* client);
        static void handleData(void* client, NlxDataTypes::CRRec* records, int numRecords, const wchar_t objectName[]);
        static void handleEvents(void* client, NlxDataTypes::EventRec* records, int numRecords, const wchar_t* const objectName);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Client);
    };

public:
    ClientHandle();

    // access Client methods directly
    Client* operator->();

private:

    // when you have a ClientHandle, this guarantees you have the *only* ClientHandle.
    ScopedLock lock;

    // the one client object (abstracts over any connection problems, to the extent possible...)
    static Client client;

    // the one critical section controlling access to client and connectionStatus
    static CriticalSection criticalSection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClientHandle);
};

typedef ClientHandle::Listener NetComListener;

#endif // CLIENT_HANDLE_H_INCLUDED
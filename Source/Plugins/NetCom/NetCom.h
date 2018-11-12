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

#ifndef NETCOM_H_INCLUDED
#define NETCOM_H_INCLUDED

/* Provides thread-safe access to the client's methods. While any instance is
*  instantiated, it has exclusive access to the client.
*  If calling a single method, you can use a temporary instead of declaring an instance
*  until the end of the scope, for example:
*
*  ClientHandle().attemptConnection("localhost");
*/

#include "../../../JuceLibraryCode/JuceHeader.h"
#include <vector>
#include <NetComClient.h>

enum AcqStatus : int
{
    UNKNOWN,
    IDLE,
    ACQUIRING,
    RECORDING
};

struct DIODeviceInfo
{
    enum Direction : bool { IN, OUT };
    int numPorts;
    int bitsPerPort;
    std::vector<bool> portDirection; // size == numPorts
};

struct DataChannelInfo
{

};

class NetComListener : public Value::Listener
{
public:
    /* automatically registers itself with the client */
    NetComListener();

    /* deregisters itself */
    virtual ~NetComListener();

    /* status is an empty string if disconnected, else the connected name/address. */
    virtual void netComConnectionChanged(const String& status);
    virtual void netComAcquisitionChanged(int status);
    virtual void netComDataChannelsChanged(const StringArray& dataChannels);
    virtual void netComDIODevicesChanged(const HashMap<String, DIODeviceInfo>& deviceInfo);

    virtual void netComDataReceived(NlxDataTypes::CRRec* records, int numRecords, const String& objectName);
    virtual void netComEventsReceived(NlxDataTypes::EventRec* records, int numRecords, const String& objectName);

private:
    // implements Value::Listener
    void valueChanged(Value& value) override;

    // keep references to the client Values so we know which one is which
    Value connectionStatus;
    Value acquisitionStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetComListener);
};


class ClientHandle
{
private:
    /* encapuslates allocating and deallocating actual client using factory methods.
     * after returning from any public method, should refer to a valid client object.
     */
    class Client
    {
    public:
        Client();
        ~Client();

        void addListener(NetComListener* listener);
        void removeListener(NetComListener* listener);

        const Value& getConnectionStatusValue();
        const Value& getAcquisitionStatusValue();

        // tries to connect to a computer name or IP address. returns true if successful.
        bool connectToServer(const String& nameOrAddress);

        // makes sure the client is disconnected, creating a new client if necessary.
        void disconnect();

        /* Queries NetCom for continuous channels and digital IO devices. If any change is
         * detected, calls netComDataChannelsChanged and/or netComDIODevicesChanged on any callbacks.
         * If the check fails, returns false. Called on connection, and can be called again by manual request.
         */
        bool refreshChannels();

        /* Sends command. First element of args should be the command name, and the rest are
         * arguments. For commands with no arguments (or if it's more convenient to pass the whole
         * command as one long string), an overloaded version accepts a single string. 
         * Returns true on success, false on failure.
         *
         * If reply is not a null pointer, copies the reply (split into tokens by whitepace)
         * into the array it points to. If hasErrCode is true, interprets the first reply token
         * as an error code where 0 indicates success and -1 indicates error, and incorporates
         * this into the return value rather than the reply array.
         */
        bool sendCommand(const Array<var>& args, StringArray* reply = nullptr, bool hasErrCode = false);
        bool sendCommand(const StringArray& args, StringArray* reply = nullptr, bool hasErrCode = false);
        bool sendCommand(const String& command, StringArray* reply = nullptr, bool hasErrCode = false);
    private:
        // wraps GetNewNetComClient with some additional setup
        NlxNetCom::NetComClient* getNewClient();

        // delete and reallocate the client (for exceptional circumstances)
        void reallocateClient();

        /* Returns true if acquisition status was successfully determined and set.
         * Otherwise, the acquisition status is set to UNKNOWN and false is returned.
         */
        bool updateAcquisitionStatus();

        NlxNetCom::NetComClient* i_client;

        // contains a string reporting the address we are connected to, or empty if not connected.
        Value connectionStatus;

        // bool: true if acquiring, false otherwise.
        Value acquisitionStatus;

        ListenerList<NetComListener, Array<NetComListener*, CriticalSection>> listeners;
        
        StringArray openEventChannels;
        StringArray openCSCChannels;
        HashMap<String, DIODeviceInfo> knownDIODevices;

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

#endif // NETCOM_H_INCLUDED
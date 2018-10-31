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

#ifndef NETCOM_INTERFACE_H_INCLUDED
#define NETCOM_INTERFACE_H_INCLUDED

#include <CommonLibHeader.h>
#include <NetComClient.h>

/* Calls listener function when the status of the NetCom connection changes.  */
class COMMON_LIB NetComListener : public Value::Listener
{
public:
    /* automatically registers itself with the connection. */
    NetComListener();

    /* deregisters itself */
    ~NetComListener();

    /* If connectedServer is null, the client is disconnected. */
    virtual void netComConnectionChanged(const String& status) = 0;

private:
    // implements Value::Listener
    void valueChanged(Value& value) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetComListener);
};

/* A way to control the *global* NetCom client. Changes such as connecting and disconnecting
 * to/from a server will affect all NetComInterface users (which can be informed of events
 * using a NetComListener).
 */
class COMMON_LIB NetComInterface
{
public:

    /* Attempts to make a connection to the given PC name or IP address, closing any existing
     * connnection. If unsuccessful, returns false and does not attempt to reconnect to the
     * previous server, if any.
     */
    bool connect(const String& nameOrAddress);

    /* Disconnects from current server, if any (always succeeds) */
    void disconnect();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetComInterface);
};

#endif // NETCOM_INTERFACE_H_INCLUDED
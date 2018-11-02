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

#include "NetComDataThread.h"

NetComDataThread::NetComDataThread(SourceNode* sn)
    : DataThread        (sn)
    , connectedToServer (false)
{}

NetComDataThread::~NetComDataThread() {}

bool NetComDataThread::updateBuffer()
{
    // sleep indefinitely...
    wait(-1);

    // Could either be notified due to acquisition stopping (normal) or
    // a connection change (abnormal). Check whether thread has been signaled to exit
    // to tell which is the case. If so, acquisition is stopping normally.
    return threadShouldExit();
}

bool NetComDataThread::foundInputSource()
{
    return connectedToServer;
}

bool NetComDataThread::startAcquisition()
{
    startThread();
}

bool NetComDataThread::stopAcquisition()
{
    signalThreadShouldExit();
    notify();
}

void NetComDataThread::netComConnectionChanged(const String& status)
{
    connectedToServer = !(status == "");
    if (isThreadRunning())
    {
        // waking the thead up without signaling that it should exit indicates acquisition error.
        notify();
    }
}

void NetComDataThread::netComDataReceived(NlxDataTypes::CRRec* records,
    int numRecords, const String& objectName)
{

}

void NetComDataThread::netComEventsReceived(NlxDataTypes::EventRec* records,
    int numRecords, const String& objectName)
{

}
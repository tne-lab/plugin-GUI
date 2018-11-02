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

#ifndef NETCOM_DATA_H_INCLUDED
#define NETCOM_DATA_H_INCLUDED

#include <DataThreadHeaders.h>
#include "../NetCom.h"

class NetComDataThread : public DataThread, public NetComListener
{
public:
    NetComDataThread(SourceNode* sn);
    ~NetComDataThread();

    /* Doesn't use updateBuffer to actually update the buffer, since that's handled by
     * the NetComListener callbacks. But this is responsible for stopping acquisition if
     * something unexpected happens.
     */
    bool updateBuffer() override;

    bool foundInputSource() override;

    bool startAcquisition() override;
    bool stopAcquisition() override;

    /* ------ NetComListener callbacks ------- */
    
    /* Update connectedToServer based on the connection status.
     * If this gets called while acquisition is active, also stop acquisition with error.
     */
    void netComConnectionChanged(const String& status) override;

    void netComDataReceived(NlxDataTypes::CRRec* records, int numRecords, const String& objectName) override;
    void netComEventsReceived(NlxDataTypes::EventRec* records, int numRecords, const String& objectName) override;

private:

    bool connectedToServer;
};

#endif // NETCOM_DATA_H_INCLUDED
/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2018 Translational NeuroEngineering Laboratory, MGH

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

#include "MeanSpikeRate.h"
#include "MeanSpikeRateEditor.h"

MeanSpikeRate::MeanSpikeRate()
    : GenericProcessor          ("Mean Spike Rate")
    , numSamplesProcessed       (0)
    , currMean                  (0.0f)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
}

MeanSpikeRate::~MeanSpikeRate() {}

AudioProcessorEditor* MeanSpikeRate::createEditor()
{
    editor = new MeanSpikeRateEditor(this);
    return editor;
}

void MeanSpikeRate::process(AudioSampleBuffer& continuousBuffer)
{

}

// private
int MeanSpikeRate::getNumActiveElectrodes()
{
    auto editor = static_cast<MeanSpikeRateEditor*>(getEditor());
    return editor->getNumActiveElectrodes();
}
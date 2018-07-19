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

#ifndef MEAN_SPIKE_RATE_H_INCLUDED
#define MEAN_SPIKE_RATE_H_INCLUDED

#include <ProcessorHeaders.h>

class MeanSpikeRate : public GenericProcessor
{
    friend class MeanSpikeRateEditor;

public:
    MeanSpikeRate();
    ~MeanSpikeRate();

    bool hasEditor() const { return true; }
    AudioProcessorEditor* createEditor() override;

    void process(AudioSampleBuffer& continuousBuffer) override;

private:
    // functions
    int getNumActiveElectrodes();

    // internals
    int numSamplesProcessed; // per-buffer - allows processing samples while handling events
    float currMean; // saved from one buffer to the next

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeanSpikeRate);
};

#endif // MEAN_SPIKE_RATE_H_INCLUDED
/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2019 Translational NeuroEngineering Laboratory

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

#ifndef COHERENCE_NODE_H_INCLUDED
#define COHERENCE_NODE_H_INCLUDED

/*

Coherence Node - continuously compute and display magnitude-squared coherence
(measure of phase synchrony) between pairs of LFP signals for a set of frequencies
of interest. Displays either raw coherence values or change from a saved baseline,
in units of z-score. 

*/

#include <ProcessorHeaders.h>
#include <VisualizerEditorHeaders.h>

#include "CoherenceVisualizer.h"
#include "AtomicSynchronizer.h"
#include "CumulativeTFR.h"

#include <vector>

class CoherenceNode : public GenericProcessor, public Thread
{
public:
    CoherenceNode();
    ~CoherenceNode();

    bool hasEditor() const override;

    AudioProcessorEditor* createEditor() override;

    void createEventChannels() override;

    void setParameter(int parameterIndex, float newValue) override;

    void process(AudioSampleBuffer& continuousBuffer) override;

    bool enable() override;
    bool disable() override;

    // thread function - coherence calculation
    void run() override;

    // Handle changing channels/groups
    void updateSettings() override;

    // Returns array of active input channels 
    Array<int> getActiveInputs();

    // Get source info
    int getFullSourceID(int chan);

    // Save info
    void saveCustomChannelParametersToXml(XmlElement* channelElement, int channelNumber, InfoObjectCommon::InfoObjectType channelType) override;
    void loadCustomChannelParametersFromXml(XmlElement* channelElement, InfoObjectCommon::InfoObjectType channelType) override;



private:
    AtomicSynchronizer dataSync;        // writer = process function , reader = thread
    AtomicSynchronizer coherenceSync;   // writer = thread, reader = visualizer (message thread)
    
    // group of 3, controlled by coherenceSync:
    //Array<std::vector<double>> meanCoherence;
    //Array<AudioBuffer<float>> dataBuffer; // Need to figure out size of this buffer. 8 seconds long
    AtomicallyShared<FFTWArray> dataBuffer;
    AtomicallyShared<std::vector<std::vector<double>>> meanCoherence;

    AtomicScopedWritePtr<FFTWArray> dataWriter;
    AtomicScopedReadPtr<std::vector<std::vector<double>>> coherenceReader;
    
    CumulativeTFR* TFR;
    Array<bool> CHANNEL_READY;

    // freq of interest
    Array<float> foi;

    // Segment Length
    int segLen;  // 8 seconds
    // Window Length
    int winLen;  // 2 seconds
    // Step Length
    float stepLen; // Iterval between times of interest
    // Interp Ratio ??
    int interpRatio; //

    // Array of channels for regions 1 and 2
    Array<int> group1Channels;
    Array<int> group2Channels;

    // returns the region for the requested channel
    int getChanGroup(int chan);

    ///// TFR vars
    // Number of channels for region 1
    int nGroup1Chans;
    // Number of channels for region 2
    int nGroup2Chans;
    // Number of freq of interest
    int nFreqs;
    // Number of times of interest
    int nTimes;
    // Fs (sampling rate?)
    float Fs;

    int nSamplesAdded; // holds how many samples were added for each channel
    AudioBuffer<float> channelData; // Holds the segment buffer for each channel.


    // Total Combinations
    int nGroupCombs;

    // from 0 to 10
    static const int COH_PRIORITY = 5;



    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoherenceNode);
};


class CoherenceEditor : public VisualizerEditor
{
public:
    CoherenceEditor(CoherenceNode* n);

    Visualizer* createNewCanvas() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoherenceEditor);
};

#endif // COHERENCE_NODE_H_INCLUDED
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

#include "CoherenceNode.h"
#include "CoherenceNodeEditor.h"
/********** node ************/
CoherenceNode::CoherenceNode()
    : GenericProcessor  ("Coherence")
    , Thread            ("Coherence Calc")
    , segLen            (4)
    , freqStep          (1)
    , freqStart         (1)
    , freqEnd           (40)
    , stepLen           (0.1)
    , winLen            (2)
    , interpRatio       (2)
    , nGroup1Chans      (0)
    , nGroup2Chans      (0)
    , Fs                (0)
{
    setProcessorType(PROCESSOR_TYPE_SINK);
}

CoherenceNode::~CoherenceNode()
{}

void CoherenceNode::createEventChannels() 
{}

AudioProcessorEditor* CoherenceNode::createEditor()
{
    editor = new CoherenceEditor(this);
    return editor;
}


void CoherenceNode::process(AudioSampleBuffer& continuousBuffer)
{  
    AtomicScopedWritePtr<Array<FFTWArray>> dataWriter(dataBuffer);
    AtomicScopedReadPtr<std::vector<std::vector<double>>> coherenceReader(meanCoherence);
    //// Get current coherence vector ////
    if (meanCoherence.hasUpdate())
    {
        coherenceReader.pullUpdate();
        // Do something with coherence!
    }
   
    ///// Add incoming data to data buffer. Let thread get the ok to start at 8seconds ////
    // Check writer
    if (!dataWriter.isValid())
    {
        jassert("atomic sync data writer broken");
    }

    //for loop over active channels and update buffer with new data
    Array<int> activeInputs = getActiveInputs();
    int nActiveInputs = activeInputs.size();
    int nSamples = 0;
    for (int activeChan = 0; activeChan < nActiveInputs; ++activeChan)
    {
        int chan = activeInputs[activeChan];
        nSamples = getNumSamples(chan); // all channels the same?
        if (nSamples == 0)
        {
            continue;
        }

        // Get read pointer of incoming data to move to the stored data buffer
        const float* rpIn = continuousBuffer.getReadPointer(chan);

        // Handle overflow 
        if (nSamplesAdded + nSamples >= segLen * Fs)
        {
            nSamples = segLen * Fs - nSamplesAdded;
        }

        // Add to buffer the new samples.
        for (int n = 0; n < nSamples; n++)
        {
            dataWriter->getReference(activeChan).set(n, rpIn[n]);
        }  
    }

    nSamplesAdded += nSamples;

    // channel buf is full. Update buffer.
    if (nSamplesAdded >= segLen * Fs)
    {
        dataWriter.pushUpdate();
        // Reset samples added
        nSamplesAdded = 0;
        //updateDataBufferSize();
    }
}

void CoherenceNode::run()
{  
    AtomicScopedReadPtr<Array<FFTWArray>> dataReader(dataBuffer);
    AtomicScopedWritePtr<std::vector<std::vector<double>>> coherenceWriter(meanCoherence);
    
    while (!threadShouldExit())
    {
        //// Check for new filled data buffer and run stats ////

        Array<int> activeInputs = getActiveInputs();
        int nActiveInputs = activeInputs.size();
        if (dataBuffer.hasUpdate())
        {
            dataReader.pullUpdate();

            for (int activeChan = 0; activeChan < nActiveInputs; ++activeChan)
            {
                int chan = activeInputs[activeChan];
                // get buffer and send it to TFR
                // Check to make sure channel is in one of our groups
                int groupNum = getChanGroup(chan);
                if (groupNum != -1)
                {
                    TFR->addTrial(dataReader->getReference(activeChan).getReadPointer(activeChan), chan);
                }
                else
                {
                    // channel isn't part of group 1 or 2
                    jassert("ungrouped channel");
                }
            }

            //// Get and send updated coherence  ////
            if (!coherenceWriter.isValid())
            {
                jassert("atomic sync coherence writer broken");
            }

            // Calc coherence at each combination of interest
            for (int itX = 0, comb = 0; itX < nGroup1Chans; itX++)
            {
                int chanX = group1Channels[itX];
                for (int itY = 0; itY < nGroup2Chans; itY++, comb++)
                {
                    int chanY = group2Channels[itY];
                    TFR->getMeanCoherence(chanX, chanY, coherenceWriter->at(comb).data(), comb);
                }
            }

            // Update coherence and reset data buffer
            std::cout << "coherence at freq 20, comb 1: " << coherenceWriter->at(0)[20] << std::endl;
            coherenceWriter.pushUpdate();
        }
    }
}

void CoherenceNode::updateDataBufferSize(int newSize)
{
    int totalChans = nGroup1Chans + nGroup2Chans;

    // no writers or readers can exist here
    // so this probably can't be called during acquisition
    // I changed it because it doesn't really make sense to just change the buffer we're currently
    // writing to when acquisition is stopped, and we need to figure out how to
    // make changes when acquisition is running anyway.
    dataBuffer.map([=](Array<FFTWArray>& arr)
    {
        for (int i = 0; i < jmin(totalChans, arr.size()); i++)
        {
            arr.getReference(i).resize(newSize);
        }

        int nChansChange = totalChans - arr.size();
        if (nChansChange > 0)
        {
            for (int i = 0; i < nChansChange; i++)
            {
                arr.add(FFTWArray(newSize));
            }
        }
        else if (nChansChange < 0)
        {
            arr.removeLast(-nChansChange);
        }
    });
}

void CoherenceNode::updateMeanCoherenceSize()
{
    meanCoherence.map([=](std::vector<std::vector<double>>& vec)
    {
        
        // Update meanCoherence size to new num combinations
        int nCombChange = nGroupCombs - vec.size();
        if (nCombChange > 0)
        {
            for (int i = 0; i < nCombChange; i++)
            {
                vec.push_back(std::vector<double>());
            }
        }
        else if (nCombChange < 0)
        {
            vec.resize(nGroupCombs);
        }

        // Update meanCoherence to new num freq at each combination
        int nFreqChange = nFreqs - vec[0].size();
        if (nFreqChange > 0)
        {
            for (int comb = 0; comb < vec.size(); comb++)
            {
                for (int i = 0; i < nFreqChange; i++)
                {
                    vec[comb].emplace_back(nFreqs);
                }
            }
        }
        else if (nFreqChange < 0)
        {
            for (int comb = 0; comb < vec.size(); comb++)
            {
                vec[comb].resize(nFreqs);
            }
        }     
    });
}

void CoherenceNode::updateSettings()
{
    // Array of samples per channel and if ready to go
    nSamplesAdded = 0;
    
    // (Start - end freq) / stepsize
    //freqStep = 1.0/float(winLen*interpRatio);
    freqStep = 1; // for debugging easier
    nFreqs = int((freqEnd - freqStart) / freqStep);
    // foi = 0.5:1/(win_len*interp_ratio):30

    // Set channels in group (need to update from editor)
    group1Channels.clear();
    //group1Channels.addArray({ 1, 2, 3, 4, 5, 6, 7, 8 });
    group1Channels.addArray({ 0 });
    group2Channels.clear();
    //group2Channels.addArray({ 9, 10, 11, 12, 13, 14, 15, 16 });
    group2Channels.addArray({ 1 });

    // Set number of channels in each group
    nGroup1Chans = group1Channels.size();
    nGroup2Chans = group2Channels.size();
    nGroupCombs = nGroup1Chans * nGroup2Chans;

    // Seg/win/step/interp - move to params eventually
    interpRatio = 2;

    if (nGroup1Chans > 0)
    {
        float newFs = getDataChannel(group1Channels[0])->getSampleRate();
        if (newFs != Fs)
        {
            Fs = newFs;
            updateDataBufferSize(segLen * Fs);
        }
    }

    // Trim time close to edge
    int nSamplesWin = winLen * Fs;
    int nTimes = ((segLen * Fs) - (nSamplesWin)) / Fs * (1 / stepLen) + 1; // Trim half of window on both sides, so 1 window length is trimmed total

    float alpha = 0.2; // exponential weighting of current segment, 0 is linear

    updateMeanCoherenceSize();

    // Overwrite TFR 
	TFR = new CumulativeTFR(nGroup1Chans, nGroup2Chans, nFreqs, nTimes, Fs, winLen, stepLen, 
        freqStep, freqStart, interpRatio, segLen, alpha);
}

void CoherenceNode::setParameter(int parameterIndex, float newValue)
{
    // Set new region channels and such in here?
    switch (parameterIndex)
    {
    case SEGMENT_LENGTH:
        segLen = static_cast<int>(newValue);
        break;
    case WINDOW_LENGTH:
        winLen = static_cast<int>(newValue);
        break;
    case START_FREQ:
        freqStart = static_cast<int>(newValue);
        break;
    case END_FREQ:
        freqEnd = static_cast<int>(newValue);
        break;
    case STEP_LENGTH:
        stepLen = static_cast<float>(newValue);
        break;
    }

    // This generally shouldn't be called during acquisition (the way it is now, it will definitely
    // cause some issues if the thread is running). On the other hand, all the parameters here
    // could also cause problems if they're changed during acquisition. Since I think at least some
    // of them could be useful to change during a run, we should think about how to do that safely...
    //updateSettings();
}

int CoherenceNode::getChanGroup(int chan)
{
    if (group1Channels.contains(chan))
    {
        return 1;
    }
    else if (group2Channels.contains(chan))
    {
        return 2;
    }
    else
    {
        return -1; // Channel isn't in group 1 or 2. Error!
    }
}

int CoherenceNode::getGroupIt(int group, int chan)
{
    int groupIt;
    if (group == 1)
    {
        int * it = std::find(group1Channels.begin(), group1Channels.end(), chan);
        groupIt = it - group1Channels.begin();
    }
    else
    {
        int * it = std::find(group2Channels.begin(), group2Channels.end(), chan);
        groupIt = it - group2Channels.begin();
    }
    return groupIt;
}

bool CoherenceNode::enable()
{
    if (isEnabled)
    {
        // Start coherence calculation thread
        startThread(COH_PRIORITY);
    }
    return isEnabled;
}

bool CoherenceNode::disable()
{
    CoherenceEditor* editor = static_cast<CoherenceEditor*>(getEditor());
    editor->disable();

    signalThreadShouldExit();

    return true;
}


Array<int> CoherenceNode::getActiveInputs()
{
    int numInputs = getNumInputs();
    auto ed = static_cast<CoherenceEditor*>(getEditor());
    if (numInputs == 0 || !ed)
    {
        return Array<int>();
    }

    Array<int> activeChannels = ed->getActiveChannels();
    return activeChannels;
}


bool CoherenceNode::hasEditor() const
{
    return true;
}

void CoherenceNode::saveCustomChannelParametersToXml(XmlElement* channelElement, int channelNumber, InfoObjectCommon::InfoObjectType channelType)
{

}

void CoherenceNode::loadCustomChannelParametersFromXml(XmlElement* channelElement, InfoObjectCommon::InfoObjectType channelType)
{

}
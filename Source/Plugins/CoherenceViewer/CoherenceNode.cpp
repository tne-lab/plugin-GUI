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
    , segLen            (8)
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
    //AtomicScopedReadPtr<std::vector<std::vector<double>>> coherenceReader(meanCoherence);
    //// Get current coherence vector ////
    //if (meanCoherence.hasUpdate())
    //{
        //coherenceReader.pullUpdate();
        // Do something with coherence!
    //}
   
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
        if (dataBuffer.hasUpdate())
        {
            dataReader.pullUpdate();
            Array<int> activeInputs = getActiveInputs();
            int nActiveInputs = activeInputs.size();
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
           /* for (int f = 0; f < nFreqs; f++)
            {
                std::cout << "coherence at freq X, comb 1: " << coherenceWriter->at(0)[f] << std::endl;
            }*/
            std::cout << "coherence update!" << std::endl;

            coherenceWriter.pushUpdate();
        }
    }
}

void CoherenceNode::updateDataBufferSize(int newSize)
{
    int totalChans = nGroup1Chans + nGroup2Chans;

    // no writers or readers can exist here
    // so this can't be called during acquisition
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
        vec.resize(nGroupCombs);

        // Update meanCoherence to new num freq at each existing combination
        for (int comb = 0; comb < nGroupCombs; comb++)
        {
            vec[comb].resize(nFreqs);
        }
    });
}

void CoherenceNode::updateSettings()
{
    // Array of samples per channel and if ready to go
    nSamplesAdded = 0;
    
    // (Start - end freq) / stepsize
    freqStep = 1.0/float(winLen*interpRatio);
    //freqStep = 1; // for debugging
    nFreqs = int((freqEnd - freqStart) / freqStep);
    // foi = 0.5:1/(win_len*interp_ratio):30

    group1Channels.clear();
    group2Channels.clear();

    // Default to this. Probably will move to canvas tab.
    Array<int> numActiveInputs(getActiveInputs());
    if (numActiveInputs.size() > 0)
    {
        for (int i = 0; i < numActiveInputs.size(); i++)
        {
            if (i < numActiveInputs.size() / 2)
            {
                group1Channels.add(numActiveInputs[i]);
            }
            else
            {
                group2Channels.add(numActiveInputs[i]);
            }
        }

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

        float alpha = 0; // exponential weighting of current segment, 0 is linear

        updateMeanCoherenceSize();

        // Overwrite TFR 
        TFR = new CumulativeTFR(nGroup1Chans, nGroup2Chans, nFreqs, nTimes, Fs, winLen, stepLen,
            freqStep, freqStart, segLen, alpha);
    }
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
    if (group == 1)
    {
        int * it = std::find(group1Channels.begin(), group1Channels.end(), chan);
        return it - group1Channels.begin();
    }
    else if (group == 2)
    {
        int * it = std::find(group2Channels.begin(), group2Channels.end(), chan);
        return it - group2Channels.begin();
    }
    else
    {
        jassertfalse;
    }
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
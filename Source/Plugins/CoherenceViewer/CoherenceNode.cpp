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

/********** node ************/
CoherenceNode::CoherenceNode()
    : GenericProcessor  ("Coherence")
    , Thread            ("Coherence Calc")
    , dataWriter        (dataBuffer)
    , coherenceReader   (meanCoherence)
    , dataReader        (dataBuffer)
    , coherenceWriter   (meanCoherence)
    , segLen            (8)
    , nFreqs            (30)
    , nTimes            (10)
    , stepLen           (0.25)
    , winLen            (2)
    , interpRatio       (2)
    , nGroup1Chans      (0)
    , nGroup2Chans      (0)
    , Fs                (CoreServices::getGlobalSampleRate())
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
    std::cout << dataWriter->size() << std::endl;
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
   // std::cout << nSamplesAdded << " of " << segLen*Fs << std::endl;
    // channel buf is full. Update buffer.
    if (nSamplesAdded >= segLen * Fs)
    {
        dataWriter.pushUpdate();
        // Reset samples added
        nSamplesAdded = 0;
        updateDataBufferSize();
    }
}

void CoherenceNode::run()
{  
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
                // get buffer and send it to TFR to fun real-timBe-coherence calcs
                int groupNum = getChanGroup(chan);
                if (groupNum != -1)
                {
                    TFR->addTrial(dataReader->getReference(activeChan).getReadPointer(activeChan), chan, groupNum);
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
            // Calc coherence
            std::vector<std::vector<double>> TFRMeanCoh = TFR->getCurrentMeanCoherence();
            // For loop over combinations
            for (int comb = 0; comb < nGroupCombs; ++comb)
            {
                for (int freq = 0; freq < nFreqs; freq++)
                {
                    // freq lookup list
                    coherenceWriter->at(comb).at(freq) = TFRMeanCoh[comb][freq];
                }

            }
            // Update coherence and reset data buffer
            coherenceWriter.pushUpdate();
        }
    }
}

void CoherenceNode::updateDataBufferSize()
{
    dataWriter->clear();
    for (int i = 0; i < nGroup1Chans + nGroup2Chans; i++)
    {
        dataWriter->add(std::move(FFTWArray(segLen * Fs)));
    }
}

void CoherenceNode::updateSettings()
{
    // Array of samples per channel and if ready to go
    nSamplesAdded = 0;
    
    // foi (need to update from editor)
    foi.addArray({ 1, 2, 3, 4 , 5, 6});
    nFreqs = foi.size();

    // Set channels in group (need to update from editor)
    group1Channels.addArray({ 1, 2, 3, 4, 5, 6, 7, 8 });
    group2Channels.addArray({ 9, 10, 11, 12, 13, 14, 15, 16 });

    // Set number of channels in each group
    nGroup1Chans = group1Channels.size();
    nGroup2Chans = group2Channels.size();
    nGroupCombs = nGroup1Chans * nGroup2Chans;

    // Seg/win/step/interp - move to params eventually
    segLen = 8;
    winLen = 2;
    stepLen = 0.1;
    interpRatio = 2;

    // Trim time close to edge
    int nSamplesWin = winLen * Fs;
    int trimTime = nSamplesWin / 2 * 2; // /2 * 2 to convey half of a window on both ends of the segment
    int nTimes = (segLen * Fs) - trimTime;

    dataWriter->clear();

    coherenceWriter->resize(nGroup1Chans + nGroup2Chans);
    for (int i = 0; i < nGroup1Chans + nGroup2Chans; i++)
    {
        coherenceWriter->at(i).resize(segLen * Fs);
        dataWriter->add(std::move(FFTWArray(segLen * Fs)));
    }

    // Overwrite TFR 
	TFR = new CumulativeTFR(nGroup1Chans, nGroup2Chans, nFreqs, nTimes, Fs, foi, winLen, stepLen, interpRatio, segLen);
}

void CoherenceNode::setParameter(int parameterIndex, float newValue)
{
    // Set new region channels and such in here?
    updateSettings();
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


/************** editor *************/
CoherenceEditor::CoherenceEditor(CoherenceNode* p)
    : VisualizerEditor(p, false)
{
    tabText = "Coherence";
}


Visualizer* CoherenceEditor::createNewCanvas()
{
    canvas = new CoherenceVisualizer();
    return canvas;
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
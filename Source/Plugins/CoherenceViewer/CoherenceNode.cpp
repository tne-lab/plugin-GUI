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
    , segLen            (8)
    , nFreqs            (30)
    , nTimes            (10)
    , Fs                (CoreServices::getGlobalSampleRate())
{
    setProcessorType(PROCESSOR_TYPE_SINK);
}


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
            nSamples = segLen - nSamplesAdded;
        }

        // Add to buffer the new samples. Use copyFrom ?
        for (int n = 0; n < nSamples; n++)
        {
            dataWriter->set(n + nSamplesAdded, rpIn[n]);
        }  
    }

    nSamplesAdded += nSamples;
    // channel buf is full. Update buffer.
    if (nSamplesAdded >= segLen * Fs)
    {
        dataWriter.pushUpdate();
        // Reset samples added
        nSamplesAdded = 0;
    }
}




void CoherenceNode::run()
{
    AtomicScopedWritePtr<std::vector<std::vector<double>>> coherenceWriter(meanCoherence);
    AtomicScopedReadPtr<FFTWArray> dataReader(dataBuffer);
    
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
                // get buffer and send it to TFR to fun real-time-coherence calcs
                int groupNum = getChanGroup(activeChan);
                FFTWArray fftArray;
                fftArray.copyFrom(dataReader.operator*, segLen * Fs);
                if (groupNum != -1)
                {
                    TFR->addTrial(fftArray, activeChan, groupNum);
                }
                else
                {
                    // channel isn't part of group 1 or 2
                    jassert("ungrouped channel");
                }
            }

            //// Get and send updated coherence  ////
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
            dataBuffer = new AtomicallyShared<AudioBuffer<float>>(nGroup1Chans + nGroup2Chans, segLen * Fs);
        }
    }
}

void CoherenceNode::updateSettings()
{
    // Init group of 3 vectors that are synced with data/coherencesync    
    dataBuffer = new AtomicallyShared<AudioBuffer<float>>(nGroup1Chans + nGroup2Chans, segLen * Fs);
    meanCoherence = new AtomicallyShared<std::vector<std::vector<double>>>(nGroup1Chans + nGroup2Chans, nFreqs);

    // Link writer/reader to newly created group of 3s
    dataWriter = AtomicScopedWritePtr<FFTWArray>(dataBuffer);
    coherenceReader = AtomicScopedReadPtr<std::vector<std::vector<double>>>(meanCoherence);

    // Array of samples per channel and if ready to go
    nSamplesAdded = 0;
    
    // Overwrite TFR 
	TFR = new CumulativeTFR(nGroup1Chans, nGroup2Chans, nFreqs, nTimes, Fs, foi, segLen, winLen, stepLen, interpRatio);
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

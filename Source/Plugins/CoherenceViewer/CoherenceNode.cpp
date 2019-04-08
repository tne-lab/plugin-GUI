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
    , dataWriter        (dataSync.getWriter())
    , coherenceReader   (coherenceSync.getReader())
    , nFreqs            (1)
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
    int coherenceIndex = coherenceReader->pullUpdate();
    std::vector<double> curCoherence = meanCoherence.at(coherenceIndex);

    // Do something with coherence!

    ///// Add incoming data to data buffer. Let thread get the ok to start at 8seconds ////
    
    // Get our current index for data buffer
    int curDataBufferIndex = dataWriter->getIndexToUse();

    // Get our data buffer, index from our atomic sync object
    AudioBuffer<float>curBuffer = dataBuffer.at(curDataBufferIndex);

    //for loop over active channels and update buffer with new data
    Array<int> activeInputs = getActiveInputs();
    int nActiveInputs = activeInputs.size();
    int nSamplesAdded = 0; // int big enough?? vector?
    for (int activeChan = 0; activeChan < nActiveInputs; ++activeChan)
    {
        int chan = activeInputs[activeChan];
        int nSamples = getNumSamples(chan); // all channels the same?
        if (nSamples == 0)
        {
            continue;
        }

        // Get read pointer of incoming data to move to the stored data buffer
        const float* rpIn = continuousBuffer.getReadPointer(chan);

        curBuffer.copyFrom(chan, nSamplesAdded, rpIn, nSamples);

        nSamplesAdded += nSamples; // Make this a vector??
    }

    

    if (nSamplesAdded >= 8 * Fs) // 8 seconds of samples
    {
        // Push update here?
        SEGMENT_DONE = true;
        // Let sync know that data is updated
        dataWriter->pushUpdate();
    }
}




void CoherenceNode::run()
{
    AtomicReaderPtr dataReader = dataSync.getReader();
    AtomicWriterPtr coherenceWriter = coherenceSync.getWriter();
    
    while (!threadShouldExit())
    {
        //// Check for new filled data buffer and run stats ////
        int curDataIndex  = dataReader->pullUpdate();
        if (curDataIndex != -1) 
        {
            // Get our current data buffer, index from our atomic sync object
            // How do we not redo the same data over and over again?
            AudioBuffer<float>curBuffer = dataBuffer.at(curDataIndex);
            if (SEGMENT_DONE)
            {
                TFR->addTrial(curBuffer);

                //// Send updated coherence  //// (only do this when addTrial happens?)
                int curCohIndex = coherenceWriter->getIndexToUse();
                // For loop over combinations?
                for (int comb = 0; comb < nGroupCombs; ++comb)
                {
                    if (curCohIndex != -1)
                    {
                        std::vector<double> * curMeanCoherence = &meanCoherence.at(curCohIndex);
                        curMeanCoherence->at(comb) = TFR->getCurrentMeanCoherence().at(comb);
                    }
                }
                coherenceWriter->pushUpdate();
            }   
        }   
    }
}

void CoherenceNode::updateSettings()
{
    // Reset synchronizers
    dataSync.reset();
    coherenceSync.reset();

    // Reset data buffer and meanCoherence vectors
    dataBuffer.clear();
    meanCoherence.clear();

    // Update TFR (probably change this into a function to be cleaner, depends on FFTW stuff)
	delete TFR;
	TFR = new CumulativeTFR(nGroup1Chans, nGroup2Chans, nFreqs, nTimes, Fs);
}

void CoherenceNode::setParameter(int parameterIndex, float newValue)
{
    // Set new region channels and such in here?
    

    updateSettings();
}

bool CoherenceNode::enable()
{
    if (isEnabled)
    {
        TFR = new CumulativeTFR(nGroup1Chans, nGroup2Chans, nFreqs, nTimes, Fs);
        // Start coherence calculation thread
        startThread(COH_PRIORITY);
    }
    return isEnabled;
}

/// CHECK THIS - COPIED FROM PHASECALCULATOR
Array<int> CoherenceNode::getActiveInputs()
{
    int numInputs = getNumInputs();
    auto ed = static_cast<CoherenceEditor*>(getEditor());
    if (numInputs == 0 || !ed)
    {
        return Array<int>();
    }

    Array<int> activeChannels = ed->getActiveChannels();
    int numToRemove = 0;
    for (int i = activeChannels.size() - 1;
        i >= 0 && activeChannels[i] >= numInputs;
        --i, ++numToRemove);
    activeChannels.removeLast(numToRemove);
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

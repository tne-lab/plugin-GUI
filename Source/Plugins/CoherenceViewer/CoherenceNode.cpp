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
    
    for (int activeChan = 0; activeChan < nActiveInputs; ++activeChan)
    {
        // Make sure buffer for this channel isn't already full
        if (!CHANNEL_READY[activeChan]) 
        {
            int chan = activeInputs[activeChan];
            int nSamples = getNumSamples(chan); // all channels the same?
            if (nSamples == 0)
            {
                continue;
            }

            // Get read pointer of incoming data to move to the stored data buffer
            const float* rpIn = continuousBuffer.getReadPointer(chan);
            
            // Handle overflow 
            if (nSamplesAdded[activeChan] + nSamples >= segLen * Fs) 
            {
                nSamples = segLen - nSamplesAdded[activeChan];
            }

            // Add to buffer the new samples. Use copyFrom ?
            curBuffer.addFrom(activeChan, nSamplesAdded[activeChan], rpIn, nSamples); 
            // Update num samples added
            nSamplesAdded.set(activeChan, nSamplesAdded[activeChan] + nSamples);

			// Think I push update here? Still unsure about sync stuff
			dataWriter->pushUpdate();

            // channel buf is full. Let thread know.
            if (nSamplesAdded[activeChan] >= segLen * Fs) 
            {             
                // Let thread know it can start on thsi chan
                CHANNEL_READY.set(activeChan, true);

                // Reset samples added
                nSamplesAdded.set(activeChan, 0);
            }
        }
    }
}




void CoherenceNode::run()
{
    AtomicReaderPtr dataReader = dataSync.getReader();
    AtomicWriterPtr coherenceWriter = coherenceSync.getWriter();
    
    while (!threadShouldExit())
    {
        //// Check for new filled data buffer and run stats ////

        Array<int> activeInputs = getActiveInputs();
        int nActiveInputs = activeInputs.size();

        for (int activeChan = 0; activeChan < nActiveInputs; ++activeChan)
        {
            // Channel buf is full and ready to be processed
            if (CHANNEL_READY[activeChan]) 
            {
                // get buffer and send it to TFR to fun real-time-coherence calcs
                int curDataIndex = dataReader->pullUpdate();
                if (curDataIndex != -1)
                {
                    TFR->addTrial(dataBuffer.getUnchecked(curDataIndex), activeChan,region);
                }
            }
        }

        // All buffers are full. Update coherence and start filling new buffer.
        if (!CHANNEL_READY.contains(false))
        {
            //// Send updated coherence  ////
            int curCohIndex = coherenceWriter->getIndexToUse();
			if (curCohIndex != -1)
			{
				std::vector<double> curMeanCoherence = meanCoherence.getUnchecked(curCohIndex); // sync probably changes here
				std::vector<std::vector<double>> TFRMeanCoh = TFR->getCurrentMeanCoherence(); 
				// For loop over combinations
				for (int comb = 0; comb < nGroupCombs; ++comb)
				{
					curMeanCoherence.assign(comb, TFRMeanCoh.at(comb)); // incorrect, cant do idexed assign for vector
				}
			}
            
            coherenceWriter->pushUpdate();
            
            // Reset audio buffer for new segment
            for (int i = 0; i < 3; i++)
            {
				// Believe this is also incorrect. Need change up based on new atomic sync
                dataBuffer.assign(i, AudioBuffer<float>(nGroup1Chans + nGroup2Chans, segLen*Fs));
            }

            // check if this actually works, kind of hacky.
            CHANNEL_READY.clearQuick(); 
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
    // Init group of 3 vectors that are synced with data/coherencesync
    for (int i = 0; i < 3; i++)
    {
        dataBuffer.push_back(AudioBuffer<float>(nGroup1Chans + nGroup2Chans, segLen*Fs));
        meanCoherence.push_back(std::vector<double>(nFreqs)); // Freq or channels here..?
    }

    // Array of samples per channel and if ready to go
    CHANNEL_READY.insertMultiple(-1, 0, nGroup1Chans + nGroup2Chans);
    nSamplesAdded.insertMultiple(-1, 0, nGroup1Chans + nGroup2Chans);
    
    // Overwrite TFR 
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

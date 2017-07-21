/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2014 Open Ephys

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

#include "PhaseCalculator.h"
#include "PhaseCalculatorEditor.h"

const double PI = 3.1415926535897;

unsigned int PhaseCalculator::numInstances = 0;

// local functions

namespace {

    /* 
     * arPredict: use autoregressive model of order to predict future data.
     * Input params is an array of coefficients of an AR model of length AR_ORDER.
     * Writes writeNum future data values starting at location writeStart.
     * *** assumes there are at least AR_ORDER existing data points *before* writeStart
     * to use to calculate future data points.
     */
    void arPredict(double* writeStart, int writeNum, double* params)
    {
        reverse_iterator<double*> dataIter;
        int i;
        for (i = 0; i < writeNum; i++)
        {
            dataIter = reverse_iterator<double*>(writeStart + i); // the reverse iterator actually starts out pointing at element i-1
            writeStart[i] = -inner_product<double*, reverse_iterator<double*>, double>(params, params + AR_ORDER, dataIter, 0);
        }
    }

    /*
    * hilbertManip: Hilbert transforms data in the frequency domain (including normalization by n)
    * Input n is the length of fft_data.
    */
    void hilbertManip(FFTWArray<complex<double>>& fftData)
    {
        int n = fftData.getLength();

        // Normalize DC and Nyquist, normalize and double prositive freqs, and set negative freqs to 0.
        int lastPosFreq = (int) round(ceil(n / 2.0) - 1);
        int firstNegFreq = (int) round(floor(n / 2.0) + 1);
        complex<double>* wp = fftData.getWritePointer();

        for (int i = 0; i < n; i++) {
            if (i > 0 && i <= lastPosFreq)
                // normalize and double
                wp[i] *= (2.0 / (double)n);
            else if (i < firstNegFreq)
                // normalize but don't double
                wp[i] /= (double)n;
            else
                // set to 0
                wp[i] = 0;
        }
    }

    /*
     * moveToFifo: move nData floats, starting at index "start", from an AudioBuffer to
     * a fifo constructed of an AudioBuffer and AbstractFifo
     */
    void moveToFifo(AbstractFifo* af, AudioSampleBuffer& from, int chanFrom,
        AudioSampleBuffer& to, int chanTo, int start, size_t nData)
    {
        // Tell abstract fifo we're about to add data, and get start positions and sizes to transfer.
        int start1, size1, start2, size2;
        af->prepareToWrite(nData, start1, size1, start2, size2);

        // Copy the first section
        const float* rp = from.getReadPointer(chanFrom, start);
        float* wp = to.getWritePointer(chanTo, start1);

        for (int i = 0; i < size1; i++)
            wp[i] = rp[i];

        // Copy the second section
        if (size2 > 0)
        {
            rp = from.getReadPointer(chanFrom, start + size1);
            wp = to.getWritePointer(chanTo, start2);

            for (int i = 0; i < size2; i++)
                wp[i] = rp[i];
        }
        
        // Tell abstract fifo how much data has been added
        af->finishedWrite(nData);
    }
}

// member functions

// public:

PhaseCalculator::PhaseCalculator() :
GenericProcessor("Phase Calculator"), Thread("AR Modeler"),
processLength(1 << START_PLEN_POW), numFuture(START_NUM_FUTURE),
calcInterval(START_AR_INTERVAL), glitchLimit(START_GL),
processADC(false), haveSentWarning(false),
lowCut(START_LOW_CUT), highCut(START_HIGH_CUT)
{
	setProcessorType(PROCESSOR_TYPE_FILTER);
    numInstances++;
    initialize();
}

PhaseCalculator::~PhaseCalculator() 
{
    // technically not necessary since the OwnedArrays are self-destructing, but calling fftw_cleanup prevents erroneous memory leak reports.
    pForward.clear();
    pBackward.clear();
    dataToProcess.clear();
    fftData.clear();
    dataOut.clear();
    if (--numInstances == 0)
        fftw_cleanup();
}

bool PhaseCalculator::hasEditor() const
{
    return true;
}


AudioProcessorEditor* PhaseCalculator::createEditor()
{
    editor = new PhaseCalculatorEditor(this);
    return editor;
}


void PhaseCalculator::setParameter(int parameterIndex, float newValue)
{
    int numInputs = getNumInputs();

    switch (parameterIndex) {
    case pQueueSize:
        // precondition: acquisition is stopped.
        // resize everything
        processLength = (int)newValue;
        initialize();
        break;

    case pNumFuture:
        // precondition: acquisition is stopped.
        setNumFuture((int)newValue);
        break;

    case pEnabledState:
        if (newValue == 0)
            shouldProcessChannel.set(currentChannel, false);
        else
            shouldProcessChannel.set(currentChannel, true);
        break;

    case pRecalcInterval:
        calcInterval = (int)newValue;
        break;

    case pGlitchLimit:
        glitchLimit = (int)newValue;
        break;

    case pAdcEnabled:
        processADC = (newValue > 0);
        break;

    case pLowcut:
        // precondition: acquisition is stopped.
        lowCut = (double)newValue;
        setFilterParameters();
        break;

    case pHighcut:
        // precondition: acquisition is stopped.
        highCut = (double)newValue;
        setFilterParameters();
        break;
    }
}

void PhaseCalculator::process(AudioSampleBuffer& buffer)
{
    // let's calculate some phases!

    int nChannels = buffer.getNumChannels();

    for (int chan = 0; chan < nChannels; chan++)
    {
        // do we even need to process this channel?

        // "+CH" button
        if (!shouldProcessChannel[chan])
            continue;
        
        // "+ADC/AUX" button
        DataChannel::DataChannelTypes type = getDataChannel(chan)->getChannelType();
        if (!processADC && (type == DataChannel::ADC_CHANNEL || type == DataChannel::AUX_CHANNEL))
            continue;

        int nSamples = getNumSamples(chan);
        if (nSamples == 0)
            continue;

#ifdef MARK_BUFFERS // for debugging (see description in header file)
        if (chan < 8)
        {
            addEvent(events, TTL, 0, 1, chan);
            addEvent(events, TTL, nSamples / 2, 0, chan);
        }
#endif
        
        // First, forward-filter the data.
        // Code from FilterNode.
        float* wpBuffer = buffer.getWritePointer(chan);
        forwardFilters[chan]->process(nSamples, &wpBuffer);

        // We have to put the new samples in the historyFifo for processing (with size historyLength).
        // If there are more samples than we have room to process, process the most recent samples and output zero
        // for the rest (this is an error that should be noticed and fixed).
        int startIndex = std::max(nSamples - historyLength, 0);
        int nSamplesToProcess = nSamples - startIndex;
        if (startIndex != 0)
        {
            // clear the extra samples and send a warning message
            buffer.clear(chan, 0, startIndex);
            if (!haveSentWarning)
            {
                CoreServices::sendStatusMessage("WARNING: Phase Calculator buffer is shorter than the sample buffer!");
                haveSentWarning = true;
            }
        }

        // see how full the channel's historyFifo currently is (by querying its AbstractFifo)
        AbstractFifo* myAF = fifoManager[chan];
        int freeSpace = myAF->getFreeSpace();

        // if buffer wasn't full, check whether it will be.
        bool willBecomeFull = (chanState[chan] == NOT_FULL && freeSpace <= nSamplesToProcess);
        
        // virtually clear up space
        int nSamplesToClear = std::max(0, nSamplesToProcess - freeSpace);
        myAF->finishedRead(nSamplesToClear);

        // enqueue new data
        moveToFifo(myAF, buffer, chan, historyFifo, chan, startIndex, nSamplesToProcess);

        // If the fifo is now full, write to the sharedDataBuffer.
        // This allows the AR calculating thread to read from dataToProcess and calculate the model.
        if (chanState[chan] != NOT_FULL || willBecomeFull)
        {
            // rotate and copy entire fifo to dataToProcess
            int start1, size1, start2, size2;
            myAF->prepareToRead(historyLength, start1, size1, start2, size2);

            const float* rpFifo1 = historyFifo.getReadPointer(chan, start1);
            const float* rpFifo2 = historyFifo.getReadPointer(chan, start2);

            // critical section for this channel's sharedDataBuffer
            // note that the floats are cast to doubles here - this is important to avoid over/underflow when calculating the phase.
            {
                const ScopedLock myScopedLock(*sdbLock[chan]);

                // write first part
                for (int i = 0; i < size1; i++)
                    sharedDataBuffer[chan]->set(i, (double)rpFifo1[i]);

                if (size2 > 0)
                    // write second part
                    for (int i = 0; i < size2; i++)
                        sharedDataBuffer[chan]->set(size1 + i, (double)rpFifo2[i]);
            }
            // end critical section

            if (chanState[chan] == NOT_FULL)
                // now that dataToProcess for this channel has data, let the thread start calculating the AR model.
                chanState.set(chan, FULL_NO_AR);
        }

        // calc phase and write out (only if AR model has been calculated)
        if (chanState[chan] == FULL_AR) {

            // copy data to dataToProcess
            double* rpSDB = sharedDataBuffer[chan]->getRawDataPointer();
            dataToProcess[chan]->copyFrom(rpSDB, historyLength);

            // use AR(20) model to predict upcoming data and append to dataToProcess
            double* wpProcess = dataToProcess[chan]->getWritePointer(historyLength);
            
            // quasi-atomic access of AR parameters
            Array<double> currParams;
            for (int i = 0; i < AR_ORDER; i++)
                currParams.set(i, (*arParams[chan])[i]);
            double* rpParam = currParams.getRawDataPointer();

            arPredict(wpProcess, numFuture, rpParam);

            // backward-filter the data
            //TODO: figure out why this isn't working

            dataToProcess[chan]->reverse();
            double* wpProcessReverse = dataToProcess[chan]->getWritePointer();
            backwardFilters[chan]->process(processLength, &wpProcessReverse);
            dataToProcess[chan]->reverse();

            //// Hilbert-transform dataToProcess
            //pForward[chan]->execute();      // reads from dataToProcess, writes to fftData
            //hilbertManip(*(fftData[chan]));
            //pBackward[chan]->execute();     // reads from fftData, writes to dataOut

            //// calculate phase and write out to buffer
            //const complex<double>* rpProcess = dataOut[chan]->getReadPointer(historyLength - nSamplesToProcess);

            //for (int i = 0; i < nSamplesToProcess; i++)
            //{
            //    // output in degrees
            //    // note that doubles are cast back to floats
            //    wpBuffer[i + startIndex] = (float)(std::arg(rpProcess[i]) * (180.0 / PI));
            //}

            // for debugging - uncomment below and comment above from "Hilbert transform" line to just output the filtered wave            
            const double* rpDTP = dataToProcess[chan]->getReadPointer(historyLength - nSamplesToProcess);
            for (int i = 0; i < nSamplesToProcess; i++)
            {
                wpBuffer[i + startIndex] = (float)rpDTP[i];
            }

            // unwrapping / smoothing
            //unwrapBuffer(wpBuffer, nSamples, chan);
            //smoothBuffer(wpBuffer, nSamples, chan);        
        }
        else // fifo not full / becoming full
        {            
            // just output zeros
            buffer.clear(chan, startIndex, nSamplesToProcess);
        }

        // keep track of last sample
        lastSample.set(chan, buffer.getSample(chan, nSamples - 1));
    }
}

void PhaseCalculator::unwrapBuffer(float* wp, int nSamples, int chan)
{
    for (int startInd = 0; startInd < nSamples - 1; startInd++)
    {
        float diff = wp[startInd] - (startInd == 0 ? lastSample[chan] : wp[startInd - 1]);
        if (abs(diff) > 180)
        {
            // search forward for a wrap in the opposite direction, for glitchLimit samples or until the end of the buffer, whichever comes first
            int endInd = -1;
            int currInd;
            for (currInd = startInd + 1; currInd <= startInd + glitchLimit && currInd < nSamples; currInd++)
            {
                float diff2 = wp[currInd] - wp[currInd - 1];
                if (abs(diff2) > 180 && ((diff > 0) != (diff2 > 0)))
                {
                    endInd = currInd;
                    break;
                }
            }
            // if it was an upward jump at the end of the buffer, *always* unwrap
            if (endInd == -1 && diff > 0 && currInd == nSamples)
                endInd = nSamples;

            // unwrap [startInd, endInd)
            for (int i = startInd; i < endInd; i++)
                wp[i] -= 360 * (diff / abs(diff));

            if (endInd > -1)
                // skip to the end of this unwrapped section
                startInd = endInd;
        }
    }
}

void PhaseCalculator::smoothBuffer(float* wp, int nSamples, int chan)
{
    int actualMaxSL = min(glitchLimit, nSamples - 1);
    float diff = wp[0] - lastSample[chan];
    if (diff < 0 && diff > -180)
    {
        // identify whether signal exceeds last sample of the previous buffer within glitchLimit samples.
        int endIndex = -1;
        for (int i = 1; i <= actualMaxSL; i++)
        {
            if (wp[i] > lastSample[chan])
            {
                endIndex = i;
                break;
            }
            // corner case where signal wraps before it exceeds lastSample
            else if (wp[i] - wp[i - 1] > 180 && (wp[i] + 360) > lastSample[chan])
            {
                wp[i] += 360;
                endIndex = i;
                break;
            }
        }

        if (endIndex != -1)
        {
            // interpolate points from buffer start to endIndex
            float slope = (wp[endIndex] - lastSample[chan]) / (endIndex + 1);
            for (int i = 0; i < endIndex; i++)
                wp[i] = lastSample[chan] + (i + 1) * slope;
        }
    }
}

// from FilterNode code
void PhaseCalculator::setFilterParameters()
{
    int nChan = getNumInputs();
    for (int chan = 0; chan < nChan; chan++)
    {
        Dsp::Params params;
        params[0] = getDataChannel(chan)->getSampleRate();  // sample rate
        params[1] = 2;                                      // order
        params[2] = (highCut + lowCut) / 2;                 // center frequency
        params[3] = highCut - lowCut;                       // bandwidth

        if (forwardFilters.size() > chan)
            forwardFilters[chan]->setParams(params);
        if (backwardFilters.size() > chan)
            backwardFilters[chan]->setParams(params);
    }
}

void PhaseCalculator::updateSettings()
{
    // update number of channels if necessary
    int currInputs = historyFifo.getNumChannels();
    if (getNumInputs() != currInputs)
        initialize();
}


// starts thread when acquisition begins
bool PhaseCalculator::enable()
{
    if (!isEnabled)
        return false;

    startThread(AR_PRIORITY);
    return true;
}

// resets things and ends thread when acquisition stops.
bool PhaseCalculator::disable()
{
   signalThreadShouldExit();

    // reset channel states
    for (int i = 0; i < chanState.size(); i++)
        chanState.set(i,NOT_FULL);
    
    // reset AbstractFifos (effectively clearing historyFifos)
    for (int i = 0; i < fifoManager.size(); i++)
        fifoManager[i]->reset();

    // reset last sample containers
    for (int i = 0; i < lastSample.size(); i++)
        lastSample.set(i, 0);

    // reset buffer overflow warning
    haveSentWarning = false;

    return true;
}

bool PhaseCalculator::getProcessADC()
{
    return processADC;
}

bool PhaseCalculator::getEnabledStateForChannel(int chan)
{
    if (chan >= 0 && chan < shouldProcessChannel.size())
        return shouldProcessChannel[chan];
    return false;
}

int PhaseCalculator::getProcessLength()
{
    return processLength;
}

int PhaseCalculator::getNumFuture()
{
    return numFuture;
}

int PhaseCalculator::getCalcInterval()
{
    return calcInterval;
}

int PhaseCalculator::getGlitchLimit()
{
    return glitchLimit;
}

float PhaseCalculator::getRatioFuture()
{
    return ((float)numFuture) / ((float)processLength);
}

double PhaseCalculator::getLowCut()
{
    return lowCut;
}

double PhaseCalculator::getHighCut()
{
    return highCut;
}

// thread routine
void PhaseCalculator::run()
{
    Array<double> data;
    data.resize(historyLength);

    double paramsTemp[AR_ORDER];

    ARTimer timer;
    int currInterval = calcInterval;
    timer.startTimer(currInterval);

    while (true)
    {
        if (threadShouldExit())
            return;

        for (int chan = 0; chan < chanState.size(); chan++)
        {
            if (chanState[chan] == NOT_FULL)
                continue;

            // critical section for sharedDataBuffer
            {
                const ScopedLock myScopedLock(*sdbLock[chan]);

                for (int i = 0; i < historyLength; i++)
                    data.set(i, (*sharedDataBuffer[chan])[i]);
            }
            // end critical section

            double* inputseries = data.getRawDataPointer();
            double* perRaw = per.getRawDataPointer();
            double* pefRaw = pef.getRawDataPointer();
            double* hRaw = h.getRawDataPointer();
            double* gRaw = g.getRawDataPointer();

            // reset per and pef
            memset(perRaw, 0, historyLength * sizeof(double));
            memset(pefRaw, 0, historyLength * sizeof(double));

            // calculate parameters
            ARMaxEntropy(inputseries, historyLength, AR_ORDER, paramsTemp, perRaw, pefRaw, hRaw, gRaw);

            // write params quasi-atomically
            juce::Array<double>* myParams = arParams[chan];
            for (int i = 0; i < AR_ORDER; i++)
                myParams->set(i, paramsTemp[i]);

            chanState.set(chan, FULL_AR);
        }

        // update interval
        if (calcInterval != currInterval)
        {
            currInterval = calcInterval;
            timer.stopTimer();
            timer.startTimer(currInterval);
        }

        while (!timer.check())
        {
            if (threadShouldExit())
                return;
            if (calcInterval != currInterval)
            {
                currInterval = calcInterval;
                timer.stopTimer();
                timer.startTimer(currInterval);
            }
            sleep(10);
        }
    }
}

void PhaseCalculator::saveCustomChannelParametersToXml(XmlElement* channelElement, int channelNumber, InfoObjectCommon::InfoObjectType channelType)
{
    if (channelType == InfoObjectCommon::DATA_CHANNEL)
    {
        XmlElement* channelParams = channelElement->createNewChildElement("PARAMETERS");
        channelParams->setAttribute("shouldProcess", shouldProcessChannel[channelNumber]);
    }
}

void PhaseCalculator::loadCustomChannelParametersFromXml(XmlElement* channelElement, InfoObjectCommon::InfoObjectType channelType)
{
    int channelNum = channelElement->getIntAttribute("number");

    forEachXmlChildElement(*channelElement, subnode)
    {
        if (subnode->hasTagName("PARAMETERS"))
        {
            shouldProcessChannel.set(channelNum, subnode->getBoolAttribute("shouldProcess", true));
        }
    }
}

// private:

/*
* initialize all fields except processLength and numFuture based on these parameters and the current number of inputs.
*/
void PhaseCalculator::initialize()
{
    historyLength = processLength - numFuture;
    int nInputs = getNumInputs();
	int prevNInputs = historyFifo.getNumChannels();
    historyFifo.setSize(nInputs, historyLength + 1);

    per.resize(historyLength);
    pef.resize(historyLength);
    h.resize(AR_ORDER);
    g.resize(AR_ORDER);

    for (int index = 0; index < max(prevNInputs, nInputs); index++)
    {
		if (index < nInputs)
		{
			/* The AbstractFifo can actually only store one fewer entry than its "size"
			* due to lack of a "full" flag, so they and the buffers containing the
			* data must have length n+1.
			*/
			fifoManager.set(index, new AbstractFifo(historyLength + 1));

			// primitives
			chanState.set(index, NOT_FULL);
			lastSample.set(index, 0);
			if (index >= shouldProcessChannel.size()) // keep existing settings
				shouldProcessChannel.set(index, true);

			// processing buffers
            sharedDataBuffer.set(index, new juce::Array<double>());
			dataToProcess.set(index, new FFTWArray<double>(processLength));
			fftData.set(index, new FFTWArray<complex<double>>(processLength));
			dataOut.set(index, new FFTWArray<complex<double>>(processLength));

			// FFT plans
			pForward.set(index, new FFTWPlan(processLength, dataToProcess[index], fftData[index], FFTW_MEASURE));
			pBackward.set(index, new FFTWPlan(processLength, fftData[index], dataOut[index], FFTW_BACKWARD, FFTW_MEASURE));

			// mutexes
			sdbLock.set(index, new CriticalSection());

			// AR parameters
			arParams.set(index, new juce::Array<double>());
			arParams[index]->resize(AR_ORDER);

            // Bandpass filters
            // filter design copied from FilterNode
            forwardFilters.set(index, new Dsp::SmoothedFilterDesign
                <Dsp::Butterworth::Design::BandPass    // design type
                <2>,                                   // order
                1,                                     // number of channels (must be const)
                Dsp::DirectFormII>                     // realization
                (1));                                  // transition samples

            backwardFilters.set(index, new Dsp::SmoothedFilterDesign
                <Dsp::Butterworth::Design::BandPass    // design type
                <2>,                                   // order
                1,                                     // number of channels (must be const)
                Dsp::DirectFormII>                     // realization
                (1));
		}
		else
		{
			// clean up extra objects
			fifoManager.remove(index);
			pForward.remove(index);
			pBackward.remove(index);
            sharedDataBuffer.remove(index);
			dataToProcess.remove(index);
			fftData.remove(index);
			dataOut.remove(index);
			sdbLock.remove(index);
            arParams.remove(index);
            forwardFilters.remove(index);
            backwardFilters.remove(index);
		}
    }
    setFilterParameters();
}

void PhaseCalculator::setNumFuture(int newNumFuture)
{
    numFuture = newNumFuture;
    historyLength = processLength - newNumFuture;
    int nInputs = getNumInputs();
    historyFifo.setSize(nInputs, historyLength + 1);

    per.resize(historyLength);
    pef.resize(historyLength);

    for (int index = 0; index < nInputs; index++)
        fifoManager[index]->setTotalSize(historyLength + 1);
}

// ARTimer
ARTimer::ARTimer() : Timer()
{
    hasRung = false;
}

ARTimer::~ARTimer() {}

void ARTimer::timerCallback()
{
    hasRung = true;
}

bool ARTimer::check()
{
    bool temp = hasRung;
    hasRung = false;
    return temp;
}
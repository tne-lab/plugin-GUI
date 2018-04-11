/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2017 Translational NeuroEngineering Laboratory, MGH

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
#include <unordered_set> // getNumSubProcessors

// initializer for static instance counter
unsigned int PhaseCalculator::numInstances = 0;

PhaseCalculator::PhaseCalculator()
    : GenericProcessor  ("Phase Calculator")
    , Thread            ("AR Modeler")
    , calcInterval      (START_AR_INTERVAL)
    , arOrder           (START_AR_ORDER)
    , processADC        (false)
    , lowCut            (START_LOW_CUT)
    , highCut           (START_HIGH_CUT)
    , haveSentWarning   (false)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
    numInstances++;
    setProcessLength(1 << START_PLEN_POW, START_NUM_FUTURE);
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

#ifdef MARK_BUFFERS
void PhaseCalculator::createEventChannels()
{
    const DataChannel* in = getDataChannel(0);
    if (in)
    {
        float sampleRate = in->getSampleRate();
        EventChannel* chan = new EventChannel(EventChannel::TTL, 8, 1, sampleRate, this);
        chan->setName("PhaseCalculator buffer markers");
        chan->setDescription("Channel 1 turns on for the first half of each buffer of data channel 1");
        chan->setIdentifier("phasecalc.buffer");

        // source metadata
        MetaDataDescriptor sourceChanDesc(MetaDataDescriptor::UINT16, 3, "Source channel",
            "Index at source, source processor ID and subprocessor index of the channel that triggers this event",
            "source.channel.identifier.full");
        MetaDataValue sourceChanVal(sourceChanDesc);
        const uint16 sourceInfo[3] = {in->getSourceIndex(), in->getSourceNodeID(), in->getSubProcessorIdx()};
        sourceChanVal.setValue(sourceInfo);
        chan->addMetaData(sourceChanDesc, sourceChanVal);

        eventChannelPtr = eventChannelArray.add(chan);
    }
}
#endif

void PhaseCalculator::setParameter(int parameterIndex, float newValue)
{
    int numInputs = getNumInputs();

    switch (parameterIndex) {
    case pNumFuture:
        // precondition: acquisition is stopped.
        setNumFuture(static_cast<int>(newValue));
        break;

    case pEnabledState:
        if (newValue == 0)
            shouldProcessChannel.set(currentChannel, false);
        else
            shouldProcessChannel.set(currentChannel, true);
        break;

    case pRecalcInterval:
        calcInterval = static_cast<int>(newValue);
        break;

    case pAROrder:
        arOrder = static_cast<int>(newValue);
        updateSettings(); // resize the necessary fields
        break;

    case pAdcEnabled:
        processADC = (newValue > 0);
        break;

    case pLowcut:
        // precondition: acquisition is stopped.
        lowCut = newValue;
        setFilterParameters();
        break;

    case pHighcut:
        // precondition: acquisition is stopped.
        highCut = newValue;
        setFilterParameters();
        break;
    }
}

void PhaseCalculator::process(AudioSampleBuffer& buffer)
{
    int nChannels = buffer.getNumChannels();

    for (int chan = 0; chan < nChannels; chan++)
    {
        // "+CH" button
        if (!shouldProcessChannel[chan])
            continue;
        
        // "+ADC/AUX" button
        DataChannel::DataChannelTypes type = getDataChannel(chan)->getChannelType();
        if (!processADC && (type == DataChannel::ADC_CHANNEL 
                            || type == DataChannel::AUX_CHANNEL))
            continue;

        int nSamples = getNumSamples(chan);
        if (nSamples == 0)
            continue;

#ifdef MARK_BUFFERS // for debugging (see description in header file)
        if (chan == 0)
        {
            int64 ts1 = getTimestamp(0);
            uint8 ttlData1 = 1;
            TTLEventPtr event1 = TTLEvent::createTTLEvent(eventChannelPtr, ts1,
                &ttlData1, sizeof(uint8), 0);
            addEvent(eventChannelPtr, event1, 0);

            int halfway = nSamples / 2;
            int64 ts2 = ts1 + halfway;
            uint8 ttlData2 = 0;
            TTLEventPtr event2 = TTLEvent::createTTLEvent(eventChannelPtr, ts2,
                &ttlData2, sizeof(uint8), 0);
            addEvent(eventChannelPtr, event2, halfway);
        }
#endif

        // Forward-filter the data.
        // Code from FilterNode.
        float* wpIn = buffer.getWritePointer(chan);
        forwardFilters[chan]->process(nSamples, &wpIn);

        // If there are more samples than we have room to process, process the most recent samples and output zero
        // for the rest (this is an error that should be noticed and fixed).
        int startIndex = std::max(nSamples - bufferLength, 0);
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

        int freeSpace = bufferFreeSpace[chan];

        // if buffer wasn't full, check whether it will become so.
        bool willBecomeFull = (chanState[chan] == NOT_FULL && freeSpace <= nSamplesToProcess);

        // shift old data and copy new data into sharedDataBuffer
        int nOldSamples = bufferLength - nSamplesToProcess;

        const double* rpBuffer = sharedDataBuffer.getReadPointer(chan, nSamplesToProcess);
        double* wpBuffer = sharedDataBuffer.getWritePointer(chan);

        // critical section for this channel's sharedDataBuffer
        // note that the floats are coerced to doubles here - this is important to avoid over/underflow when calculating the phase.
        {
            const ScopedLock myScopedLock(*sdbLock[chan]);

            // shift old data
            for (int i = 0; i < nOldSamples; i++)
                *wpBuffer++ = *rpBuffer++;

            // copy new data
            wpIn += startIndex;
            for (int i = 0; i < nSamplesToProcess; i++)
                *wpBuffer++ = *wpIn++;
        }

        if (willBecomeFull) {
            // now that dataToProcess for this channel has data, let the thread start calculating the AR model.
            chanState.set(chan, FULL_NO_AR);
            bufferFreeSpace.set(chan, 0);
        }
        else if (chanState[chan] == NOT_FULL)
        {
            bufferFreeSpace.set(chan, bufferFreeSpace[chan] - nSamplesToProcess);
        }

        // calc phase and write out (only if AR model has been calculated)
        if (chanState[chan] == FULL_AR) {

            // copy data to dataToProcess
            const double* rpSDB = sharedDataBuffer.getReadPointer(chan);
            dataToProcess[chan]->copyFrom(rpSDB, bufferLength);

            // use AR(20) model to predict upcoming data and append to dataToProcess
            double* wpProcess = dataToProcess[chan]->getWritePointer(bufferLength);
            
            // quasi-atomic access of AR parameters
            Array<double> currParams;
            for (int i = 0; i < arOrder; i++)
                currParams.set(i, (*arParams[chan])[i]);
            double* rpParam = currParams.getRawDataPointer();

            arPredict(wpProcess, numFuture, rpParam, arOrder);

            // backward-filter the data
            //dataToProcess[chan]->reverse();
            //double* wpProcessReverse = dataToProcess[chan]->getWritePointer();
            //backwardFilters[chan]->reset();
            //backwardFilters[chan]->process(processLength, &wpProcessReverse);
            //dataToProcess[chan]->reverse();

            // Hilbert-transform dataToProcess
            pForward[chan]->execute();      // reads from dataToProcess, writes to fftData
            hilbertManip(*(fftData[chan]));
            pBackward[chan]->execute();     // reads from fftData, writes to dataOut

            // calculate phase and write out to buffer
            const complex<double>* rpProcess = dataOut[chan]->getReadPointer(bufferLength - nSamplesToProcess);
            float* wpOut = buffer.getWritePointer(chan);

            for (int i = 0; i < nSamplesToProcess; i++)
            {
                // output in degrees
                // note that doubles are cast back to floats
                wpOut[i + startIndex] = static_cast<float>(std::arg(rpProcess[i]) * (180.0 / Dsp::doublePi));
            }

            // for debugging - uncomment below and comment above from "Hilbert transform" line to just output the filtered wave            
            //const double* rpDTP = dataToProcess[chan]->getReadPointer(bufferLength - nSamplesToProcess);
            //float* wpOut = buffer.getWritePointer(chan);
            //for (int i = 0; i < nSamplesToProcess; i++)
            //{
            //    wpOut[i + startIndex] = static_cast<float>(rpDTP[i]);
            //}

            // unwrapping / smoothing
            unwrapBuffer(wpOut, nSamples, chan);
            smoothBuffer(wpOut, nSamples, chan);        
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

// starts thread when acquisition begins
bool PhaseCalculator::enable()
{
    if (!isEnabled)
        return false;

    startThread(AR_PRIORITY);
    return true;
}

bool PhaseCalculator::disable()
{
    signalThreadShouldExit();

    // reset channel states
    for (int i = 0; i < chanState.size(); i++)
        chanState.set(i, NOT_FULL);

    // reset bufferFreeSpace
    for (int i = 0; i < bufferFreeSpace.size(); i++)
        bufferFreeSpace.set(i, bufferLength);

    // reset last sample containers
    for (int i = 0; i < lastSample.size(); i++)
        lastSample.set(i, 0);

    // reset buffer overflow warning
    haveSentWarning = false;

    return true;
}


float PhaseCalculator::getRatioFuture()
{
    return static_cast<float>(numFuture) / processLength;
}

// thread routine
void PhaseCalculator::run()
{
    Array<double> data;
    data.resize(bufferLength);

    Array<double> paramsTemp;
    paramsTemp.resize(arOrder);

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

                for (int i = 0; i < bufferLength; i++)
                    data.set(i, sharedDataBuffer.getSample(chan, i));
            }
            // end critical section

            double* inputseries = data.getRawDataPointer();
            double* paramsOut = paramsTemp.getRawDataPointer();
            double* perRaw = per.getRawDataPointer();
            double* pefRaw = pef.getRawDataPointer();
            double* hRaw = h.getRawDataPointer();
            double* gRaw = g.getRawDataPointer();

            // reset per and pef
            memset(perRaw, 0, bufferLength * sizeof(double));
            memset(pefRaw, 0, bufferLength * sizeof(double));

            // calculate parameters
            ARMaxEntropy(inputseries, bufferLength, arOrder, paramsOut, perRaw, pefRaw, hRaw, gRaw);

            // write params quasi-atomically
            juce::Array<double>* myParams = arParams[chan];
            for (int i = 0; i < arOrder; i++)
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

void PhaseCalculator::updateSettings()
{
    int nInputs = getNumInputs();
    int prevNInputs = sharedDataBuffer.getNumChannels();

    sharedDataBuffer.setSize(nInputs, bufferLength);

    // update AR order dependent parameters
    if (arOrder != h.size())
    {
        h.resize(arOrder);
        g.resize(arOrder);
        for (int i = 0; i < prevNInputs; i++)
        {
            arParams[i]->resize(arOrder);
        }
    }

    if (nInputs > prevNInputs)
    {
        // initialize fields at new indices
        for (int i = prevNInputs; i < nInputs; i++)
        {
            // primitives
            bufferFreeSpace.set(i, bufferLength);            
            shouldProcessChannel.set(i, true);
            chanState.set(i, NOT_FULL);
            lastSample.set(i, 0);

            // processing buffers
            dataToProcess.set(i, new FFTWArray<double>(processLength));
            fftData.set(i, new FFTWArray<complex<double>>(processLength));
            dataOut.set(i, new FFTWArray<complex<double>>(processLength));

            // FFT plans
            pForward.set(i, new FFTWPlan(processLength, dataToProcess[i], fftData[i], FFTW_MEASURE));
            pBackward.set(i, new FFTWPlan(processLength, fftData[i], dataOut[i], FFTW_BACKWARD, FFTW_MEASURE));

            // mutexes
            sdbLock.set(i, new CriticalSection());

            // AR parameters
            arParams.set(i, new juce::Array<double>());
            arParams[i]->resize(arOrder);

            // Bandpass filters
            // filter design copied from FilterNode
            forwardFilters.set(i, new Dsp::SmoothedFilterDesign
                <Dsp::Butterworth::Design::BandPass    // design type
                <2>,                                   // order
                1,                                     // number of channels (must be const)
                Dsp::DirectFormII>                     // realization
                (1));                                  // samples of transition when changing parameters (i.e. passband)

            backwardFilters.set(i, new Dsp::SmoothedFilterDesign
                <Dsp::Butterworth::Design::BandPass
                <2>,
                1,
                Dsp::DirectFormII>
                (1));
        }
    }
    else if (nInputs < prevNInputs)
    {
        // delete unneeded fields
        int diff = prevNInputs - nInputs;

        bufferFreeSpace.removeLast(diff);
        shouldProcessChannel.removeLast(diff);
        dataToProcess.removeLast(diff);
        fftData.removeLast(diff);
        dataOut.removeLast(diff);
        pForward.removeLast(diff);
        pBackward.removeLast(diff);
        sdbLock.removeLast(diff);
        arParams.removeLast(diff);
        forwardFilters.removeLast(diff);
        backwardFilters.removeLast(diff);
    }
    // call this no matter what, since the sample rate may have changed.
    setFilterParameters();
}

bool PhaseCalculator::isGeneratesTimestamps() const
{
    return true;
}

int PhaseCalculator::getNumSubProcessors() const
{
    int numChannels = getTotalDataChannels();
    unordered_set<uint32> procFullIds;

    for (int i = 0; i < numChannels; ++i)
    {
        const DataChannel* chan = getDataChannel(i);
        uint16 sourceNodeId = chan->getSourceNodeID();
        uint16 subProcessorId = chan->getSubProcessorIdx();
        uint32 procFullId = getProcessorFullId(sourceNodeId, subProcessorId);
        procFullIds.insert(procFullId);
    }

    return procFullIds.size();
}

// ------------ PRIVATE METHODS ---------------

void PhaseCalculator::setProcessLength(int newProcessLength, int newNumFuture)
{
    jassert(newNumFuture <= newProcessLength - arOrder);

    processLength = newProcessLength;
    if (newNumFuture != numFuture)
        setNumFuture(newNumFuture);

    // update fields that depend on processLength
    int nInputs = getNumInputs();
    for (int i = 0; i < nInputs; i++)
    {
        // processing buffers
        dataToProcess[i]->resize(processLength);
        fftData[i]->resize(processLength);
        dataOut[i]->resize(processLength);

        // FFT plans
        pForward.set(i, new FFTWPlan(processLength, dataToProcess[i], fftData[i], FFTW_MEASURE));
        pBackward.set(i, new FFTWPlan(processLength, fftData[i], dataOut[i], FFTW_BACKWARD, FFTW_MEASURE));
    }
}

void PhaseCalculator::setNumFuture(int newNumFuture)
{
    numFuture = newNumFuture;
    bufferLength = processLength - newNumFuture;
    int nInputs = getNumInputs();
    sharedDataBuffer.setSize(nInputs, bufferLength);

    per.resize(bufferLength);
    pef.resize(bufferLength);

    for (int i = 0; i < nInputs; i++)
        bufferFreeSpace.set(i, bufferLength);
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

void PhaseCalculator::unwrapBuffer(float* wp, int nSamples, int chan)
{
    for (int startInd = 0; startInd < nSamples - 1; startInd++)
    {
        float diff = wp[startInd] - (startInd == 0 ? lastSample[chan] : wp[startInd - 1]);
        if (abs(diff) > 180)
        {
            // search forward for a jump in the opposite direction
            int endInd;
            int maxInd;
            if (diff < 0)
            // for downward jumps, unwrap if there's a jump back up within GLITCH_LIMIT samples
            {
                endInd = -1;
                maxInd = min(startInd + GLITCH_LIMIT, nSamples - 1);
            }
            else
            // for upward jumps, default to unwrapping until the end of the buffer, but stop if there's a jump back down sooner.
            {
                endInd = nSamples;
                maxInd = nSamples - 1;
            }
            for (int currInd = startInd + 1; currInd <= maxInd; currInd++)
            {
                float diff2 = wp[currInd] - wp[currInd - 1];
                if (abs(diff2) > 180 && ((diff > 0) != (diff2 > 0)))
                {
                    endInd = currInd;
                    break;
                }
            }

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
    int actualMaxGL = min(GLITCH_LIMIT, nSamples - 1);
    float diff = wp[0] - lastSample[chan];
    if (diff < 0 && diff > -180)
    {
        // identify whether signal exceeds last sample of the previous buffer within glitchLimit samples.
        int endIndex = -1;
        for (int i = 1; i <= actualMaxGL; i++)
        {
            if (wp[i] > lastSample[chan])
            {
                endIndex = i;
                break;
            }
            // corner case where signal wraps before it exceeds lastSample
            else if (wp[i] - wp[i - 1] < -180 && (wp[i] + 360) > lastSample[chan])
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

void PhaseCalculator::arPredict(double* writeStart, int writeNum, const double* params, int order)
{
    reverse_iterator<double*> dataIter;
    int i;
    for (i = 0; i < writeNum; i++)
    {
        dataIter = reverse_iterator<double*>(writeStart + i); // the reverse iterator actually starts out pointing at element i-1
        writeStart[i] = -inner_product<const double*, reverse_iterator<const double*>, double>(params, params + order, dataIter, 0);
    }
}

void PhaseCalculator::hilbertManip(FFTWArray<complex<double>>& fftData)
{
    int n = fftData.getLength();

    // Normalize DC and Nyquist, normalize and double prositive freqs, and set negative freqs to 0.
    int lastPosFreq = static_cast<int>(round(ceil(n / 2.0) - 1));
    int firstNegFreq = static_cast<int>(round(floor(n / 2.0) + 1));
    complex<double>* wp = fftData.getWritePointer();

    for (int i = 0; i < n; i++) {
        if (i > 0 && i <= lastPosFreq)
            // normalize and double
            wp[i] *= (2.0 / n);
        else if (i < firstNegFreq)
            // normalize but don't double
            wp[i] /= n;
        else
            // set to 0
            wp[i] = 0;
    }
}

// ----------- ARTimer ---------------

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
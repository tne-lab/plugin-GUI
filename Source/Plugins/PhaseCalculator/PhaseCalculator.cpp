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

#include <cstring>       // memset (for Burg method)
#include <numeric>       // inner_product

#include "PhaseCalculator.h"
#include "PhaseCalculatorEditor.h"
#include "PhaseCalculatorCanvas.h"
#include "burg.h"        // Autoregressive modeling

// initializer for static instance counter
unsigned int PhaseCalculator::numInstances = 0;

PhaseCalculator::PhaseCalculator()
    : GenericProcessor  ("Phase Calculator")
    , Thread            ("AR Modeler")
    , calcInterval      (50)
    , arOrder           (20)
    , lowCut            (4.0)
    , highCut           (8.0)
    , haveSentWarning   (false)
    , outputMode        (PH)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
    numInstances++;
    setProcessLength(1 << 13, 1 << 12);
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
    case NUM_FUTURE:
        setNumFuture(static_cast<int>(newValue));
        break;

    case RECALC_INTERVAL:
        calcInterval = static_cast<int>(newValue);
        break;

    case AR_ORDER:
        arOrder = static_cast<int>(newValue);
        h.resize(arOrder);
        g.resize(arOrder);
        for (int i = 0; i < getNumInputs(); i++)
            arParams[i]->resize(arOrder);
        break;

    case LOWCUT:
        lowCut = newValue;
        setFilterParameters();
        break;

    case HIGHCUT:
        highCut = newValue;
        setFilterParameters();
        break;

    case OUTPUT_MODE:
        outputMode = static_cast<OutputMode>(static_cast<int>(newValue));
        CoreServices::updateSignalChain(editor);  // add or remove channels if necessary
        break;
    }
}

void PhaseCalculator::process(AudioSampleBuffer& buffer)
{
    // handle subprocessors, if any
    HashMap<int, uint16>::Iterator it(subProcessorMap);
    while (it.next())
    {
        uint32 fullSourceID = static_cast<uint32>(it.getKey());
        int subProcessor = it.getValue();
        uint32 sourceTimestamp = getSourceTimestamp(fullSourceID);
        uint64 sourceSamples = getNumSourceSamples(fullSourceID);
        setTimestampAndSamples(sourceTimestamp, sourceSamples, subProcessor);
    }

    // iterate over active input channels
    int nInputs = getNumInputs();
    Array<int> activeChannels = editor->getActiveChannels();
    int nActiveChannels = activeChannels.size();
    for (int activeChan = 0;
        activeChan < nActiveChannels && activeChannels[activeChan] < nInputs;
        ++activeChan)
    {
        int chan = activeChannels[activeChan];
        int nSamples = getNumSamples(chan);
        if (nSamples == 0)
            continue;

        // Filter the data.
        float* wpIn = buffer.getWritePointer(chan);
        filters[chan]->process(nSamples, &wpIn);

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

            // Hilbert-transform dataToProcess
            pForward[chan]->execute();      // reads from dataToProcess, writes to fftData
            hilbertManip(*(fftData[chan]));
            pBackward[chan]->execute();     // reads from fftData, writes to dataOut

            // calculate phase and write out to buffer
            const complex<double>* rpProcess = dataOut[chan]->getReadPointer(bufferLength - nSamplesToProcess);
            float* wpOut = buffer.getWritePointer(chan);
            float* wpOut2;
            if (outputMode == PH_AND_MAG)
                // second output channel
                wpOut2 = buffer.getWritePointer(nInputs + activeChan);

            for (int i = 0; i < nSamplesToProcess; i++)
            {
                switch (outputMode)
                {
                case MAG:
                    wpOut[i + startIndex] = static_cast<float>(std::abs(rpProcess[i]));
                    break;
                
                case PH_AND_MAG:
                    wpOut2[i + startIndex] = static_cast<float>(std::abs(rpProcess[i]));
                    // fall through
                case PH:
                    // output in degrees
                    wpOut[i + startIndex] = static_cast<float>(std::arg(rpProcess[i]) * (180.0 / Dsp::doublePi));
                    break;
                    
                case IM:
                    wpOut[i + startIndex] = static_cast<float>(std::imag(rpProcess[i]));
                    break;
                }
            }

            // unwrapping / smoothing
            if (outputMode == PH || outputMode == PH_AND_MAG)
            {
                unwrapBuffer(wpOut, nSamples, chan);
                smoothBuffer(wpOut, nSamples, chan);
            }
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

void PhaseCalculator::updateSettings()
{
    // react to changed # of inputs
    int numInputs = getNumInputs();
    int prevNumInputs = sharedDataBuffer.getNumChannels();
    int numInputsChange = numInputs - prevNumInputs;

    sharedDataBuffer.setSize(numInputs, bufferLength);

    if (numInputsChange > 0)
    {
        // resize simple arrays
        bufferFreeSpace.insertMultiple(-1, bufferLength, numInputsChange);
        chanState.insertMultiple(-1, NOT_FULL, numInputsChange);
        lastSample.insertMultiple(-1, 0, numInputsChange);

        // add new objects at new indices
        for (int i = prevNumInputs; i < numInputs; i++)
        {
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
            filters.set(i, new Dsp::SmoothedFilterDesign
                <Dsp::Butterworth::Design::BandPass    // design type
                <2>,                                   // order
                1,                                     // number of channels (must be const)
                Dsp::DirectFormII>                     // realization
                (1));                                  // samples of transition when changing parameters (i.e. passband)
        }
    }
    else if (numInputsChange < 0)
    {
        // delete unneeded entries
        bufferFreeSpace.removeLast(-numInputsChange);
        dataToProcess.removeLast(-numInputsChange);
        fftData.removeLast(-numInputsChange);
        dataOut.removeLast(-numInputsChange);
        pForward.removeLast(-numInputsChange);
        pBackward.removeLast(-numInputsChange);
        sdbLock.removeLast(-numInputsChange);
        arParams.removeLast(-numInputsChange);
        filters.removeLast(-numInputsChange);
    }
    // call this no matter what, since the sample rate may have changed.
    setFilterParameters();

    // create new data channels if necessary
    updateSubProcessorMap();
    updateExtraChannels();
}

void PhaseCalculator::addAngleToCanvas(double newAngle)
{
    PhaseCalculatorEditor* editor = static_cast<PhaseCalculatorEditor*>(editor);
    if (editor->canvas != nullptr)
    {
        PhaseCalculatorCanvas* canvas = static_cast<PhaseCalculatorCanvas*>(editor->canvas.get());
        canvas->addAngle(newAngle);
    }
}

bool PhaseCalculator::isGeneratesTimestamps() const
{
    return true;
}

int PhaseCalculator::getNumSubProcessors() const
{
    return subProcessorMap.size();
}

float PhaseCalculator::getSampleRate(int subProcessorIdx) const
{
    jassert(subProcessorIdx < getNumSubProcessors());
    int chan = getDataChannelIndex(0, getNodeId(), subProcessorIdx);
    return getDataChannel(chan)->getSampleRate();
}

float PhaseCalculator::getBitVolts(int subProcessorIdx) const
{
    jassert(subProcessorIdx < getNumSubProcessors());
    int chan = getDataChannelIndex(0, getNodeId(), subProcessorIdx);
    return getDataChannel(chan)->getBitVolts();
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

        if (filters.size() > chan)
            filters[chan]->setParams(params);
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

void PhaseCalculator::updateSubProcessorMap()
{
    subProcessorMap.clear();
    if (outputMode == PH_AND_MAG)
    {
        uint16 maxUsedIdx = 0;
        Array<int> unmappedFullIds;

        // iterate over active input channels
        int numInputs = getNumInputs();
        Array<int> activeChans = editor->getActiveChannels();
        int numActiveChans = activeChans.size();
        for (int i = 0; i < numActiveChans && activeChans[i] < numInputs; ++i)
        {
            int c = activeChans[i];

            const DataChannel* chan = getDataChannel(c);
            uint16 sourceNodeId = chan->getSourceNodeID();
            uint16 subProcessorIdx = chan->getSubProcessorIdx();
            int procFullId = static_cast<int>(getProcessorFullId(sourceNodeId, subProcessorIdx));
            if (!subProcessorMap.contains(procFullId))
            {
                // try to match index if possible
                if (!subProcessorMap.containsValue(subProcessorIdx))
                {
                    subProcessorMap.set(procFullId, subProcessorIdx);
                    maxUsedIdx = max(maxUsedIdx, subProcessorIdx);
                }
                else
                {
                    unmappedFullIds.add(procFullId);
                }
            }
        }
        // assign remaining unmapped ids
        int numUnmappedIds = unmappedFullIds.size();
        for (int i = 0; i < numUnmappedIds; ++i)
            subProcessorMap.set(unmappedFullIds[i], ++maxUsedIdx);
    }
}

void PhaseCalculator::updateExtraChannels()
{
    // reset dataChannelArray to # of inputs
    int numInputs = getNumInputs();
    int numChannels = dataChannelArray.size();
    jassert(numChannels >= numInputs);
    dataChannelArray.removeLast(numChannels - numInputs);

    if (outputMode == PH_AND_MAG)
    {
        // iterate over active input channels
        Array<int> activeChans = editor->getActiveChannels();
        int numActiveChans = activeChans.size();
        for (int i = 0; i < numActiveChans && activeChans[i] < numInputs; ++i)
        {
            int c = activeChans[i];

            // see GenericProcessor::createDataChannelsByType
            DataChannel* baseChan = dataChannelArray[c];
            uint16 sourceNodeId = baseChan->getSourceNodeID();
            uint16 subProcessorIdx = baseChan->getSubProcessorIdx();
            uint32 baseFullId = getProcessorFullId(sourceNodeId, subProcessorIdx);
                        
            DataChannel* newChan = new DataChannel(
                baseChan->getChannelType(),
                baseChan->getSampleRate(),
                this,
                subProcessorMap[static_cast<int>(baseFullId)]);
            newChan->setBitVolts(baseChan->getBitVolts());
            newChan->addToHistoricString(getName());
            dataChannelArray.add(newChan);
        }
    }
    settings.numOutputs = dataChannelArray.size();
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

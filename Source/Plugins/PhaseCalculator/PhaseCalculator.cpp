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

const float PhaseCalculator::PASSBAND_EPS = 0.01F;

PhaseCalculator::PhaseCalculator()
    : GenericProcessor      ("Phase Calculator")
    , Thread                ("AR Modeler")
    , calcInterval          (50)
    , lowCut                (4.0)
    , highCut               (8.0)
    , minNyquist            (FLT_MAX)
    , hilbertLength         (1 << 14)
    , predictionLength      (1 << 13)
    , haveSentWarning       (false)
    , outputMode            (PH)
    , visEventChannel       (-1)
    , visContinuousChannel  (-1)
    , visHilbertBuffer      (VIS_HILBERT_LENGTH)
    , visForwardPlan        (VIS_HILBERT_LENGTH, &visHilbertBuffer, FFTW_MEASURE)
    , visBackwardPlan       (VIS_HILBERT_LENGTH, &visHilbertBuffer, FFTW_BACKWARD, FFTW_MEASURE)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
    setAROrder(20);
}

PhaseCalculator::~PhaseCalculator() {}

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
    case HILBERT_LENGTH:
        setHilbertLength(static_cast<int>(newValue));
        break;

    case PAST_LENGTH:
        setPredLength(hilbertLength - static_cast<int>(newValue));
        break;

    case PRED_LENGTH:
        setPredLength(static_cast<int>(newValue));
        break;

    case RECALC_INTERVAL:
        calcInterval = static_cast<int>(newValue);
        break;

    case AR_ORDER:
        setAROrder(static_cast<int>(newValue));
        break;

    case LOWCUT:
        setLowCut(newValue);
        break;

    case HIGHCUT:
        setHighCut(newValue);
        break;

    case OUTPUT_MODE:
        outputMode = static_cast<OutputMode>(static_cast<int>(newValue));
        CoreServices::updateSignalChain(editor);  // add or remove channels if necessary
        break;

    case VIS_E_CHAN:
        jassert(newValue >= -1);
        visEventChannel = static_cast<int>(newValue);
        break;

    case VIS_C_CHAN:
        setVisContChan(static_cast<int>(newValue));
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

    // check for events to visualize
    bool hasCanvas = static_cast<PhaseCalculatorEditor*>(getEditor())->canvas != nullptr;
    if (hasCanvas && visEventChannel > -1)
    {
        checkForEvents();
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
        {
            continue;
        }

        // Filter the data.
        float* wpIn = buffer.getWritePointer(chan);
        filters[chan]->process(nSamples, &wpIn);

        // If there are more samples than we have room to process, process the most recent samples and output zero
        // for the rest (this is an error that should be noticed and fixed).
        int hilbertPastLength = hilbertLength - predictionLength;
        int historyStartIndex = jmax(nSamples - historyLength, 0);
        int outputStartIndex = jmax(nSamples - hilbertPastLength, 0);
        
        jassert(outputStartIndex >= historyStartIndex); // since historyLength >= hilbertPastLength
        
        int nSamplesToEnqueue = nSamples - historyStartIndex;
        int nSamplesToProcess = nSamples - outputStartIndex;

        if (outputStartIndex != 0)
        {
            // clear the extra samples and send a warning message
            buffer.clear(chan, 0, outputStartIndex);
            if (!haveSentWarning)
            {
                CoreServices::sendStatusMessage("WARNING: Phase Calculator buffer is shorter than the sample buffer!");
                haveSentWarning = true;
            }
        }

        // shift old data and copy new data into historyBuffer
        int nOldSamples = historyLength - nSamplesToEnqueue;

        const double* rpBuffer = historyBuffer.getReadPointer(chan, nSamplesToEnqueue);
        double* wpBuffer = historyBuffer.getWritePointer(chan);

        // critical section for this channel's historyBuffer
        // note that the floats are coerced to doubles here - this is important to avoid over/underflow when calculating the phase.
        {
            const ScopedLock myHistoryLock(*historyLock[chan]);

            // shift old data
            for (int i = 0; i < nOldSamples; ++i)
            {
                *wpBuffer++ = *rpBuffer++;
            }

            // copy new data
            wpIn += historyStartIndex;
            for (int i = 0; i < nSamplesToEnqueue; ++i)
            {
                *wpBuffer++ = *wpIn++;
            }
        }

        if (chanState[chan] == NOT_FULL)
        {
            int newBufferFreeSpace = jmax(bufferFreeSpace[chan] - nSamplesToEnqueue, 0);
            bufferFreeSpace.set(chan, newBufferFreeSpace);
            if (newBufferFreeSpace == 0)
            {
                // now that dataToProcess for this channel is full,
                // let the thread start calculating the AR model.
                chanState.set(chan, FULL_NO_AR);
            }
        }

        // calc phase and write out (only if AR model has been calculated)
        if (chanState[chan] == FULL_AR) {

            // copy data to dataToProcess
            rpBuffer = historyBuffer.getReadPointer(chan, historyLength - hilbertPastLength);
            hilbertBuffer[chan]->copyFrom(rpBuffer, hilbertPastLength);

            // use AR(20) model to predict upcoming data and append to dataToProcess
            rpBuffer = historyBuffer.getReadPointer(chan, historyLength);
            double* wpHilbert = hilbertBuffer[chan]->getRealPointer(hilbertPastLength);

            // read current AR parameters safely
            Array<double> currParams;
            {
                const ScopedLock currParamLock(*arParamLock[chan]);

                for (int i = 0; i < arOrder; ++i)
                {
                    currParams.set(i, (*arParams[chan])[i]);
                }
            }
            
            double* rpParam = currParams.getRawDataPointer();
            arPredict(rpBuffer, wpHilbert, predictionLength, rpParam, arOrder);

            // Hilbert-transform dataToProcess
            forwardPlan[chan]->execute();      // reads from dataToProcess, writes to fftData
            hilbertManip(hilbertBuffer[chan]);
            backwardPlan[chan]->execute();     // reads from fftData, writes to dataOut

            // calculate phase and write out to buffer
            auto rpHilbert = hilbertBuffer[chan]->getComplexPointer(hilbertPastLength - nSamplesToProcess);
            float* wpOut = buffer.getWritePointer(chan);
            float* wpOut2;
            if (outputMode == PH_AND_MAG)
            {
                // second output channel
                jassert(nInputs + activeChan < buffer.getNumChannels());
                wpOut2 = buffer.getWritePointer(nInputs + activeChan);
            }

            for (int i = 0; i < nSamplesToProcess; ++i)
            {
                switch (outputMode)
                {
                case MAG:
                    wpOut[i + outputStartIndex] = static_cast<float>(std::abs(rpHilbert[i]));
                    break;
                
                case PH_AND_MAG:
                    wpOut2[i + outputStartIndex] = static_cast<float>(std::abs(rpHilbert[i]));
                    // fall through
                case PH:
                    // output in degrees
                    wpOut[i + outputStartIndex] = static_cast<float>(std::arg(rpHilbert[i]) * (180.0 / Dsp::doublePi));
                    break;
                    
                case IM:
                    wpOut[i + outputStartIndex] = static_cast<float>(std::imag(rpHilbert[i]));
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
        else // fifo not full or AR model not ready
        {
            // just output zeros
            buffer.clear(chan, outputStartIndex, nSamplesToProcess);
        }

        // if this is the monitored channel for events, check whether we can add a new phase
        if (hasCanvas && chan == visContinuousChannel && chanState[chan] != NOT_FULL)
        {
            calcVisPhases(getTimestamp(chan) + getNumSamples(chan));
        }

        // keep track of last sample
        lastSample.set(chan, buffer.getSample(chan, nSamples - 1));
    }
}

// starts thread when acquisition begins
bool PhaseCalculator::enable()
{
    if (isEnabled)
    {
        startThread(AR_PRIORITY);

        // have to manually enable editor, I guess...
        PhaseCalculatorEditor* editor = static_cast<PhaseCalculatorEditor*>(getEditor());
        editor->enable();
    }

    return isEnabled;
}

bool PhaseCalculator::disable()
{
    PhaseCalculatorEditor* editor = static_cast<PhaseCalculatorEditor*>(getEditor());
    editor->disable();

    signalThreadShouldExit();

    haveSentWarning = false;

    // reset states of active inputs
    int numInputs = getNumInputs();
    Array<int> activeChannels = editor->getActiveChannels();
    for (int chan : activeChannels)
    {
        if (chan < numInputs)
        {
            resetInputChannel(chan);
        }
    }

    // clear timestamp and phase queues
    while (!visTsBuffer.empty())
    {
        visTsBuffer.pop();
    }

    ScopedLock phaseLock(visPhaseBufferLock);
    while (!visPhaseBuffer.empty())
    {
        visPhaseBuffer.pop();
    }

    return true;
}

// thread routine
void PhaseCalculator::run()
{
    Array<double> data;
    data.resize(historyLength);

    Array<double> paramsTemp;
    paramsTemp.resize(arOrder);

    ARTimer timer;
    int currInterval = calcInterval;
    timer.startTimer(currInterval);

    while (true)
    {
        if (threadShouldExit())
        {
            return;
        }

        for (int chan = 0; chan < chanState.size(); ++chan)
        {
            if (chanState[chan] == NOT_FULL)
            {
                continue;
            }

            // critical section for historyBuffer
            {
                const ScopedLock myHistoryLock(*historyLock[chan]);

                for (int i = 0; i < historyLength; ++i)
                {
                    data.set(i, historyBuffer.getSample(chan, i));
                }
            }
            // end critical section

            // calculate parameters
            arModeler.fitModel(data, paramsTemp);

            // write params safely
            {
                const ScopedLock myParamLock(*arParamLock[chan]);

                juce::Array<double>* myParams = arParams[chan];
                for (int i = 0; i < arOrder; ++i)
                {
                    myParams->set(i, paramsTemp[i]);
                }
            }

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
            {
                return;
            }

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
    int prevNumInputs = historyBuffer.getNumChannels();
    int numInputsChange = numInputs - prevNumInputs;

    historyBuffer.setSize(numInputs, historyLength);

    if (numInputsChange > 0)
    {
        // resize simple arrays
        bufferFreeSpace.insertMultiple(-1, historyLength, numInputsChange);
        chanState.insertMultiple(-1, NOT_FULL, numInputsChange);
        lastSample.insertMultiple(-1, 0, numInputsChange);

        // add new objects at new indices
        for (int i = prevNumInputs; i < numInputs; i++)
        {
            // processing buffers
            hilbertBuffer.set(i, new FFTWArray(hilbertLength));

            // FFT plans
            forwardPlan.set(i, new FFTWPlan(hilbertLength, hilbertBuffer[i], FFTW_MEASURE));
            backwardPlan.set(i, new FFTWPlan(hilbertLength, hilbertBuffer[i], FFTW_BACKWARD, FFTW_MEASURE));

            // mutexes
            historyLock.set(i, new CriticalSection());
            arParamLock.set(i, new CriticalSection());

            // AR parameters
            arParams.set(i, new juce::Array<double>());
            arParams[i]->resize(arOrder);

            // Bandpass filters
            filters.set(i, new BandpassFilter());
        }
    }
    else if (numInputsChange < 0)
    {
        // delete unneeded entries
        bufferFreeSpace.removeLast(-numInputsChange);
        hilbertBuffer.removeLast(-numInputsChange);
        forwardPlan.removeLast(-numInputsChange);
        backwardPlan.removeLast(-numInputsChange);
        historyLock.removeLast(-numInputsChange);
        arParamLock.removeLast(-numInputsChange);
        arParams.removeLast(-numInputsChange);
        filters.removeLast(-numInputsChange);
    }
    // call these no matter what, since the sample rates may have changed.
    updateMinNyquist();
    setFilterParameters();

    // create new data channels if necessary
    updateSubProcessorMap();
    updateExtraChannels();
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

std::queue<double>& PhaseCalculator::getVisPhaseBuffer(ScopedPointer<ScopedLock>& lock)
{
    lock = new ScopedLock(visPhaseBufferLock);
    return visPhaseBuffer;
}

void PhaseCalculator::saveCustomChannelParametersToXml(XmlElement* channelElement,
    int channelNumber, InfoObjectCommon::InfoObjectType channelType)
{
    if (channelType == InfoObjectCommon::DATA_CHANNEL && channelNumber == visContinuousChannel)
    {
        channelElement->setAttribute("visualize", 1);
    }
}

void PhaseCalculator::loadCustomChannelParametersFromXml(XmlElement* channelElement,
    InfoObjectCommon::InfoObjectType channelType)
{
    if (channelElement->hasAttribute("visualize"))
    {
        // The saved channel should be added to the dropdown at this point.
        setVisContChan(channelElement->getIntAttribute("number"));   
        static_cast<PhaseCalculatorEditor*>(getEditor())->refreshVisContinuousChan();
    }
}

// ------------ PRIVATE METHODS ---------------

void PhaseCalculator::handleEvent(const EventChannel* eventInfo,
    const MidiMessage& event, int samplePosition)
{
    if (visEventChannel < 0)
    {
        return;
    }

    if (Event::getEventType(event) == EventChannel::TTL)
    {
        TTLEventPtr ttl = TTLEvent::deserializeFromMessage(event, eventInfo);
        if (ttl->getChannel() == visEventChannel && ttl->getState())
        {
            // add timestamp to the queue for visualization
            juce::int64 ts = ttl->getTimestamp();
            jassert(visTsBuffer.empty() || visTsBuffer.back() <= ts);
            visTsBuffer.push(ts);
        }
    }
}

void PhaseCalculator::setHilbertLength(int newHilbertLength)
{
    if (newHilbertLength == hilbertLength) { return; }

    float predLengthRatio = static_cast<float>(predictionLength) / hilbertLength;
    hilbertLength = newHilbertLength;

    static_cast<PhaseCalculatorEditor*>(getEditor())->refreshHilbertLength();

    // update dependent variables    
    int newPredLength = static_cast<int>(roundf(predLengthRatio * hilbertLength));
    setPredLength(newPredLength);

    // update dependent per-channel objects
    int nInputs = getNumInputs();
    for (int i = 0; i < nInputs; i++)
    {
        // processing buffers
        hilbertBuffer[i]->resize(hilbertLength);

        // FFT plans
        forwardPlan.set(i, new FFTWPlan(hilbertLength, hilbertBuffer[i], FFTW_MEASURE));
        backwardPlan.set(i, new FFTWPlan(hilbertLength, hilbertBuffer[i], FFTW_BACKWARD, FFTW_MEASURE));
    }
}

void PhaseCalculator::setPredLength(int newPredLength)
{
    if (newPredLength == predictionLength) { return; }

    predictionLength = newPredLength;
    static_cast<PhaseCalculatorEditor*>(getEditor())->refreshPredLength();
}

void PhaseCalculator::setAROrder(int newOrder)
{
    if (newOrder == arOrder) { return; }
    
    arOrder = newOrder;
    updateHistoryLength();
    bool s = arModeler.setParams(arOrder, historyLength);
    jassert(s);

    // update dependent per-channel objects
    int nInputs = getNumInputs();
    for (int i = 0; i < nInputs; i++)
    {
        arParams[i]->resize(arOrder);
    }
}

void PhaseCalculator::setLowCut(float newLowCut)
{
    if (newLowCut == lowCut) { return; }

    lowCut = newLowCut;
    if (lowCut >= highCut)
    {
        // push highCut up
        highCut = lowCut + PASSBAND_EPS;
        static_cast<PhaseCalculatorEditor*>(getEditor())->refreshHighCut();
    }
    setFilterParameters();
}

void PhaseCalculator::setHighCut(float newHighCut)
{
    if (newHighCut == highCut) { return; }

    highCut = newHighCut;
    if (highCut <= lowCut)
    {
        // push lowCut down
        lowCut = highCut - PASSBAND_EPS;
        static_cast<PhaseCalculatorEditor*>(getEditor())->refreshLowCut();
    }
    setFilterParameters();
}

void PhaseCalculator::setVisContChan(int newChan)
{
    jassert(newChan < filters.size());

    if (newChan >= 0)
    {
        int tempVisEventChan = visEventChannel;
        visEventChannel = -1; // disable temporarily

        // clear timestamp queue
        while (!visTsBuffer.empty())
        {
            visTsBuffer.pop();
        }

        // update filter settings
        visReverseFilter.setParams(filters[newChan]->getParams());
        visEventChannel = tempVisEventChan;
    }
    visContinuousChannel = newChan;
}

void PhaseCalculator::updateHistoryLength()
{
    jassert(VIS_HILBERT_LENGTH >= (1 << MAX_HILB_LEN_POW));
    int newHistoryLength = juce::jmax(
        VIS_HILBERT_LENGTH,   // must have enough samples for current and delayed Hilbert transforms
        arOrder + 1);         // must be longer than arOrder to train the AR model

    if (newHistoryLength == historyLength) { return; }
    
    historyLength = newHistoryLength;

    // update things that depend on historyLength
    int numInputs = getNumInputs();
    historyBuffer.setSize(numInputs, historyLength);

    for (int i = 0; i < numInputs; ++i)
    {
        bufferFreeSpace.set(i, historyLength);
    }

    bool s = arModeler.setParams(arOrder, historyLength);
    jassert(s);
}

void PhaseCalculator::updateMinNyquist()
{
    float currMinNyquist = FLT_MAX;

    auto ed = static_cast<PhaseCalculatorEditor*>(getEditor());
    int nInputs = getNumInputs();
    Array<int> activeChannels = ed->getActiveChannels();
    for (int chan : activeChannels)
    {
        if (chan < nInputs) 
        {
            float sampleRate = getDataChannel(chan)->getSampleRate();
            currMinNyquist = jmin(currMinNyquist, sampleRate / 2);
        }
    }

    minNyquist = currMinNyquist;
    if (highCut > minNyquist)
    {
        // push down highCut to make it valid
        CoreServices::sendStatusMessage("Lowering Phase Calculator upper passband limit to the Nyquist frequency (" +
            String(minNyquist) + " Hz)");
        setHighCut(minNyquist);
        ed->refreshHighCut();
    }
}

void PhaseCalculator::setFilterParameters()
{
    int nInputs = getNumInputs();
    Array<int> activeChannels = getEditor()->getActiveChannels();
    for (int chan : activeChannels)
    {
        if (chan >= nInputs) { continue; }

        jassert(chan < filters.size());
        jassert(lowCut >= 0 && lowCut < highCut);

        Dsp::Params params;
        params[0] = getDataChannel(chan)->getSampleRate();  // sample rate
        params[1] = 2;                                      // order
        params[2] = (highCut + lowCut) / 2;                 // center frequency
        params[3] = highCut - lowCut;                       // bandwidth

        filters[chan]->setParams(params);
    }

    // copy filter parameters for corresponding channel to visReverseFilter
    if (visContinuousChannel >= 0 && visContinuousChannel < nInputs)
    {
        visReverseFilter.setParams(filters[visContinuousChannel]->getParams());
    }
}

void PhaseCalculator::resetInputChannel(int chan)
{
    jassert(chan >= 0 && chan < getNumInputs());
    bufferFreeSpace.set(chan, historyLength);
    chanState.set(chan, NOT_FULL);
    lastSample.set(chan, 0);
    filters[chan]->reset();
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
                maxInd = jmin(startInd + GLITCH_LIMIT, nSamples - 1);
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
            {
                wp[i] -= 360 * (diff / abs(diff));
            }

            if (endInd > -1)
            {
                // skip to the end of this unwrapped section
                startInd = endInd;
            }
        }
    }
}

void PhaseCalculator::smoothBuffer(float* wp, int nSamples, int chan)
{
    int actualGL = jmin(GLITCH_LIMIT, nSamples - 1);
    float diff = wp[0] - lastSample[chan];
    if (diff < 0 && diff > -180)
    {
        // identify whether signal exceeds last sample of the previous buffer within glitchLimit samples.
        int endIndex = -1;
        for (int i = 1; i <= actualGL; i++)
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
            {
                wp[i] = lastSample[chan] + (i + 1) * slope;
            }
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
                    maxUsedIdx = jmax(maxUsedIdx, subProcessorIdx);
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
        {
            subProcessorMap.set(unmappedFullIds[i], ++maxUsedIdx);
        }
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

void PhaseCalculator::calcVisPhases(juce::int64 sdbEndTs)
{
    juce::int64 minTs = sdbEndTs - VIS_TS_MAX_DELAY;
    juce::int64 maxTs = sdbEndTs - VIS_TS_MIN_DELAY;

    // discard any timestamps less than minTs
    while (!visTsBuffer.empty() && visTsBuffer.front() < minTs)
    {
        visTsBuffer.pop();
    }

    if (!visTsBuffer.empty() && visTsBuffer.front() <= maxTs)
    {
        // perform reverse filtering and Hilbert transform
        const double* rpBuffer = historyBuffer.getReadPointer(visContinuousChannel, historyLength - 1);
        for (int i = 0; i < VIS_HILBERT_LENGTH; ++i)
        {
            visHilbertBuffer.set(i, rpBuffer[-i]);
        }

        double* realPtr = visHilbertBuffer.getRealPointer();
        visReverseFilter.reset();
        visReverseFilter.process(VIS_HILBERT_LENGTH, &realPtr);

        // un-reverse values
        visHilbertBuffer.reverseReal(VIS_HILBERT_LENGTH);

        visForwardPlan.execute();
        hilbertManip(&visHilbertBuffer);
        visBackwardPlan.execute();

        juce::int64 ts;
        ScopedLock phaseBufferLock(visPhaseBufferLock);
        while (!visTsBuffer.empty() && (ts = visTsBuffer.front()) <= maxTs)
        {
            visTsBuffer.pop();
            juce::int64 delay = sdbEndTs - ts;
            std::complex<double> analyticPt = visHilbertBuffer.getAsComplex(VIS_HILBERT_LENGTH - delay);
            visPhaseBuffer.push(std::arg(analyticPt));
        }
    }
}

void PhaseCalculator::arPredict(const double* readEnd, double* writeStart, int writeNum, const double* params, int order)
{
    for (int s = 0; s < writeNum; ++s)
    {
        // s = index to write output
        writeStart[s] = 0;
        for (int ind = s - 1; ind > s - 1 - order; --ind)
        {
            // ind = index of previous output to read
            writeStart[s] -= params[s - 1 - ind] * (ind < 0 ? readEnd[ind] : writeStart[ind]);
        }
    }
}

void PhaseCalculator::hilbertManip(FFTWArray* fftData)
{
    int n = fftData->getLength();

    // Normalize DC and Nyquist, normalize and double prositive freqs, and set negative freqs to 0.
    int lastPosFreq = (n + 1) / 2 - 1;
    int firstNegFreq = n / 2 + 1;
    int numPosNegFreqDoubles = lastPosFreq * 2; // sizeof(complex<double>) = 2 * sizeof(double)
    bool hasNyquist = (n % 2 == 0);

    std::complex<double>* wp = fftData->getComplexPointer();

    // normalize but don't double DC value
    wp[0] /= n;

    // normalize and double positive frequencies
    FloatVectorOperations::multiply(reinterpret_cast<double*>(wp + 1), 2.0 / n, numPosNegFreqDoubles);
    
    if (hasNyquist)
    {
        // normalize but don't double Nyquist frequency
        wp[lastPosFreq + 1] /= n;
    }

    // set negative frequencies to 0
    FloatVectorOperations::clear(reinterpret_cast<double*>(wp + firstNegFreq), numPosNegFreqDoubles);
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

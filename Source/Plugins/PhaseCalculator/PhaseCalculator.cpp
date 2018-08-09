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

PhaseCalculator::PhaseCalculator()
    : GenericProcessor      ("Phase Calculator")
    , Thread                ("AR Modeler")
    , calcInterval          (50)
    , arOrder               (20)
    , historyLength         (VIS_HILBERT_LENGTH)
    , lowCut                (4.0)
    , highCut               (8.0)
    , htScaleFactor         (getScaleFactor(lowCut, highCut))
    , outputMode            (PH)
    , visEventChannel       (-1)
    , visContinuousChannel  (-1)
    , visHilbertBuffer      (VIS_HILBERT_LENGTH)
    , visForwardPlan        (VIS_HILBERT_LENGTH, &visHilbertBuffer, FFTW_MEASURE)
    , visBackwardPlan       (VIS_HILBERT_LENGTH, &visHilbertBuffer, FFTW_BACKWARD, FFTW_MEASURE)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
    updateHistoryLength();
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

void PhaseCalculator::createEventChannels()
{
    // add vis phase event channel
    const DataChannel* visChannel = getDataChannel(visContinuousChannel);
    float sampleRate = visChannel ? visChannel->getSampleRate() : CoreServices::getGlobalSampleRate();
    juce::uint16 subproc = visChannel ? visChannel->getSubProcessorIdx() : 0;

    EventChannel* chan = new EventChannel(EventChannel::DOUBLE_ARRAY, 1, 1, sampleRate, this, subproc);
    chan->setName(chan->getName() + ": PC visualized phase (deg.)");
    chan->setDescription("The accurate phase in degrees of each visualized event");
    chan->setIdentifier("phasecalc.visphase");

    // metadata storing source data channel
    if (visChannel)
    {
        MetaDataDescriptor sourceChanDesc(MetaDataDescriptor::UINT16, 3, "Source Channel",
            "Index at its source, Source processor ID and Sub Processor index of the channel that triggers this event",
            "source.channel.identifier.full");
        MetaDataValue sourceChanVal(sourceChanDesc);
        uint16 sourceInfo[3];
        sourceInfo[0] = visChannel->getSourceIndex();
        sourceInfo[1] = visChannel->getSourceNodeID();
        sourceInfo[2] = visChannel->getSubProcessorIdx();
        sourceChanVal.setValue(static_cast<const uint16*>(sourceInfo));
        chan->addMetaData(sourceChanDesc, sourceChanVal);
    }

    visPhaseChannel = eventChannelArray.add(chan);
}

void PhaseCalculator::setParameter(int parameterIndex, float newValue)
{
    int numInputs = getNumInputs();

    switch (parameterIndex) {
    case RECALC_INTERVAL:
        calcInterval = static_cast<int>(newValue);
        break;

    case AR_ORDER:
    {
        int oldOrder = arOrder;
        arOrder = static_cast<int>(newValue);
        if (arOrder == oldOrder) { return; }

        // if order is increasing, update inputLength of modelers first:
        if (arOrder > oldOrder) { updateHistoryLength(); }
        for (auto chan : getEditor()->getActiveChannels())
        {
            if (chan < getNumInputs())
            {
                bool success = arModelers[chan]->setOrder(arOrder);
                jassert(success);
            }
        }
        // if order is decreasing, update inputLength second:
        if (arOrder < oldOrder) { updateHistoryLength(); }

        // update size of params for each channel
        for (int i = 0; i < getNumInputs(); i++)
        {
            arParams[i]->resize(arOrder);
        }
        break;
    }

    case LOWCUT:
        lowCut = newValue;
        setFilterParameters();
        htScaleFactor = getScaleFactor(lowCut, highCut);
        break;

    case HIGHCUT:
        highCut = newValue;
        setFilterParameters();
        htScaleFactor = getScaleFactor(lowCut, highCut);
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
    {
        int newVisContChan = static_cast<int>(newValue);
        jassert(newVisContChan < filters.size());

        if (newVisContChan >= 0)
        {
            int tempVisEventChan = visEventChannel;
            visEventChannel = -1; // disable temporarily

            // clear timestamp queue
            while (!visTsBuffer.empty())
            {
                visTsBuffer.pop();
            }

            // update filter settings
            visReverseFilter.setParams(filters[newVisContChan]->getParams());
            visEventChannel = tempVisEventChan;
        }
        visContinuousChannel = newVisContChan;
        break;
    }
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
                // now that the historyBuffer for this channel is full,
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
            arPredict(wpHilbert, predictionLength, rpParam, arOrder);

            // TODO

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

    Array<int> activeChannels;
    auto e = getEditor();

    while (true)
    {
        if (threadShouldExit())
        {
            return;
        }

        activeChannels = e->getActiveChannels();
        for (int chan : activeChannels)
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
            arModelers[chan]->fitModel(data, paramsTemp);

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
        lastComputedSample.insertMultiple(-1, 0, numInputsChange);
        // (temporary, until validSampleRate call):
        sampleRateMultiple.insertMultiple(-1, 1, numInputsChange);
        dsOffset.insertMultiple(-1, 0, numInputsChange);

        // add new objects at new indices
        for (int i = prevNumInputs; i < numInputs; i++)
        {
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
        chanState.removeLast(-numInputsChange);
        lastSample.removeLast(-numInputsChange);
        lastComputedSample.removeLast(-numInputsChange);
        sampleRateMultiple.removeLast(-numInputsChange);
        dsOffset.removeLast(-numInputsChange);
        historyLock.removeLast(-numInputsChange);
        arParamLock.removeLast(-numInputsChange);
        arParams.removeLast(-numInputsChange);
        filters.removeLast(-numInputsChange);
    }

    // set filter parameters (sample rates may have changed)
    setFilterParameters();

    // check whether active channels can be processed
    Array<int> activeChannels = editor->getActiveChannels();
    for (int chan : activeChannels)
    {
        if (chan < numInputs)
        {
            validateSampleRate(chan);
        }
    }

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
        // Set the visualization channel through the canvas. Should be added to the dropdown at this point.
        int chan = channelElement->getIntAttribute("number");
        static_cast<PhaseCalculatorEditor*>(getEditor())->setVisContinuousChan(chan);
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

void PhaseCalculator::updateHistoryLength()
{
    // minimum - must have enough samples to do a Hilbert transform on past values for visualization
    int newHistoryLength = VIS_HILBERT_LENGTH;
    Array<int> activeChannels = getEditor()->getActiveChannels();
    int numInputs = getNumInputs();
    for (int chan : activeChannels)
    {
        if (chan < numInputs)
        {
            newHistoryLength = jmax(newHistoryLength,
                arOrder * sampleRateMultiple[chan] + 1, // minimum to train AR model
                HT_FS * sampleRateMultiple[chan]);      // use @ least 1 second to train model
        }
    }

    if (newHistoryLength != historyLength)
    {
        historyLength = newHistoryLength;

        // update fields that depend on historyLength
        historyBuffer.setSize(numInputs, historyLength);
        for (int chan : activeChannels)
        {
            if (chan < numInputs)
            {
                bool success = arModelers[chan]->setInputLength(historyLength);
                jassert(success);
            }
        }

        for (int i = 0; i < numInputs; ++i)
        {
            bufferFreeSpace.set(i, historyLength);
        }
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

bool PhaseCalculator::validateSampleRate(int chan)
{
    auto e = getEditor();
    bool p, r, a;
    e->getChannelSelectionState(chan, &p, &r, &a);
    if (!p) { return false; }

    // test whether sample rate is a multiple of HT_FS
    float fsMult = getDataChannel(chan)->getSampleRate() / HT_FS;
    float fsMultRound = std::round(fsMult);
    if (std::abs(fsMult - fsMultRound) < FLT_EPSILON)
    {
        // leave selected
        int fsMultInt = static_cast<int>(fsMultRound);
        sampleRateMultiple.set(chan, fsMultInt);
        dsOffset.set(chan, fsMultInt - 1);
        return true;
    }

    // deselect and send warning
    e->setChannelSelectionState(chan - 1, false, r, a);
    CoreServices::sendStatusMessage("Channel " + String(chan + 1) + " was deselected because " +
        " its sample rate is not a multiple of " + String(HT_FS));
    return false;
}

void PhaseCalculator::resetInputChannel(int chan)
{
    jassert(chan >= 0 && chan < getNumInputs());
    bufferFreeSpace.set(chan, historyLength);
    chanState.set(chan, NOT_FULL);
    lastSample.set(chan, 0);
    lastComputedSample.set(chan, 0);
    dsOffset.set(chan, sampleRateMultiple[chan] - 1);
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
            double phaseRad = std::arg(analyticPt);
            visPhaseBuffer.push(phaseRad);

            // add to event channel
            double eventData = phaseRad * 180.0 / Dsp::doublePi;
            juce::int64 eventTs = sdbEndTs - getNumSamples(visContinuousChannel);
            BinaryEventPtr event = BinaryEvent::createBinaryEvent(visPhaseChannel, eventTs, &eventData, sizeof(double));
            addEvent(visPhaseChannel, event, 0);
        }
    }
}

void PhaseCalculator::arPredict(double* writeStart, int writeNum, const double* params, int order)
{
    for (int s = 0; s < writeNum; ++s)
    {
        writeStart[s] = 0;
        for (int p = 0; p < order; ++p)
        {
            writeStart[s] -= params[p] * writeStart[s - 1 - p];
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

double PhaseCalculator::getScaleFactor(double lowCut, double highCut)
{
    jassert()
}

// Hilbert transformer coefficients (FIR filter)
// Obtained by matlab call "firpm(HT_ORDER, [4 HT_FS/2-4]/(HT_FS/2), [1 1], 'hilbert)"
// Should be modified if HT_ORDER or HT_FS are changed, or if targeting frequencies lower than 4 Hz.
const double PhaseCalculator::HT_COEF[HT_ORDER + 1] = {
    -0.287572507836144,
    2.76472250749945e-05,
    -0.0946113256432684,
    -0.000258874394997638,
    -0.129436276914844,
    -0.000160842742642405,
    -0.213150968600552,
    -0.00055322197399798,
    -0.636856982103511,
    0,
    0.636856982103511,
    0.00055322197399798,
    0.213150968600552,
    0.000160842742642405,
    0.129436276914844,
    0.000258874394997638,
    0.0946113256432684,
    -2.76472250749945e-05,
    0.287572507836144
};

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

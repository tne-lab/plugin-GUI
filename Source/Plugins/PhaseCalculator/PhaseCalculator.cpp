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

#include <cfloat> // DBL_MAX
#include <cmath>  // sqrt

#include "PhaseCalculator.h"
#include "PhaseCalculatorEditor.h"

const float PhaseCalculator::PASSBAND_EPS = 0.01F;

/*** ShiftRegister ***/
ShiftRegister::ShiftRegister(int size)
    : Array()
    , freeSpace(size)
{
    resize(size);
}

void ShiftRegister::reset()
{
    const ScopedLock dataLock(getLock());
    freeSpace = size();
}

void ShiftRegister::resetAndResize(int newSize)
{
    const ScopedLock dataLock(getLock());
    resize(newSize);
    freeSpace = newSize;
}

bool ShiftRegister::isFull() const
{
    return freeSpace == 0;
}

void ShiftRegister::enqueue(const float* source, int n)
{
    const ScopedLock dataLock(getLock());

    int cap = size();
    if (n > cap) // skip beginning
    {
        source += n - cap;
        n = cap;
    }

    int nRemaining = cap - n;
    int nShift = jmax(nRemaining, cap - freeSpace);
    
    // shift back existing data
    for (int i = 1; i <= nShift; ++i)
    {
        setUnchecked(nRemaining - i, getUnchecked(cap - i));
    }

    // copy new data
    for (int i = 0; i < n; ++i)
    {
        setUnchecked(nRemaining + i, source[i]);
    }

    freeSpace = cap - (n + nShift);
}


/**** channel info *****/
ActiveChannelInfo::ActiveChannelInfo(const ChannelInfo& cInfo)
    : chanInfo (cInfo)
{}

void ActiveChannelInfo::reset()
{
    history.reset();
    filter.reset();
    arModeler.reset();
    FloatVectorOperations::clear(htState.begin(), htState.size());
    dsOffset = chanInfo.dsFactor;
    lastComputedSample = 0;
    lastPhase = 0;
}


ChannelInfo::ChannelInfo(int index, PhaseCalculator& proc)
    : ind       (index)
    , acInfo    (nullptr)
    , sampleRate(0)
    , dsFactor  (0)
    , owner     (proc)
{
    update();
}

void ChannelInfo::update()
{
    const DataChannel* chanInfo = owner.getDataChannel(ind);
    if (chanInfo == nullptr)
    {
        jassertfalse;
        return;
    }

    sampleRate = chanInfo->getSampleRate();

    float fsMult = sampleRate / Hilbert::FS;
    float fsMultRound = std::round(fsMult);
    if (std::abs(fsMult - fsMultRound) < FLT_EPSILON)
    {
        // can be active - sample rate is multiple of Hilbert Fs
        dsFactor = int(fsMultRound);

        if (isActive())
        {
            owner.updateActiveChannelInfo(*acInfo);
        }
    }
    else
    {
        dsFactor = 0;
        deactivate(); // this channel can no longer be active.
    }
}

bool ChannelInfo::activate()
{
    if (!isActive() && dsFactor != 0)
    {
        acInfo = new ActiveChannelInfo(*this);
        owner.updateActiveChannelInfo(*acInfo);
    }

    return isActive();
}

void ChannelInfo::deactivate()
{
    acInfo = nullptr;
}

bool ChannelInfo::isActive() const
{
    return acInfo != nullptr;
}

ActiveChannelInfo* ChannelInfo::getActiveInfo() const
{
    return acInfo;
}

int ChannelInfo::getDsFactor() const
{
    return dsFactor;
}

/**** phase calculator ****/
PhaseCalculator::PhaseCalculator()
    : GenericProcessor      ("Phase Calculator")
    , Thread                ("AR Modeler")
    , calcInterval          (50)
    , arOrder               (20)
    , outputMode            (PH)
    , visEventChannel       (-1)
    , visContinuousChannel  (-1)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
    setBand(ALPHA_THETA, true);
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
    const DataChannel* visChannel = getDataChannel(visContinuousChannel);

    if (!visChannel)
    {
        visPhaseChannel = nullptr;
        return;
    }

    float sampleRate = visChannel->getSampleRate();

    EventChannel* chan = new EventChannel(EventChannel::DOUBLE_ARRAY, 1, 1, sampleRate, this);
    chan->setName(chan->getName() + ": PC visualized phase (deg.)");
    chan->setDescription("The accurate phase in degrees of each visualized event");
    chan->setIdentifier("phasecalc.visphase");

    // metadata storing source data channel
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

    visPhaseChannel = eventChannelArray.add(chan);
}

void PhaseCalculator::setParameter(int parameterIndex, float newValue)
{
    switch (parameterIndex) {
    case RECALC_INTERVAL:
        calcInterval = int(newValue);
        break;

    case AR_ORDER:
        arOrder = int(newValue);
        updateActiveChannels();
        break;

    case BAND:
        setBand(Band(int(newValue)));
        break;

    case LOWCUT:
        setLowCut(newValue);
        break;

    case HIGHCUT:
        setHighCut(newValue);
        break;

    case OUTPUT_MODE:
    {
        OutputMode oldMode = outputMode;
        outputMode = OutputMode(int(newValue));
        if (oldMode == PH_AND_MAG || outputMode == PH_AND_MAG)
        {
            CoreServices::updateSignalChain(editor);  // add or remove channels if necessary
        }
        break;
    }

    case VIS_E_CHAN:
        jassert(newValue >= -1);
        visEventChannel = int(newValue);
        break;

    case VIS_C_CHAN:
        setVisContChan(int(newValue));
        break;
    }
}

void PhaseCalculator::process(AudioSampleBuffer& buffer)
{
    // handle subprocessors, if any
    HashMap<int, uint16>::Iterator subProcIt(subProcessorMap);
    while (subProcIt.next())
    {
        uint32 fullSourceID = uint32(subProcIt.getKey());
        int subProcessor = subProcIt.getValue();
        uint64 sourceTimestamp = getSourceTimestamp(fullSourceID);
        uint32 sourceSamples = getNumSourceSamples(fullSourceID);
        setTimestampAndSamples(sourceTimestamp, sourceSamples, subProcessor);
    }

    // check for events to visualize
    bool hasCanvas = static_cast<PhaseCalculatorEditor*>(getEditor())->canvas != nullptr;
    if (hasCanvas && visEventChannel > -1)
    {
        checkForEvents();
    }

    // iterate over active input channels
    int activeChanInd = -1;
    for (auto chanInfo : channelInfo)
    {
        if (!chanInfo->isActive())
        {
            continue;
        }
        ++activeChanInd;

        ActiveChannelInfo& acInfo = *chanInfo->acInfo;

        int chan = chanInfo->ind;
        int nSamples = getNumSamples(chan);
        if (nSamples == 0) // nothing to do
        {
            continue;
        }

        // filter the data
        float* const wpIn = buffer.getWritePointer(chan);
        acInfo.filter.process(nSamples, &wpIn);

        // enqueue as much new data as can fit into history
        acInfo.history.enqueue(wpIn, nSamples);

        // calc phase and write out (only if AR model has been calculated)
        if (acInfo.history.isFull() && acInfo.arModeler.hasBeenFit()) 
        {
            // read current AR parameters safely (uses lock internally)
            localARParams = acInfo.arParams;

            // use AR model to fill predSamps (which is downsampled) based on past data.
            int htDelay = Hilbert::DELAY.at(band);
            int stride = acInfo.chanInfo.dsFactor;

            double* pPredSamps = predSamps.getRawDataPointer();
            const double* pLocalParam = localARParams.getRawDataPointer();
            {
                const ScopedLock historyLock(acInfo.history.getLock());
                const double* rpHistory = acInfo.history.end() - acInfo.dsOffset;
                arPredict(rpHistory, pPredSamps, pLocalParam, htDelay + 1, stride, arOrder);
            }

            // identify indices of current buffer to execute HT
            htInds.clearQuick();
            for (int i = stride - acInfo.dsOffset; i < nSamples; i += stride)
            {
                htInds.add(i);
            }

            int htOutputSamps = htInds.size() + 1;
            if (htOutput.size() < htOutputSamps)
            {
                htOutput.resize(htOutputSamps);
            }

            // execute tranformer on current buffer
            int kOut = -htDelay;
            for (int kIn = 0; kIn < htInds.size(); ++kIn, ++kOut)
            {
                double samp = htFilterSamp(wpIn[htInds[kIn]], band, acInfo.htState);
                if (kOut >= 0)
                {
                    double rc = wpIn[htInds[kOut]];
                    double ic = htScaleFactor * samp;
                    htOutput.set(kOut, std::complex<double>(rc, ic));
                }
            }

            // copy state to transform prediction without changing the end-of-buffer state
            htTempState = acInfo.htState;
            
            // execute transformer on prediction
            for (int i = 0; i <= htDelay; ++i, ++kOut)
            {
                double samp = htFilterSamp(predSamps[i], band, htTempState);
                if (kOut >= 0)
                {
                    double rc = i == htDelay ? predSamps[0] : wpIn[htInds[kOut]];
                    double ic = htScaleFactor * samp;
                    htOutput.set(kOut, std::complex<double>(rc, ic));
                }
            }

            // output with upsampling (interpolation)
            float* wpOut = buffer.getWritePointer(chan);
            float* wpOut2;
            if (outputMode == PH_AND_MAG)
            {
                // second output channel
                int outChan2 = getNumInputs() + activeChanInd;
                jassert(outChan2 < buffer.getNumChannels());
                wpOut2 = buffer.getWritePointer(outChan2);
            }

            kOut = 0;
            std::complex<double> prevCS = acInfo.lastComputedSample;
            std::complex<double> nextCS = htOutput[kOut];
            double prevPhase, nextPhase, phaseSpan, thisPhase;
            double prevMag, nextMag, magSpan, thisMag;
            bool needPhase = outputMode != MAG;
            bool needMag = outputMode != PH;

            if (needPhase)
            {
                prevPhase = std::arg(prevCS);
                nextPhase = std::arg(nextCS);
                phaseSpan = circDist(nextPhase, prevPhase, Dsp::doublePi);
            }
            if (needMag)
            {
                prevMag = std::abs(prevCS);
                nextMag = std::abs(nextCS);
                magSpan = nextMag - prevMag;
            }
            int subSample = acInfo.dsOffset % stride;

            for (int i = 0; i < nSamples; ++i, subSample = (subSample + 1) % stride)
            {
                if (subSample == 0)
                {
                    // update interpolation frame
                    prevCS = nextCS;
                    nextCS = htOutput[++kOut];
                    
                    if (needPhase)
                    {
                        prevPhase = nextPhase;
                        nextPhase = std::arg(nextCS);
                        phaseSpan = circDist(nextPhase, prevPhase, Dsp::doublePi);
                    }
                    if (needMag)
                    {
                        prevMag = nextMag;
                        nextMag = std::abs(nextCS);
                        magSpan = nextMag - prevMag;
                    }
                }

                if (needPhase)
                {
                    thisPhase = prevPhase + phaseSpan * subSample / stride;
                    thisPhase = circDist(thisPhase, 0, Dsp::doublePi);
                }
                if (needMag)
                {
                    thisMag = prevMag + magSpan * subSample / stride;
                }

                switch (outputMode)
                {
                case MAG:
                    wpOut[i] = static_cast<float>(thisMag);
                    break;

                case PH_AND_MAG:
                    wpOut2[i] = static_cast<float>(thisMag);
                    // fall through
                case PH:
                    // output in degrees
                    wpOut[i] = static_cast<float>(thisPhase * (180.0 / Dsp::doublePi));
                    break;

                case IM:
                    wpOut[i] = static_cast<float>(thisMag * std::sin(thisPhase));
                    break;
                }
            }
            acInfo.lastComputedSample = prevCS;
            acInfo.dsOffset = ((acInfo.dsOffset + nSamples - 1) % stride) + 1;

            // unwrapping / smoothing
            if (outputMode == PH || outputMode == PH_AND_MAG)
            {
                unwrapBuffer(wpOut, nSamples, acInfo.lastPhase);
                smoothBuffer(wpOut, nSamples, acInfo.lastPhase);
                acInfo.lastPhase = wpOut[nSamples - 1];
            }
        }
        else // fifo not full or AR model not ready
        {
            // just output zeros
            buffer.clear(chan, 0, nSamples);
        }

        // if this is the monitored channel for events, check whether we can add a new phase
        if (hasCanvas && acInfo.history.isFull())
        {
            ScopedLock visProcessingLock(visProcessingCS);
            if (chan == visContinuousChannel)
            {
                calcVisPhases(chan, getTimestamp(chan) + getNumSamples(chan));
            }
        }
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
    for (auto chanInfo : channelInfo)
    {
        if (chanInfo->isActive())
        {
            chanInfo->acInfo->reset();
        }
    }

    // clear timestamp and phase queues
    while (!visTsBuffer.empty())
    {
        visTsBuffer.pop();
    }

    ScopedLock phaseLock(visPhaseBufferCS);
    while (!visPhaseBuffer.empty())
    {
        visPhaseBuffer.pop();
    }

    return true;
}

// thread routine
void PhaseCalculator::run()
{
    // collect enabled active channels and find maximum history length
    Array<ActiveChannelInfo*> activeChans;
    int maxHistoryLength = 0;
    for (auto chanInfo : channelInfo)
    {
        if (chanInfo->isActive())
        {
            activeChans.add(chanInfo->acInfo);
            maxHistoryLength = jmax(maxHistoryLength, chanInfo->acInfo->history.size());
        }
    }

    Array<double> data;
    data.resize(maxHistoryLength);

    Array<double, CriticalSection> paramsTemp;
    paramsTemp.resize(arOrder);

    uint32 startTime, endTime;
    while (!threadShouldExit())
    {
        startTime = Time::getMillisecondCounter();

        for (auto acInfo : activeChans)
        {
            if (!acInfo->history.isFull())
            {
                continue;
            }

            data.clearQuick();
            data.addArray(acInfo->history);

            // calculate parameters
            acInfo->arModeler.fitModel(data, paramsTemp);

            // write params safely (locking internally)
            acInfo->arParams.swapWith(paramsTemp);
        }

        endTime = Time::getMillisecondCounter();
        int remainingInterval = calcInterval - (endTime - startTime);
        if (remainingInterval >= 10) // avoid WaitForSingleObject
        {
            sleep(remainingInterval);
        }
    }
}

void PhaseCalculator::updateSettings()
{
    // update arrays that store one entry per input
    int numInputs = getNumInputs();
    int prevNumInputs = channelInfo.size();

    for (int i = numInputs; i < prevNumInputs; ++i)
    {
        channelInfo.removeLast();
    }

    updateAllChannels();

    for (int i = prevNumInputs; i < numInputs; ++i)
    {
        channelInfo.add(new ChannelInfo(i, *this));
    }

    // create new data channels if necessary
    updateSubProcessorMap();
    updateExtraChannels();

    if (outputMode == PH_AND_MAG)
    {
        // keep previously selected input channels from becoming selected extra channels
        deselectAllExtraChannels();
    }
}


Array<int> PhaseCalculator::getActiveInputs()
{
    int numInputs = getNumInputs();
    auto ed = static_cast<PhaseCalculatorEditor*>(getEditor());
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

int PhaseCalculator::getFullSourceId(int chan)
{
    const DataChannel* chanInfo = getDataChannel(chan);
    if (!chanInfo)
    {
        jassertfalse;
        return 0;
    }
    uint16 sourceNodeId = chanInfo->getSourceNodeID();
    uint16 subProcessorIdx = chanInfo->getSubProcessorIdx();
    return int(getProcessorFullId(sourceNodeId, subProcessorIdx));
}

std::queue<double>& PhaseCalculator::getVisPhaseBuffer(ScopedPointer<ScopedLock>& lock)
{
    lock = new ScopedLock(visPhaseBufferCS);
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
    int chanNum = channelElement->getIntAttribute("number");

    if (chanNum < getNumInputs() && channelElement->hasAttribute("visualize"))
    {
        // The saved channel should be added to the dropdown at this point.
        setVisContChan(chanNum);
        static_cast<PhaseCalculatorEditor*>(getEditor())->refreshVisContinuousChan();
    }
}

void PhaseCalculator::updateActiveChannelInfo(ActiveChannelInfo& acInfo)
{
    // update length of history based on sample rate
    // the history buffer should have enough samples to calculate phases for the viusalizer
    // with the proper Hilbert transform length AND train an AR model of the requested order,
    // using at least 1 second of data
    int newHistorySize = acInfo.chanInfo.dsFactor * jmax(
        VIS_HILBERT_LENGTH_MS * Hilbert::FS / 1000,
        arOrder + 1,
        Hilbert::FS);

    acInfo.history.resetAndResize(newHistorySize);

    // set filter parameters
    Dsp::Params params;
    params[0] = acInfo.chanInfo.sampleRate; // sample rate
    params[1] = 2;                          // order
    params[2] = (highCut + lowCut) / 2;     // center frequency
    params[3] = highCut - lowCut;           // bandwidth

    acInfo.filter.setParams(params);

    acInfo.arModeler.setParams(arOrder, newHistorySize, acInfo.chanInfo.dsFactor);

    acInfo.arParams.resize(arOrder);

    acInfo.htState.resize(Hilbert::DELAY.at(band) * 2 + 1);

    acInfo.reset();
}

double PhaseCalculator::circDist(double x, double ref, double cutoff)
{
    const double TWO_PI = 2 * Dsp::doublePi;
    double xMod = std::fmod(x - ref, TWO_PI);
    double xPos = (xMod < 0 ? xMod + TWO_PI : xMod);
    return (xPos > cutoff ? xPos - TWO_PI : xPos);
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

void PhaseCalculator::setBand(Band newBand, bool force)
{
    if (!force && newBand == band) { return; }
    if (newBand < 0 || newBand >= NUM_BANDS)
    {
        jassertfalse;
        return;
    }

    band = newBand;

    // set low and high cut to the defaults for this band, making sure to notify the editor
    resetCutsToDefaults();

    // resize htState for each active channel, htTempState, and predSamps
    int delay = Hilbert::DELAY.at(band);
    htTempState.resize(delay * 2 + 1);
    predSamps.resize(delay + 1);

    updateActiveChannels();
}

void PhaseCalculator::resetCutsToDefaults()
{
    auto& defaultBand = Hilbert::DEFAULT_BAND.at(band);
    lowCut = defaultBand[0];
    highCut = defaultBand[1];

    auto editor = static_cast<PhaseCalculatorEditor*>(getEditor());
    if (editor)
    {
        editor->refreshLowCut();
        editor->refreshHighCut();
    }

    updateScaleFactor();
    updateActiveChannels();
}

void PhaseCalculator::setLowCut(float newLowCut)
{
    if (newLowCut == lowCut) { return; }
    
    auto editor = static_cast<PhaseCalculatorEditor*>(getEditor());
    const Array<float>& validBand = Hilbert::VALID_BAND.at(band);

    if (newLowCut < validBand[0] || newLowCut >= validBand[1])
    {
        // invalid; don't set parameter and reset editor
        editor->refreshLowCut();
        CoreServices::sendStatusMessage("Low cut outside valid band of selected filter.");
        return;
    }
        
    lowCut = newLowCut;
    if (lowCut >= highCut)
    {
        // push highCut up
        highCut = jmin(lowCut + PASSBAND_EPS, validBand[1]);
        editor->refreshHighCut();
    }

    updateScaleFactor();    
    updateActiveChannels();
}

void PhaseCalculator::setHighCut(float newHighCut)
{
    if (newHighCut == highCut) { return; }

    auto editor = static_cast<PhaseCalculatorEditor*>(getEditor());
    const Array<float>& validBand = Hilbert::VALID_BAND.at(band);

    if (newHighCut <= validBand[0] || newHighCut > validBand[1])
    {
        // invalid; don't set parameter and reset editor
        editor->refreshHighCut();
        CoreServices::sendStatusMessage("High cut outside valid band of selected filter.");
        return;
    }

    highCut = newHighCut;
    if (highCut <= lowCut)
    {
        // push lowCut down
        lowCut = jmax(highCut - PASSBAND_EPS, validBand[0]);
        editor->refreshLowCut();
    }

    updateScaleFactor();
    updateActiveChannels();
}

void PhaseCalculator::setVisContChan(int newChan)
{
    if (newChan >= 0)
    {
        jassert(newChan < channelInfo.size() && channelInfo[newChan]->isActive());

        // disable event receival temporarily so we can flush the buffer
        int tempVisEventChan = visEventChannel;
        visEventChannel = -1;

        // clear timestamp queue
        while (!visTsBuffer.empty())
        {
            visTsBuffer.pop();
        }

        {
            ScopedLock visProcessingLock(visProcessingCS);

            visContinuousChannel = newChan;
            visReverseFilter.setParams(channelInfo[newChan]->acInfo->filter.getParams());
            updateVisHilbertLength();
        }
        
        visEventChannel = tempVisEventChan;
    }
    else
    {
        // OK to do this without the lock, since the old visualized channel
        // can still be processed while the filter, FFT buffer and plans haven't been modified.
        visContinuousChannel = -1;
    }
    
    // If acquisition is stopped (and thus the new channel might be from a different subprocessor),
    // update signal chain. Sinks such as LFP Viewer should receive this information.
    if (!CoreServices::getAcquisitionStatus())
    {
        CoreServices::updateSignalChain(getEditor());
    }
}

void PhaseCalculator::updateVisHilbertLength()
{
    // can happen during acquisition if the visualized channel is changed.
    ScopedLock visProcessingLock(visProcessingCS);

    int chan = visContinuousChannel;
    if (chan >= 0 && chan < channelInfo.size())
    {
        int newVisHilbertLength = VIS_HILBERT_LENGTH_MS * (Hilbert::FS * channelInfo[chan]->getDsFactor()) / 1000;
        if (visHilbertLength == newVisHilbertLength) { return; }
        visHilbertLength = newVisHilbertLength;

        // update the Fourier transform buffer and plans
        if (visHilbertBuffer.getLength() < visHilbertLength) // longer is OK
        {
            visHilbertBuffer.resize(visHilbertLength);
        }

        if (visForwardPlan == nullptr || visForwardPlan->getLength() != visHilbertLength)
        {
            visForwardPlan = new FFTWPlan(visHilbertLength, &visHilbertBuffer, FFTW_MEASURE);
        }

        if (visBackwardPlan == nullptr || visBackwardPlan->getLength() != visHilbertLength)
        {
            visBackwardPlan = new FFTWPlan(visHilbertLength, &visHilbertBuffer, FFTW_BACKWARD, FFTW_MEASURE);
        }
    }
}


void PhaseCalculator::updateScaleFactor()
{
    htScaleFactor = getScaleFactor(band, lowCut, highCut);
}

void PhaseCalculator::unwrapBuffer(float* wp, int nSamples, float lastPhase)
{
    for (int startInd = 0; startInd < nSamples - 1; startInd++)
    {
        float diff = wp[startInd] - (startInd == 0 ? lastPhase : wp[startInd - 1]);
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

void PhaseCalculator::smoothBuffer(float* wp, int nSamples, float lastPhase)
{
    int actualGL = jmin(GLITCH_LIMIT, nSamples - 1);
    float diff = wp[0] - lastPhase;
    if (diff < 0 && diff > -180)
    {
        // identify whether signal exceeds last sample of the previous buffer within glitchLimit samples.
        int endIndex = -1;
        for (int i = 1; i <= actualGL; i++)
        {
            if (wp[i] > lastPhase)
            {
                endIndex = i;
                break;
            }
            // corner case where signal wraps before it exceeds lastSample
            else if (wp[i] - wp[i - 1] < -180 && (wp[i] + 360) > lastPhase)
            {
                wp[i] += 360;
                endIndex = i;
                break;
            }
        }

        if (endIndex != -1)
        {
            // interpolate points from buffer start to endIndex
            float slope = (wp[endIndex] - lastPhase) / (endIndex + 1);
            for (int i = 0; i < endIndex; i++)
            {
                wp[i] = lastPhase + (i + 1) * slope;
            }
        }
    }
}

void PhaseCalculator::updateSubProcessorMap()
{
    if (outputMode != PH_AND_MAG)
    {
        subProcessorMap.clear();
        return;
    }

    // fill map according to selected channels, and remove outdated entries.
    uint16 maxUsedIdx = 0;
    SortedSet<int> foundFullIds;
    Array<int> unmappedFullIds;

    Array<int> activeInputs = getActiveInputs();
    for (int chan : activeInputs)
    {
        const DataChannel* chanInfo = getDataChannel(chan);
        uint16 sourceNodeId = chanInfo->getSourceNodeID();
        uint16 subProcessorIdx = chanInfo->getSubProcessorIdx();
        int procFullId = int(getProcessorFullId(sourceNodeId, subProcessorIdx));
        foundFullIds.add(procFullId);

        if (subProcessorMap.contains(procFullId))
        {
            maxUsedIdx = jmax(maxUsedIdx, subProcessorMap[subProcessorIdx]);
        }
        else // add new entry for this source subprocessor
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
    for (int id : unmappedFullIds)
    {
        subProcessorMap.set(id, ++maxUsedIdx);
    }

    // remove outdated entries
    Array<int> outdatedFullIds;
    HashMap<int, juce::uint16>::Iterator it(subProcessorMap);
    while (it.next())
    {
        int key = it.getKey();
        if (!foundFullIds.contains(key))
        {
            outdatedFullIds.add(key);
        }
    }
    for (int id : outdatedFullIds)
    {
        subProcessorMap.remove(id);
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
        Array<int> activeInputs = getActiveInputs();
        for (int chan : activeInputs)
        {
            // see GenericProcessor::createDataChannelsByType
            DataChannel* baseChan = dataChannelArray[chan];
            int baseFullId = getFullSourceId(chan);
                        
            DataChannel* newChan = new DataChannel(
                baseChan->getChannelType(),
                baseChan->getSampleRate(),
                this,
                subProcessorMap[baseFullId]);

            // rename to match base channel (implies that it contains magnitude data)
            newChan->setName(baseChan->getName() + "MAG");
            newChan->setBitVolts(baseChan->getBitVolts());
            newChan->addToHistoricString(getName());
            dataChannelArray.add(newChan);
        }
    }
    settings.numOutputs = dataChannelArray.size();
}

void PhaseCalculator::deselectChannel(int chan, bool warn)
{
    jassert(chan >= 0 && chan < getTotalDataChannels());

    auto ed = getEditor();
    bool p, r, a;
    ed->getChannelSelectionState(chan, &p, &r, &a);
    ed->setChannelSelectionState(chan - 1, false, r, a);

    if (warn)
    {
        CoreServices::sendStatusMessage("Channel " + String(chan + 1) + " was deselected because" +
            " its sample rate is not a multiple of " + String(Hilbert::FS));
    }
}

void PhaseCalculator::deselectAllExtraChannels()
{
    jassert(outputMode == PH_AND_MAG);
    Array<int> activeChans = getEditor()->getActiveChannels();
    int nInputs = getNumInputs();
    int nExtraChans = 0;
    for (int chan : activeChans)
    {
        if (chan < nInputs)
        {
            nExtraChans++;
        }
        else if (chan < nInputs + nExtraChans)
        {
            deselectChannel(chan, false);
        }
    }
}

void PhaseCalculator::calcVisPhases(int chan, juce::int64 sdbEndTs)
{
    int multiplier = Hilbert::FS * channelInfo[chan]->dsFactor / 1000;
    int maxDelay = VIS_TS_MAX_DELAY_MS * multiplier;
    int minDelay = VIS_TS_MIN_DELAY_MS * multiplier;
    int hilbertLength = VIS_HILBERT_LENGTH_MS * multiplier;

    juce::int64 minTs = sdbEndTs - maxDelay;
    juce::int64 maxTs = sdbEndTs - minDelay;

    // discard any timestamps less than minTs
    while (!visTsBuffer.empty() && visTsBuffer.front() < minTs)
    {
        visTsBuffer.pop();
    }

    if (!visTsBuffer.empty() && visTsBuffer.front() <= maxTs)
    {
        // perform reverse filtering and Hilbert transform
        // don't need to use a lock here since it's the same thread as the one
        // that writes to it.
        const double* rpBuffer = channelInfo[chan]->acInfo->history.end() - 1;
        for (int i = 0; i < hilbertLength; ++i)
        {
            visHilbertBuffer.set(i, rpBuffer[-i]);
        }

        double* realPtr = visHilbertBuffer.getRealPointer();
        visReverseFilter.reset();
        visReverseFilter.process(hilbertLength, &realPtr);

        // un-reverse values
        visHilbertBuffer.reverseReal(hilbertLength);

        visForwardPlan->execute();
        hilbertManip(&visHilbertBuffer, hilbertLength);
        visBackwardPlan->execute();

        juce::int64 ts;
        ScopedLock phaseBufferLock(visPhaseBufferCS);
        while (!visTsBuffer.empty() && (ts = visTsBuffer.front()) <= maxTs)
        {
            visTsBuffer.pop();
            int delay = static_cast<int>(sdbEndTs - ts);
            std::complex<double> analyticPt = visHilbertBuffer.getAsComplex(hilbertLength - delay);
            double phaseRad = std::arg(analyticPt);
            visPhaseBuffer.push(phaseRad);

            // add to event channel
            if (!visPhaseChannel)
            {
                jassertfalse; // event channel should not be null here.
                continue;
            }
            double eventData = phaseRad * 180.0 / Dsp::doublePi;
            juce::int64 eventTs = sdbEndTs - getNumSamples(chan);
            BinaryEventPtr event = BinaryEvent::createBinaryEvent(visPhaseChannel, eventTs, &eventData, sizeof(double));
            addEvent(visPhaseChannel, event, 0);
        }
    }
}

void PhaseCalculator::updateAllChannels()
{
    for (auto chanInfo : channelInfo)
    {
        bool wasActive = chanInfo->isActive();
        chanInfo->update();

        if (wasActive && !chanInfo->isActive())
        {
            // deselect if this channel just got deactivated
            deselectChannel(chanInfo->ind, true);
        }
    }
}

void PhaseCalculator::updateActiveChannels()
{
    for (auto chanInfo : channelInfo)
    {
        if (chanInfo->isActive())
        {
            updateActiveChannelInfo(*chanInfo->acInfo);
        }
    }
}

bool PhaseCalculator::activateInputChannel(int chan)
{
    if (chan < 0 || chan >= channelInfo.size())
    {
        jassertfalse;
        return false;
    }
    
    jassert(!channelInfo[chan]->isActive()); // this shouldn't be called if it's already active.

    return channelInfo[chan]->activate();
}

void PhaseCalculator::deactivateInputChannel(int chan)
{
    if (chan < 0 || chan >= channelInfo.size())
    {
        jassertfalse;
        return;
    }

    jassert(channelInfo.getUnchecked(chan)->isActive());
    channelInfo.getUnchecked(chan)->deactivate();
}

void PhaseCalculator::arPredict(const double* lastSample, double* prediction,
    const double* params, int samps, int stride, int order)
{
    for (int s = 0; s < samps; ++s)
    {
        // s = index to write output
        prediction[s] = 0;
        for (int ind = s - 1; ind > s - 1 - order; --ind)
        {
            // ind = index of previous output to read
            prediction[s] -= params[s - 1 - ind] *
                (ind < 0 ? lastSample[(ind + 1) * stride] : prediction[ind]);
        }
    }
}

void PhaseCalculator::hilbertManip(FFTWArray* fftData, int n)
{
    jassert(fftData->getLength() >= n);

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

double PhaseCalculator::getScaleFactor(Band band, double lowCut, double highCut)
{
    double maxResponse = -DBL_MAX;
    double minResponse = DBL_MAX;

    Array<double> testFreqs({ lowCut, highCut });
    // also look at any magnitude response extrema that fall within the selected band
    for (double freq : Hilbert::EXTREMA.at(band))
    {
        if (freq > lowCut && freq < highCut)
        {
            testFreqs.add(freq);
        }
    }

    // at each frequency, calculate the filter response
    int nCoefs = Hilbert::DELAY.at(band);
    for (double freq : testFreqs)
    {
        double normFreq = freq * Dsp::doublePi / (Hilbert::FS / 2);
        std::complex<double> response = 0;

        auto* transf = Hilbert::TRANSFORMER.at(band);
        for (int kCoef = 0; kCoef < nCoefs; ++kCoef)
        {
            double coef = transf[kCoef];
            
            // near component
            response += coef * std::polar(1.0, -(kCoef * normFreq));

            // mirrored component
            // there is no term for -nCoefs because that coefficient is 0.
            response -= coef * std::polar(1.0, -((2 * nCoefs - kCoef) * normFreq));
        }

        double absResponse = std::abs(response);
        maxResponse = jmax(maxResponse, absResponse);
        minResponse = jmin(minResponse, absResponse);
    }

    // scale factor is reciprocal of geometric mean of max and min
    return 1 / std::sqrt(minResponse * maxResponse);
}

double PhaseCalculator::htFilterSamp(double input, Band band, Array<double>& state)
{
    double* state_p = state.getRawDataPointer();

    // initialize new state entry
    int nCoefs = Hilbert::DELAY.at(band);
    int order = nCoefs * 2;
    jassert(order == state.size() - 1);
    state_p[order] = 0;

    // incorporate new input
    auto& transf = Hilbert::TRANSFORMER.at(band);
    for (int kCoef = 0; kCoef < nCoefs; ++kCoef)
    {
        double val = input * transf[kCoef];
        state_p[kCoef] += val;          // near component
        state_p[order - kCoef] -= val;  // mirrored component
    }

    // output and shift state
    double sampOut = state_p[0];
    memmove(state_p, state_p + 1, order * sizeof(double));
    return sampOut;
}

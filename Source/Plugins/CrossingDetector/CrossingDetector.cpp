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

#include "CrossingDetector.h"
#include "CrossingDetectorEditor.h"

CrossingDetector::CrossingDetector()
    : GenericProcessor  ("Crossing Detector")
    , threshold         (0.0f)
    , useRandomThresh   (false)
    , minThresh         (-180)
    , maxThresh         (180)
    , thresholdVal      (0.0)
    , useChannel        (false)
    , constant          (0)
    , selectedChannel   (-1)
    , posOn             (true)
    , negOn             (false)
    , inputChan         (0)
    , eventChan         (0)
    , eventDuration     (5)
    , timeout           (1000)
    , pastStrict        (1.0f)
    , pastSpan          (0)
    , futureStrict      (1.0f)
    , futureSpan        (0)
    , useJumpLimit      (false)
    , jumpLimit         (5.0f)
    , sampToReenable    (pastSpan + futureSpan + 1)
    , pastCounter       (0)
    , futureCounter     (0)
    , inputHistory      (pastSpan + futureSpan + 2)
    , thresholdHistory  (pastSpan + futureSpan + 2)
    , turnoffEvent      (nullptr)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
}

CrossingDetector::~CrossingDetector() {}

AudioProcessorEditor* CrossingDetector::createEditor()
{
    editor = new CrossingDetectorEditor(this);
    return editor;
}

void CrossingDetector::createEventChannels()
{
    // add detection event channel
    const DataChannel* in = getDataChannel(inputChan);
    float sampleRate = in ? in->getSampleRate() : CoreServices::getGlobalSampleRate();
    EventChannel* chan = new EventChannel(EventChannel::TTL, 8, 1, sampleRate, this);
    chan->setName("Crossing detector output");
    chan->setDescription("Triggers whenever the input signal crosses a voltage threshold.");
    chan->setIdentifier("crossing.event");

    // metadata storing source data channel
    if (in)
    {
        MetaDataDescriptor sourceChanDesc(MetaDataDescriptor::UINT16, 3, "Source Channel",
            "Index at its source, Source processor ID and Sub Processor index of the channel that triggers this event", "source.channel.identifier.full");
        MetaDataValue sourceChanVal(sourceChanDesc);
        uint16 sourceInfo[3];
        sourceInfo[0] = in->getSourceIndex();
        sourceInfo[1] = in->getSourceNodeID();
        sourceInfo[2] = in->getSubProcessorIdx();
        sourceChanVal.setValue(static_cast<const uint16*>(sourceInfo));
        chan->addMetaData(sourceChanDesc, sourceChanVal);
    }

    // event-related metadata!
    eventMetaDataDescriptors.clearQuick();

    MetaDataDescriptor* crossingPointDesc = new MetaDataDescriptor(MetaDataDescriptor::INT64, 1, "Crossing Point",
        "Time when threshold was crossed", "crossing.point");
    chan->addEventMetaData(crossingPointDesc);
    eventMetaDataDescriptors.add(crossingPointDesc);

    MetaDataDescriptor* crossingLevelDesc = new MetaDataDescriptor(MetaDataDescriptor::FLOAT, 1, "Crossing level",
        "Voltage level at first sample after crossing", "crossing.level");
    chan->addEventMetaData(crossingLevelDesc);
    eventMetaDataDescriptors.add(crossingLevelDesc);

    MetaDataDescriptor* threshDesc = new MetaDataDescriptor(MetaDataDescriptor::FLOAT, 1, "Threshold",
        "Monitored voltage threshold", "crossing.threshold");
    chan->addEventMetaData(threshDesc);
    eventMetaDataDescriptors.add(threshDesc);

    MetaDataDescriptor* directionDesc = new MetaDataDescriptor(MetaDataDescriptor::UINT8, 1, "Direction",
        "Direction of crossing: 1 = rising, 0 = falling", "crossing.direction");
    chan->addEventMetaData(directionDesc);
    eventMetaDataDescriptors.add(directionDesc);

    eventChannelPtr = eventChannelArray.add(chan);
}

void CrossingDetector::process(AudioSampleBuffer& continuousBuffer)
{
    if (inputChan < 0 || inputChan >= continuousBuffer.getNumChannels())
    {
        jassertfalse;
        return;
    }

    int nSamples = getNumSamples(inputChan);
    const float* rp = continuousBuffer.getReadPointer(inputChan);
    juce::int64 startTs = getTimestamp(inputChan);
    juce::int64 endTs = startTs + nSamples; // 1 past end

    // turn off event from previous buffer if necessary
    if (turnoffEvent != nullptr && turnoffEvent->getTimestamp() < endTs)
    {
        int turnoffOffset = static_cast<int>(turnoffEvent->getTimestamp() - startTs);
        if (turnoffOffset < 0)
        {
            // shouldn't happen; should be added during a previous buffer
            jassertfalse;
            turnoffEvent = nullptr;
        }
        else
        {
            addEvent(eventChannelPtr, turnoffEvent, turnoffOffset);
            turnoffEvent = nullptr;
        }
    }

    // store threshold for each sample of current buffer
    Array<float> currThresholds;
    currThresholds.resize(nSamples);

    // define lambdas to access history values more easily
    auto inputAt = [=](int index)
    {
        return index < 0 ? inputHistory[index] : rp[index];
    };

    auto thresholdAt = [&, this](int index)
    {
        return index < 0 ? thresholdHistory[index] : currThresholds[index];
    };

    // loop over current buffer and add events for newly detected crossings
    for (int i = 0; i < nSamples; ++i)
    {
        // state to keep constant during each iteration
        if (useRandomThresh)
        {
            currThresholds.set(i, currRandomThresh);
        }
        else
        {
            currThresholds.set(i, threshold);
        }

        //threshold using active channels
        const float* rpThresh;
        if (useChannel)
        {
            rpThresh = continuousBuffer.getReadPointer(selectedChannel);
        }
        //look at active channels
        Array<int> activeChannels = editor->getActiveChannels();
        bool selectedChannelIsActive = false;
        for (int chan : activeChannels)
        {
            if (useChannel && chan == selectedChannel)
            {
                selectedChannelIsActive = true;
                continue; //processed at end
            }
            if (useChannel)
                currThresh = rpThresh[i]; 
            else
                currThresh = constant; 
        }
        if (selectedChannelIsActive)
        {
            //what to do if the selected channel = input channel
            jassertfalse; break; //?
        }

        bool currPosOn = posOn;
        bool currNegOn = negOn;

        // update pastCounter and futureCounter
        if (pastSpan >= 1)
        {
            int indLeaving = i - (pastSpan + futureSpan + 2);
            if (inputAt(indLeaving) > thresholdAt(indLeaving))
                pastCounter--;

            int indEntering = i - (futureSpan + 2);
            if (inputAt(indEntering) > thresholdAt(indEntering))
                pastCounter++;
        }

        if (futureSpan >= 1)
        {
            int indLeaving = i - futureSpan;
            if (inputAt(indLeaving) > thresholdAt(indLeaving))
                futureCounter--;

            int indEntering = i;
            if (inputAt(indEntering) > thresholdAt(indEntering))
                futureCounter++;
        }

        if (i < sampToReenable)
            // can't trigger an event now
            continue;

        int crossingOffset = i - futureSpan;

        float preVal = inputAt(crossingOffset - 1);
        float preThresh = thresholdAt(crossingOffset - 1);
        float postVal = inputAt(crossingOffset);
        float postThresh = thresholdAt(crossingOffset);

        // check whether to trigger an event
        if (currPosOn && shouldTrigger(true, preVal, postVal, preThresh, postThresh) ||
            currNegOn && shouldTrigger(false, preVal, postVal, preThresh, postThresh))
        {
            // add event
            triggerEvent(startTs, crossingOffset, nSamples, postThresh, postVal);
            
            // update sampToReenable
            sampToReenable = i + 1 + timeoutSamp;

            // if using random thresholds, set a new threshold
            if (useRandomThresh)
            {
                currRandomThresh = nextThresh();
                thresholdVal = currRandomThresh;
            }
        }
    }

    // update inputHistory and thresholdHistory
    inputHistory.enqueueArray(rp, nSamples);
    float* rpThresh = currThresholds.getRawDataPointer();
    thresholdHistory.enqueueArray(rpThresh, nSamples);

    // shift sampToReenable so it is relative to the next buffer
    sampToReenable = std::max(0, sampToReenable - nSamples);
}

// all new values should be validated before this function is called!
void CrossingDetector::setParameter(int parameterIndex, float newValue)
{
    switch (parameterIndex)
    {
    case pRandThresh:
        useRandomThresh = static_cast<bool>(newValue);
        // update threshold
        float newThresh;
        if (useRandomThresh)
        {
            newThresh = nextThresh();
            currRandomThresh = newThresh;
        }
        else
        {
            newThresh = threshold;
        }
        thresholdVal = newThresh;
        break;

    case pMinThresh:
        minThresh = newValue;
        currRandomThresh = nextThresh();
        if (useRandomThresh)
            thresholdVal = currRandomThresh;
        break;

    case pMaxThresh:
        maxThresh = newValue;
        currRandomThresh = nextThresh();
        if (useRandomThresh)
            thresholdVal = currRandomThresh;
        break;

    case pThreshold:
        threshold = newValue;
        break;

    case pUseChannel: 
        if (newValue) 
            validateActiveChannels();
        useChannel = static_cast<bool>(newValue);
        break;

    case pConstant:
        constant = newValue;
        break;

    case pSelectedChannel:
        selectedChannel = static_cast<int>(newValue);
        break;

    case pPosOn:
        posOn = static_cast<bool>(newValue);
        break;

    case pNegOn:
        negOn = static_cast<bool>(newValue);
        break;

    case pInputChan:
        if (getNumInputs() > newValue)
            inputChan = static_cast<int>(newValue);
        break;

    case pEventChan:
        eventChan = static_cast<int>(newValue);
        break;

    case pEventDur:
        eventDuration = static_cast<int>(newValue);
        if (CoreServices::getAcquisitionStatus())
        {
            float sampleRate = getDataChannel(inputChan)->getSampleRate();
            eventDurationSamp = static_cast<int>(ceil(eventDuration * sampleRate / 1000.0f));
        }
        break;

    case pTimeout:
        timeout = static_cast<int>(newValue);
        if (CoreServices::getAcquisitionStatus())
        {
            float sampleRate = getDataChannel(inputChan)->getSampleRate();
            timeoutSamp = static_cast<int>(floor(timeout * sampleRate / 1000.0f));
        }
        break;

    case pPastSpan:
        pastSpan = static_cast<int>(newValue);
        sampToReenable = pastSpan + futureSpan + 1;

        inputHistory.reset();
        inputHistory.resize(pastSpan + futureSpan + 2);
        thresholdHistory.resize(pastSpan + futureSpan + 2);

        // counters must reflect current contents of inputHistory
        pastCounter = 0;
        futureCounter = 0;
        break;

    case pPastStrict:
        pastStrict = newValue;
        break;

    case pFutureSpan:
        futureSpan = static_cast<int>(newValue);
        sampToReenable = pastSpan + futureSpan + 1;

        inputHistory.reset();
        inputHistory.resize(pastSpan + futureSpan + 2);
        thresholdHistory.resize(pastSpan + futureSpan + 2);

        // counters must reflect current contents of inputHistory
        pastCounter = 0;
        futureCounter = 0;
        break;

    case pFutureStrict:
        futureStrict = newValue;
        break;

    case pUseJumpLimit:
        useJumpLimit = static_cast<bool>(newValue);
        break;

    case pJumpLimit:
        jumpLimit = newValue;
        break;
    }
}

void CrossingDetector::validateActiveChannels()
{
    Array<int> activeChannels = editor->getActiveChannels();
    int numChannels = getNumInputs();
    bool p, r, a, haveSentMessage = false;
    const String message = "Deselecting channels that don't match subprocessor of selected reference";
    for (int chan : activeChannels)
    {
        if (chan >= numChannels) // can happen during update if # of channels decreases
            continue;

        if (chanToFullID(chan) != validSubProcFullID)
        {
            if (!haveSentMessage)
                CoreServices::sendStatusMessage(message);
            editor->getChannelSelectionState(chan, &p, &r, &a);
            editor->setChannelSelectionState(chan - 1, false, r, a);
        }
    }
}

juce::uint32 CrossingDetector::chanToFullID(int chanNum) const
{
    const DataChannel* chan = getDataChannel(chanNum);
    uint16 sourceNodeID = chan->getSourceNodeID();
    uint16 subProcessorIdx = chan->getSubProcessorIdx();
    return getProcessorFullId(sourceNodeID, subProcessorIdx);
}

bool CrossingDetector::enable()
{
    // input channel is fixed once acquisition starts, so convert timeout and eventDuration
    float sampleRate = getDataChannel(inputChan)->getSampleRate();
    eventDurationSamp = static_cast<int>(ceil(eventDuration * sampleRate / 1000.0f));
    timeoutSamp = static_cast<int>(floor(timeout * sampleRate / 1000.0f));
    return isEnabled;
}

bool CrossingDetector::disable()
{
    // set this to pastSpan so that we don't trigger on old data when we start again.
    sampToReenable = pastSpan + futureSpan + 1;
    // cancel any pending turning-off
    turnoffEvent = nullptr;
    return true;
}

bool CrossingDetector::shouldTrigger(bool direction, float preVal, float postVal,
    float preThresh, float postThresh)
{
    //check jumpLimit
    if (useJumpLimit && abs(postVal - preVal) >= jumpLimit)
        return false;

    //number of samples required before and after crossing threshold
    int pastSamplesNeeded = pastSpan ? static_cast<int>(ceil(pastSpan * pastStrict)) : 0;
    int futureSamplesNeeded = futureSpan ? static_cast<int>(ceil(futureSpan * futureStrict)) : 0;
    // if enough values cross threshold
    if(direction)
    {
        int pastZero = pastSpan - pastCounter;
        if(pastZero >= pastSamplesNeeded && futureCounter >= futureSamplesNeeded &&
            preVal <= preThresh && postVal > postThresh)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        int futureZero = futureSpan - futureCounter;
        if(pastCounter >= pastSamplesNeeded && futureZero >= futureSamplesNeeded &&
            preVal > preThresh && postVal <= postThresh)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
}

float CrossingDetector::nextThresh()
{
    float range = maxThresh - minThresh;
    return minThresh + range * rng.nextFloat();
}

void CrossingDetector::triggerEvent(juce::int64 bufferTs, int crossingOffset,
    int bufferLength, float threshold, float crossingLevel)
{
    // Construct metadata array
    // The order has to match the order the descriptors are stored in createEventChannels.
    MetaDataValueArray mdArray;

    int mdInd = 0;
    MetaDataValue* crossingPointVal = new MetaDataValue(*eventMetaDataDescriptors[mdInd++]);
    crossingPointVal->setValue(bufferTs + crossingOffset);
    mdArray.add(crossingPointVal);

    MetaDataValue* crossingLevelVal = new MetaDataValue(*eventMetaDataDescriptors[mdInd++]);
    crossingLevelVal->setValue(crossingLevel);
    mdArray.add(crossingLevelVal);

    MetaDataValue* threshVal = new MetaDataValue(*eventMetaDataDescriptors[mdInd++]);
    threshVal->setValue(threshold);
    mdArray.add(threshVal);

    MetaDataValue* directionVal = new MetaDataValue(*eventMetaDataDescriptors[mdInd++]);
    directionVal->setValue(static_cast<juce::uint8>(crossingLevel > threshold));
    mdArray.add(directionVal);

    // Create events
    int currEventChan = eventChan;
    juce::uint8 ttlDataOn = 1 << currEventChan;
    int sampleNumOn = std::max(crossingOffset, 0);
    juce::int64 eventTsOn = bufferTs + sampleNumOn;
    TTLEventPtr eventOn = TTLEvent::createTTLEvent(eventChannelPtr, eventTsOn,
        &ttlDataOn, sizeof(juce::uint8), mdArray, currEventChan);
    addEvent(eventChannelPtr, eventOn, sampleNumOn);

    juce::uint8 ttlDataOff = 0;
    int sampleNumOff = sampleNumOn + eventDurationSamp;
    juce::int64 eventTsOff = bufferTs + sampleNumOff;
    TTLEventPtr eventOff = TTLEvent::createTTLEvent(eventChannelPtr, eventTsOff,
        &ttlDataOff, sizeof(juce::uint8), mdArray, currEventChan);

    // Add or schedule turning-off event
    // We don't care whether there are other turning-offs scheduled to occur either in
    // this buffer or later. The abilities to change event duration during acquisition and for
    // events to be longer than the timeout period create a lot of possibilities and edge cases,
    // but overwriting turnoffEvent unconditionally guarantees that this and all previously
    // turned-on events will be turned off by this "turning-off" if they're not already off.
    if (sampleNumOff <= bufferLength)
        // add event now
        addEvent(eventChannelPtr, eventOff, sampleNumOff);
    else
        // save for later
        turnoffEvent = eventOff;
}
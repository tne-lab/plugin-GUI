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

#include "PhaseCalculatorEditor.h"
#include "PhaseCalculatorCanvas.h"
#include <climits> // INT_MAX

PhaseCalculatorEditor::PhaseCalculatorEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors)
    : VisualizerEditor  (parentNode, 190, useDefaultParameterEditors)
    , prevExtraChans    (0)
{
    tabText = "Event Phase Plot";
    int filterWidth = 85;

    PhaseCalculator* processor = static_cast<PhaseCalculator*>(parentNode);

    lowCutLabel = new Label("lowCutL", "Low cut");
    lowCutLabel->setBounds(10, 30, 80, 20);
    lowCutLabel->setFont(Font("Small Text", 12, Font::plain));
    lowCutLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(lowCutLabel);

    lowCutEditable = new Label("lowCutE");
    lowCutEditable->setEditable(true);
    lowCutEditable->addListener(this);
    lowCutEditable->setBounds(15, 47, 60, 18);
    lowCutEditable->setText(String(processor->lowCut), dontSendNotification);
    lowCutEditable->setColour(Label::backgroundColourId, Colours::grey);
    lowCutEditable->setColour(Label::textColourId, Colours::white);
    addAndMakeVisible(lowCutEditable);

    highCutLabel = new Label("highCutL", "High cut");
    highCutLabel->setBounds(10, 70, 80, 20);
    highCutLabel->setFont(Font("Small Text", 12, Font::plain));
    highCutLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(highCutLabel);

    highCutEditable = new Label("highCutE");
    highCutEditable->setEditable(true);
    highCutEditable->addListener(this);
    highCutEditable->setBounds(15, 87, 60, 18);
    highCutEditable->setText(String(processor->highCut), dontSendNotification);
    highCutEditable->setColour(Label::backgroundColourId, Colours::grey);
    highCutEditable->setColour(Label::textColourId, Colours::white);
    addAndMakeVisible(highCutEditable);

    recalcIntervalLabel = new Label("recalcL", "AR Refresh:");
    recalcIntervalLabel->setBounds(filterWidth, 25, 100, 20);
    recalcIntervalLabel->setFont(Font("Small Text", 12, Font::plain));
    recalcIntervalLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(recalcIntervalLabel);

    recalcIntervalEditable = new Label("recalcE");
    recalcIntervalEditable->setEditable(true);
    recalcIntervalEditable->addListener(this);
    recalcIntervalEditable->setBounds(filterWidth + 5, 44, 55, 18);
    recalcIntervalEditable->setColour(Label::backgroundColourId, Colours::grey);
    recalcIntervalEditable->setColour(Label::textColourId, Colours::white);
    recalcIntervalEditable->setText(String(processor->calcInterval), dontSendNotification);
    recalcIntervalEditable->setTooltip(RECALC_INTERVAL_TOOLTIP);
    addAndMakeVisible(recalcIntervalEditable);

    recalcIntervalUnit = new Label("recalcU", "ms");
    recalcIntervalUnit->setBounds(filterWidth + 60, 47, 25, 15);
    recalcIntervalUnit->setFont(Font("Small Text", 12, Font::plain));
    recalcIntervalUnit->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(recalcIntervalUnit);

    arOrderLabel = new Label("arOrderL", "Order:");
    arOrderLabel->setBounds(filterWidth, 65, 60, 20);
    arOrderLabel->setFont(Font("Small Text", 12, Font::plain));
    arOrderLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(arOrderLabel);

    arOrderEditable = new Label("arOrderE");
    arOrderEditable->setEditable(true);
    arOrderEditable->addListener(this);
    arOrderEditable->setBounds(filterWidth + 55, 66, 25, 18);
    arOrderEditable->setColour(Label::backgroundColourId, Colours::grey);
    arOrderEditable->setColour(Label::textColourId, Colours::white);
    arOrderEditable->setText(String(processor->arOrder), sendNotificationAsync);
    arOrderEditable->setTooltip(AR_ORDER_TOOLTIP);
    addAndMakeVisible(arOrderEditable);

    outputModeLabel = new Label("outputModeL", "Output:");
    outputModeLabel->setBounds(filterWidth, 87, 70, 20);
    outputModeLabel->setFont(Font("Small Text", 12, Font::plain));
    outputModeLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(outputModeLabel);

    outputModeBox = new ComboBox("outputModeB");
    outputModeBox->addItem("PHASE", PH);
    outputModeBox->addItem("MAG", MAG);
    outputModeBox->addItem("PH+MAG", PH_AND_MAG);
    outputModeBox->addItem("IMAG", IM);
    outputModeBox->setSelectedId(processor->outputMode);
    outputModeBox->setTooltip(OUTPUT_MODE_TOOLTIP);
    outputModeBox->setBounds(filterWidth + 5, 105, 76, 19);
    outputModeBox->addListener(this);
    addAndMakeVisible(outputModeBox);

    // new channels should be disabled by default
    channelSelector->paramButtonsToggledByDefault(false);
}

PhaseCalculatorEditor::~PhaseCalculatorEditor() {}

void PhaseCalculatorEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    PhaseCalculator* processor = static_cast<PhaseCalculator*>(getProcessor());

    if (comboBoxThatHasChanged == outputModeBox)
    {
        processor->setParameter(OUTPUT_MODE, static_cast<float>(outputModeBox->getSelectedId()));
    }
}

void PhaseCalculatorEditor::labelTextChanged(Label* labelThatHasChanged)
{
    PhaseCalculator* processor = static_cast<PhaseCalculator*>(getProcessor());

    if (labelThatHasChanged == recalcIntervalEditable)
    {
        int intInput;
        bool valid = updateIntControl(labelThatHasChanged, 0, INT_MAX, processor->calcInterval, &intInput);

        if (valid)
        {
            processor->setParameter(RECALC_INTERVAL, static_cast<float>(intInput));
        }
    }
    else if (labelThatHasChanged == arOrderEditable)
    {
        int intInput;
        bool valid = updateIntControl(labelThatHasChanged, 1, INT_MAX, processor->arOrder, &intInput);

        if (valid)
        {
            processor->setParameter(AR_ORDER, static_cast<float>(intInput));
        }
    }
    else if (labelThatHasChanged == lowCutEditable)
    {
        float floatInput;
        bool valid = updateFloatControl(labelThatHasChanged, PhaseCalculator::PASSBAND_EPS,
            processor->HT_FS / 2 - PhaseCalculator::PASSBAND_EPS, processor->lowCut, &floatInput);

        if (valid)
        {
            processor->setParameter(LOWCUT, floatInput);
        }
    }
    else if (labelThatHasChanged == highCutEditable)
    {
        float floatInput;
        bool valid = updateFloatControl(labelThatHasChanged, 2 * PhaseCalculator::PASSBAND_EPS,
            processor->HT_FS / 2, processor->highCut, &floatInput);

        if (valid)
        {
            processor->setParameter(HIGHCUT, floatInput);
        }
    }
}

void PhaseCalculatorEditor::buttonEvent(Button* button)
{
    // if it's a ChannelSelectorButton, assume it's a record button and update record status
    auto recordButton = dynamic_cast<ChannelSelectorButton*>(button);
    if (recordButton != nullptr)
    {
        int numInputs = static_cast<PhaseCalculator*>(getProcessor())->getNumInputs();
        int chanInd = button->getParentComponent()->getIndexOfChildComponent(button);
        int extraChanInd = chanInd - numInputs;
        if (extraChanInd < 0 || extraChanInd >= extraChanRecordStatus.size())
        {
            jassertfalse;
            return;
        }
        extraChanRecordStatus.set(extraChanInd, recordButton->getToggleState());
    }
}

void PhaseCalculatorEditor::channelChanged(int chan, bool newState)
{
    auto pc = static_cast<PhaseCalculator*>(getProcessor());
    if (chan < pc->getNumInputs())
    {
        Array<int> activeInputs = pc->getActiveInputs();
        int numActiveInputs = activeInputs.size();
        if (newState)
        {
            // check whether sample rate is compatible (and if not, disable channel)
            if (!pc->validateSampleRate(chan)) { return; }

            // ensure space allocated for per-active-channel arrays
            if (numActiveInputs > pc->numActiveChansAllocated)
            {
                pc->addActiveChannel();
            }
        }

        if (pc->outputMode == PH_AND_MAG)
        {
            if (newState)
            {
                int newInputIndex = activeInputs.indexOf(chan);
                jassert(newInputIndex <= extraChanRecordStatus.size());
                extraChanRecordStatus.insert(newInputIndex, false);
            }
            else
            {
                // find # of lower-index active inputs
                int i = 0;
                for (; i < numActiveInputs && activeInputs[i] < chan; ++i);
                jassert(i < extraChanRecordStatus.size());
                extraChanRecordStatus.remove(i);
            }

            // Update signal chain to add/remove output channels if necessary
            CoreServices::updateSignalChain(this);
        }
        else
        {
            updateVisualizer(); // update the available continuous channels for visualizer
        }
    }
}

void PhaseCalculatorEditor::startAcquisition()
{
    lowCutEditable->setEnabled(false);
    highCutEditable->setEnabled(false);
    arOrderEditable->setEnabled(false);
    outputModeBox->setEnabled(false);
    channelSelector->inactivateButtons();
}

void PhaseCalculatorEditor::stopAcquisition()
{
    lowCutEditable->setEnabled(true);
    highCutEditable->setEnabled(true);
    arOrderEditable->setEnabled(true);
    outputModeBox->setEnabled(true);
    channelSelector->activateButtons();
}

Visualizer* PhaseCalculatorEditor::createNewCanvas()
{
    canvas = new PhaseCalculatorCanvas(static_cast<PhaseCalculator*>(getProcessor()));
    return canvas;
}

void PhaseCalculatorEditor::updateSettings()
{
    auto pc = static_cast<PhaseCalculator*>(getProcessor());

    // only care about any of this stuff if we have extra channels
    // (and preserve when deselecting/reselecting PH_AND_MAG)
    if (pc->outputMode != PH_AND_MAG || channelSelector == nullptr) { return; }
    
    int numChans = pc->getNumOutputs();
    int numInputs = pc->getNumInputs();
    int extraChans = numChans - numInputs;

    int prevNumChans = channelSelector->getNumChannels();
    int prevNumInputs = prevNumChans - prevExtraChans;
    prevExtraChans = extraChans; // update for next time

    extraChanRecordStatus.resize(extraChans);
    channelSelector->setNumChannels(numChans);

    // super hacky, access record buttons to add or remove listeners
    Component* rbmComponent = channelSelector->getChildComponent(9);
    auto recordButtonManager = dynamic_cast<ButtonGroupManager*>(rbmComponent);
    if (recordButtonManager == nullptr)
    {
        jassertfalse;
        return;
    }

    // remove listeners on channels that are no longer "extra channels"
    // and set their record status to false since they're actually new channels
    for (int chan = prevNumInputs; chan < jmin(prevNumChans, numInputs); ++chan)
    {
        juce::Button* recordButton = recordButtonManager->getButtonAt(chan);
        recordButton->removeListener(this);
        // make sure listener really gets called
        recordButton->setToggleState(true, dontSendNotification);
        channelSelector->setRecordStatus(chan, false);
    }

    // add listeners for current "extra channels" and restore record statuses
    // (it's OK if addListener gets called more than once for a button)
    for (int eChan = 0; eChan < extraChans; ++eChan)
    {
        int chan = numInputs + eChan;
        juce::Button* recordButton = recordButtonManager->getButtonAt(chan);
        recordButton->removeListener(this);
        // make sure listener really gets called
        recordButton->setToggleState(!extraChanRecordStatus[eChan], dontSendNotification);
        channelSelector->setRecordStatus(chan, extraChanRecordStatus[eChan]);
        recordButton->addListener(this);
    }
}

void PhaseCalculatorEditor::saveCustomParameters(XmlElement* xml)
{
    VisualizerEditor::saveCustomParameters(xml);

    xml->setAttribute("Type", "PhaseCalculatorEditor");
    PhaseCalculator* processor = (PhaseCalculator*)(getProcessor());

    XmlElement* paramValues = xml->createNewChildElement("VALUES");
    paramValues->setAttribute("calcInterval", processor->calcInterval);
    paramValues->setAttribute("arOrder", processor->arOrder);
    paramValues->setAttribute("lowCut", processor->lowCut);
    paramValues->setAttribute("highCut", processor->highCut);
    paramValues->setAttribute("outputMode", processor->outputMode);
}

void PhaseCalculatorEditor::loadCustomParameters(XmlElement* xml)
{
    VisualizerEditor::loadCustomParameters(xml);

    forEachXmlChildElementWithTagName(*xml, xmlNode, "VALUES")
    {
        // some parameters have two fallbacks for backwards compatability
        recalcIntervalEditable->setText(xmlNode->getStringAttribute("calcInterval", recalcIntervalEditable->getText()), sendNotificationSync);
        arOrderEditable->setText(xmlNode->getStringAttribute("arOrder", arOrderEditable->getText()), sendNotificationSync);
        lowCutEditable->setText(xmlNode->getStringAttribute("lowCut", lowCutEditable->getText()), sendNotificationSync);
        highCutEditable->setText(xmlNode->getStringAttribute("highCut", highCutEditable->getText()), sendNotificationSync);
        outputModeBox->setSelectedId(xmlNode->getIntAttribute("outputMode", outputModeBox->getSelectedId()), sendNotificationSync);
    }
}

void PhaseCalculatorEditor::refreshLowCut()
{
    auto p = static_cast<PhaseCalculator*>(getProcessor());
    lowCutEditable->setText(String(p->lowCut), dontSendNotification);
}

void PhaseCalculatorEditor::refreshHighCut()
{
    auto p = static_cast<PhaseCalculator*>(getProcessor());
    highCutEditable->setText(String(p->highCut), dontSendNotification);
}

void PhaseCalculatorEditor::refreshVisContinuousChan()
{
    auto p = static_cast<PhaseCalculator*>(getProcessor());
    if (canvas != nullptr)
    {
        auto c = static_cast<PhaseCalculatorCanvas*>(canvas.get());
        c->displayContinuousChan(p->visContinuousChannel);
    }
}

// static utilities

/* Attempts to parse the current text of a label as an int between min and max inclusive.
*  If successful, sets "*out" and the label text to this value and and returns true.
*  Otherwise, sets the label text to defaultValue and returns false.
*/
template<typename Ctrl>
bool PhaseCalculatorEditor::updateIntControl(Ctrl* c, const int min, const int max,
    const int defaultValue, int* out)
{
    const String& in = c->getText();
    int parsedInt;
    try
    {
        parsedInt = std::stoi(in.toRawUTF8());
    }
    catch (const std::logic_error&)
    {
        c->setText(String(defaultValue), dontSendNotification);
        return false;
    }

    *out = jmax(min, jmin(max, parsedInt));

    c->setText(String(*out), dontSendNotification);
    return true;
}

// Like updateIntControl, but for floats
template<typename Ctrl>
bool PhaseCalculatorEditor::updateFloatControl(Ctrl* c, float min, float max,
    float defaultValue, float* out)
{
    const String& in = c->getText();
    float parsedFloat;
    try
    {
        parsedFloat = std::stof(in.toRawUTF8());
    }
    catch (const std::logic_error&)
    {
        c->setText(String(defaultValue), dontSendNotification);
        return false;
    }

    *out = jmax(min, jmin(max, parsedFloat));

    c->setText(String(*out), dontSendNotification);
    return true;
}

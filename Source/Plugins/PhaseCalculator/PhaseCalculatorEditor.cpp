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

PhaseCalculatorEditor::PhaseCalculatorEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors)
    : GenericEditor     (parentNode, useDefaultParameterEditors)
{
    int filterWidth = 80;
    desiredWidth = filterWidth + 260;

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

    processLengthLabel = new Label("processLength", "Buffer length:");
    processLengthLabel->setBounds(filterWidth + 8, 25, 180, 20);
    processLengthLabel->setFont(Font("Small Text", 12, Font::plain));
    processLengthLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(processLengthLabel);

    processLengthBox = new ComboBox("Buffer size");
    processLengthBox->setEditableText(true);
    for (int pow = MIN_PLEN_POW; pow <= MAX_PLEN_POW; pow++)
        processLengthBox->addItem(String(1 << pow), pow);
    processLengthBox->setText(String(processor->processLength), dontSendNotification);
    processLengthBox->setTooltip(QUEUE_SIZE_TOOLTIP);
    processLengthBox->setBounds(filterWidth + 10, 45, 80, 20);
    processLengthBox->addListener(this);
    addAndMakeVisible(processLengthBox);

    processLengthUnitLabel = new Label("processLengthUnit", "Samp.");
    processLengthUnitLabel->setBounds(filterWidth + 90, 45, 40, 20);
    processLengthUnitLabel->setFont(Font("Small Text", 12, Font::plain));
    processLengthUnitLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(processLengthUnitLabel);

    numPastLabel = new Label("numPastL", "Past:");
    numPastLabel->setBounds(filterWidth + 8, 85, 60, 15);
    numPastLabel->setFont(Font("Small Text", 12, Font::plain));
    numPastLabel->setColour(Label::backgroundColourId, Colour(230, 168, 0));
    numPastLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(numPastLabel);

    numFutureLabel = new Label("numFutureL", "Future:");
    numFutureLabel->setBounds(filterWidth + 70, 85, 60, 15);
    numFutureLabel->setFont(Font("Small Text", 12, Font::plain));
    numFutureLabel->setColour(Label::backgroundColourId, Colour(102, 140, 255));
    numFutureLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(numFutureLabel);

    numPastEditable = new Label("numPastE");
    numPastEditable->setEditable(true);
    numPastEditable->addListener(this);
    numPastEditable->setBounds(filterWidth + 8, 102, 60, 18);
    numPastEditable->setColour(Label::backgroundColourId, Colours::grey);
    numPastEditable->setColour(Label::textColourId, Colours::white);

    numFutureEditable = new Label("numFutureE");
    numFutureEditable->setEditable(true);
    numFutureEditable->addListener(this);
    numFutureEditable->setBounds(filterWidth + 70, 102, 60, 18);
    numFutureEditable->setColour(Label::backgroundColourId, Colours::grey);
    numFutureEditable->setColour(Label::textColourId, Colours::white);

    numFutureSlider = new ProcessBufferSlider("numFuture");
    numFutureSlider->setBounds(filterWidth + 8, 70, 122, 10);
    numFutureSlider->setColour(Slider::thumbColourId, Colour(255, 187, 0));
    numFutureSlider->setColour(Slider::backgroundColourId, Colour(51, 102, 255));
    numFutureSlider->setTooltip(NUM_FUTURE_TOOLTIP);
    numFutureSlider->addListener(this);
    numFutureSlider->updateFromProcessor(processor);
    addAndMakeVisible(numFutureSlider);
    addAndMakeVisible(numPastEditable);
    addAndMakeVisible(numFutureEditable);

    recalcIntervalLabel = new Label("recalcL", "AR Refresh:");
    recalcIntervalLabel->setBounds(filterWidth + 140, 25, 100, 20);
    recalcIntervalLabel->setFont(Font("Small Text", 12, Font::plain));
    recalcIntervalLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(recalcIntervalLabel);

    recalcIntervalEditable = new Label("recalcE");
    recalcIntervalEditable->setEditable(true);
    recalcIntervalEditable->addListener(this);
    recalcIntervalEditable->setBounds(filterWidth + 145, 45, 55, 18);
    recalcIntervalEditable->setColour(Label::backgroundColourId, Colours::grey);
    recalcIntervalEditable->setColour(Label::textColourId, Colours::white);
    recalcIntervalEditable->setText(String(processor->calcInterval), dontSendNotification);
    recalcIntervalEditable->setTooltip(RECALC_INTERVAL_TOOLTIP);
    addAndMakeVisible(recalcIntervalEditable);

    recalcIntervalUnit = new Label("recalcU", "ms");
    recalcIntervalUnit->setBounds(filterWidth + 200, 48, 25, 15);
    recalcIntervalUnit->setFont(Font("Small Text", 12, Font::plain));
    recalcIntervalUnit->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(recalcIntervalUnit);

    arOrderLabel = new Label("arOrderL", "AR Order:");
    arOrderLabel->setBounds(filterWidth + 140, 65, 115, 20);
    arOrderLabel->setFont(Font("Small Text", 12, Font::plain));
    arOrderLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(arOrderLabel);

    arOrderEditable = new Label("arOrderE");
    arOrderEditable->setEditable(true);
    arOrderEditable->addListener(this);
    arOrderEditable->setBounds(filterWidth + 145, 85, 55, 18);
    arOrderEditable->setColour(Label::backgroundColourId, Colours::grey);
    arOrderEditable->setColour(Label::textColourId, Colours::white);
    arOrderEditable->setText(String(processor->arOrder), dontSendNotification);
    arOrderEditable->setTooltip(AR_ORDER_TOOLTIP);
    addAndMakeVisible(arOrderEditable);

    applyToChan = new UtilityButton("+CH", Font("Default", 10, Font::plain));
    applyToChan->addListener(this);
    applyToChan->setBounds(filterWidth + 144, 108, 30, 18);
    applyToChan->setClickingTogglesState(true);
    applyToChan->setToggleState(true, dontSendNotification);
    applyToChan->setTooltip(APPLY_TO_CHAN_TOOLTIP);
    addAndMakeVisible(applyToChan);

    applyToADC = new UtilityButton("+ADC/AUX", Font("Default", 10, Font::plain));
    applyToADC->addListener(this);
    applyToADC->setBounds(filterWidth + 180, 108, 60, 18);
    applyToADC->setClickingTogglesState(true);
    applyToADC->setToggleState(false, dontSendNotification);
    applyToADC->setTooltip(APPLY_TO_ADC_TOOLTIP);
    addAndMakeVisible(applyToADC);
}

PhaseCalculatorEditor::~PhaseCalculatorEditor() {}

void PhaseCalculatorEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == processLengthBox)
    {
        PhaseCalculator* processor = static_cast<PhaseCalculator*>(getProcessor());
        int newId = processLengthBox->getSelectedId();
        int newProcessLength;
        if (newId) // one of the items in the list is selected
        {
            
            newProcessLength = (1 << newId);
        }
        else
        {
            // try to parse input
            String input = processLengthBox->getText();
            bool stringValid = parseInput(input, 1 << MIN_PLEN_POW, 1 << MAX_PLEN_POW, &newProcessLength);

            if (!stringValid)
            {
                processLengthBox->setText(String(processor->processLength), dontSendNotification);
                return;
            }
           
            processLengthBox->setText(String(newProcessLength), dontSendNotification);
        }

        // update AR order if necessary
        if (newProcessLength < processor->arOrder)
        {
            arOrderEditable->setText(String(newProcessLength), sendNotificationSync);
            CoreServices::sendStatusMessage("AR order snapped to maximum value for this processing buffer length");
        }

        // calculate numFuture
        float currRatio = processor->getRatioFuture();
        float newNumFuture;
        newNumFuture = fminf(roundf(currRatio * newProcessLength), newProcessLength - processor->arOrder);

        // change both at once
        processor->setProcessLength(newProcessLength, newNumFuture);

        // update slider
        numFutureSlider->updateFromProcessor(processor);
    }
}

void PhaseCalculatorEditor::labelTextChanged(Label* labelThatHasChanged)
{
    PhaseCalculator* processor = static_cast<PhaseCalculator*>(getProcessor());

    int sliderMin = static_cast<int>(numFutureSlider->getRealMinValue());
    int sliderMax = static_cast<int>(numFutureSlider->getMaximum());

    if (labelThatHasChanged == numPastEditable)
    {
        int intInput;
        bool valid = updateLabel<int>(labelThatHasChanged, sliderMin, sliderMax,
            static_cast<int>(numFutureSlider->getValue()), &intInput);

        if (valid)
        {
            int newNumFuture = sliderMax - intInput;
            numFutureSlider->setValue(intInput, dontSendNotification);
            numFutureEditable->setText(String(newNumFuture), dontSendNotification);
            processor->setParameter(pNumFuture, static_cast<float>(newNumFuture));
        }        
    }
    else if (labelThatHasChanged == numFutureEditable)
    {
        int intInput;
        bool valid = updateLabel<int>(labelThatHasChanged, 0, sliderMax - sliderMin,
            sliderMax - static_cast<int>(numFutureSlider->getValue()), &intInput);
        
        if (valid)
        {
            int newNumPast = sliderMax - intInput;
            numFutureSlider->setValue(newNumPast, dontSendNotification);
            numPastEditable->setText(String(newNumPast), dontSendNotification);
            processor->setParameter(pNumFuture, static_cast<float>(intInput));
        }
    }
    else if (labelThatHasChanged == recalcIntervalEditable)
    {
        int intInput;
        bool valid = updateLabel(labelThatHasChanged, 0, INT_MAX, processor->calcInterval, &intInput);

        if (valid)
            processor->setParameter(pRecalcInterval, static_cast<float>(intInput));
    }
    else if (labelThatHasChanged == arOrderEditable)
    {
        int intInput;
        bool valid = updateLabel<int>(labelThatHasChanged, 1, processor->bufferLength, processor->arOrder, &intInput);

        if (valid)
            processor->setParameter(pAROrder, static_cast<float>(intInput));

        // update slider's minimum value
        numFutureSlider->updateFromProcessor(processor);
    }
    else if (labelThatHasChanged == lowCutEditable)
    {
        float floatInput;
        bool valid = updateLabel<float>(labelThatHasChanged, 0.01F, 10000.0F, static_cast<float>(processor->lowCut), &floatInput);

        if (valid)
            processor->setParameter(pLowcut, floatInput);
    }
    else if (labelThatHasChanged == highCutEditable)
    {
        float floatInput;
        bool valid = updateLabel<float>(labelThatHasChanged, 0.01F, 10000.0F, static_cast<float>(processor->highCut), &floatInput);

        if (valid)
            processor->setParameter(pHighcut, floatInput);
    }
}

void PhaseCalculatorEditor::sliderEvent(Slider* slider)
{
    if (slider == numFutureSlider)
    {
        // At this point, a snapValue call has ensured that the new value is valid.
        int newVal = static_cast<int>(slider->getValue());
        int maxVal = static_cast<int>(slider->getMaximum());
        numPastEditable->setText(String(newVal), dontSendNotification);
        numFutureEditable->setText(String(maxVal - newVal), dontSendNotification);
        
        getProcessor()->setParameter(pNumFuture, static_cast<float>(maxVal - newVal));
    }
}


// based on FilterEditor code
void PhaseCalculatorEditor::buttonEvent(Button* button)
{
    PhaseCalculator* pc = static_cast<PhaseCalculator*>(getProcessor());
    if (button == applyToChan)
    {
        float newValue = button->getToggleState() ? 1.0F : 0.0F;

        // apply to active channels
        Array<int> chans = getActiveChannels();
        for (int i = 0; i < chans.size(); i++)
        {
            pc->setCurrentChannel(chans[i]);
            pc->setParameter(pEnabledState, newValue);
        }
    }
    else if (button == applyToADC)
    {
        float newValue = button->getToggleState() ? 1.0F : 0.0F;
        pc->setParameter(pAdcEnabled, newValue);
    }
}

// based on FilterEditor code
void PhaseCalculatorEditor::channelChanged(int chan, bool newState)
{
    PhaseCalculator* pc = static_cast<PhaseCalculator*>(getProcessor());
    applyToChan->setToggleState(pc->shouldProcessChannel[chan], dontSendNotification);
}

void PhaseCalculatorEditor::startAcquisition()
{
    GenericEditor::startAcquisition();
    processLengthBox->setEnabled(false);
    numFutureSlider->setEnabled(false);
    numPastEditable->setEnabled(false);
    numFutureEditable->setEnabled(false);
    lowCutEditable->setEnabled(false);
    highCutEditable->setEnabled(false);
    arOrderEditable->setEnabled(false);
}

void PhaseCalculatorEditor::stopAcquisition()
{
    GenericEditor::stopAcquisition();
    processLengthBox->setEnabled(true);
    numFutureSlider->setEnabled(true);
    numPastEditable->setEnabled(true);
    numFutureEditable->setEnabled(true);
    lowCutEditable->setEnabled(true);
    highCutEditable->setEnabled(true);
    arOrderEditable->setEnabled(true);
}

void PhaseCalculatorEditor::saveCustomParameters(XmlElement* xml)
{
    xml->setAttribute("Type", "PhaseCalculatorEditor");

    PhaseCalculator* processor = (PhaseCalculator*)(getProcessor());
    XmlElement* paramValues = xml->createNewChildElement("VALUES");
    paramValues->setAttribute("processLength", processor->processLength);
    paramValues->setAttribute("numFuture", processor->numFuture);
    paramValues->setAttribute("calcInterval", processor->calcInterval);
    paramValues->setAttribute("arOrder", processor->arOrder);
    paramValues->setAttribute("processADC", processor->processADC);
    paramValues->setAttribute("lowCut", processor->lowCut);
    paramValues->setAttribute("highCut", processor->highCut);
}

void PhaseCalculatorEditor::loadCustomParameters(XmlElement* xml)
{
    forEachXmlChildElementWithTagName(*xml, xmlNode, "VALUES")
    {
        processLengthBox->setText(xmlNode->getStringAttribute("processLength", processLengthBox->getText()), sendNotificationSync);
        numFutureEditable->setText(xmlNode->getStringAttribute("numFuture", numFutureEditable->getText()), sendNotificationSync);
        recalcIntervalEditable->setText(xmlNode->getStringAttribute("calcInterval", recalcIntervalEditable->getText()), sendNotificationSync);
        arOrderEditable->setText(xmlNode->getStringAttribute("glitchLim", arOrderEditable->getText()), sendNotificationSync);
        applyToADC->setToggleState(xmlNode->getBoolAttribute("processADC", applyToADC->getToggleState()), sendNotificationSync);
        lowCutEditable->setText(xmlNode->getStringAttribute("lowCut", lowCutEditable->getText()), sendNotificationSync);
        highCutEditable->setText(xmlNode->getStringAttribute("highCut", highCutEditable->getText()), sendNotificationSync);
    }
}

// static utilities

template<typename labelType>
bool PhaseCalculatorEditor::updateLabel(Label* labelThatHasChanged,
    labelType minValue, labelType maxValue, labelType defaultValue, labelType* result)
{
    String& input = labelThatHasChanged->getText();
    bool valid = parseInput(input, minValue, maxValue, result);
    if (!valid)
        labelThatHasChanged->setText(String(defaultValue), dontSendNotification);
    else
        labelThatHasChanged->setText(String(*result), dontSendNotification);

    return valid;
}

bool PhaseCalculatorEditor::parseInput(String& in, int min, int max, int* out)
{
    int parsedInt;
    try
    {
        parsedInt = std::stoi(in.toRawUTF8());
    }
    catch (...)
    {
        return false;
    }

    if (parsedInt < min)
        *out = min;
    else if (parsedInt > max)
        *out = max;
    else
        *out = parsedInt;

    return true;
}

bool PhaseCalculatorEditor::parseInput(String& in, float min, float max, float* out)
{
    float parsedFloat;
    try
    {
        parsedFloat = stof(in.toRawUTF8());
    }
    catch (...)
    {
        return false;
    }

    if (parsedFloat < min)
        *out = min;
    else if (parsedFloat > max)
        *out = max;
    else
        *out = parsedFloat;

    return true;
}

// ProcessBufferSlider definitions
ProcessBufferSlider::ProcessBufferSlider(const String& componentName)
    : Slider        (componentName)
    , realMinValue  (0)
{
    setLookAndFeel(&myLookAndFeel);
    setSliderStyle(LinearBar);
    setTextBoxStyle(NoTextBox, false, 40, 20);
    setScrollWheelEnabled(false);
}

ProcessBufferSlider::~ProcessBufferSlider() {}

double ProcessBufferSlider::snapValue(double attemptedValue, DragMode dragMode)
{
    if (attemptedValue < realMinValue)
        return realMinValue;
    else
        return attemptedValue;
}

void ProcessBufferSlider::updateFromProcessor(PhaseCalculator* parentNode)
{
    int processLength = parentNode->processLength;
    int numFuture = parentNode->numFuture;
    realMinValue = parentNode->arOrder;

    setRange(0, processLength, 1);
    setValue(0); // hack to ensure the listener gets called even if only the range is changed
    setValue(processLength - numFuture);
}

double ProcessBufferSlider::getRealMinValue()
{
    return realMinValue;
}
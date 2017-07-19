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


#include "PhaseCalculatorEditor.h"
#include "PhaseCalculator.h"

// local utility
namespace {

    /* Attempt to parse an input string into an integer between min and max, inclusive. 
     * Returns false if no integer could be parsed.
     */
    bool parseInput(String& in, int min, int max, int* out)
    {
        int parsedInt;
        try
        {
            parsedInt = std::stoi(in.toRawUTF8());
        }
        catch (const exception& e)
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

    // Same as above, but for floats
    bool parseInput(String& in, float min, float max, float* out)
    {
        float parsedFloat;
        try
        {
            parsedFloat = stof(in.toRawUTF8());
        }
        catch (const exception& e)
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
}

PhaseCalculatorEditor::PhaseCalculatorEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors)
    : GenericEditor(parentNode, useDefaultParameterEditors), lastProcessLength(1 << START_PLEN_POW)
{
    int filterWidth = 80;
    desiredWidth = filterWidth + 260;

    PhaseCalculator* processor = (PhaseCalculator*)(parentNode);

    lowCutLabel = new Label("lowCutL", "Low cut");
    lowCutLabel->setBounds(10, 30, 80, 20);
    lowCutLabel->setFont(Font("Small Text", 12, Font::plain));
    lowCutLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(lowCutLabel);

    lowCutEditable = new Label("lowCutE");
    lowCutEditable->setEditable(true);
    lowCutEditable->addListener(this);
    lowCutEditable->setBounds(15, 47, 60, 18);
    lowCutEditable->setText(String(processor->getLowCut()), dontSendNotification);
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
    highCutEditable->setText(String(processor->getHighCut()), dontSendNotification);
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
    processLengthBox->setText(String(processor->getProcessLength()), dontSendNotification);
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
    numFutureSlider->updateFromProcessor(parentNode);
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
    recalcIntervalEditable->setText(String(processor->getCalcInterval()), dontSendNotification);
    recalcIntervalEditable->setTooltip(RECALC_INTERVAL_TOOLTIP);
    addAndMakeVisible(recalcIntervalEditable);

    recalcIntervalUnit = new Label("recalcU", "ms");
    recalcIntervalUnit->setBounds(filterWidth + 200, 48, 25, 15);
    recalcIntervalUnit->setFont(Font("Small Text", 12, Font::plain));
    recalcIntervalUnit->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(recalcIntervalUnit);

    glitchLimLabel = new Label("glitchLimL", "Glitch limit:");
    glitchLimLabel->setBounds(filterWidth + 140, 65, 115, 20);
    glitchLimLabel->setFont(Font("Small Text", 12, Font::plain));
    glitchLimLabel->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(glitchLimLabel);

    glitchLimEditable = new Label("glitchLimE");
    glitchLimEditable->setEditable(true);
    glitchLimEditable->addListener(this);
    glitchLimEditable->setBounds(filterWidth + 145, 85, 55, 18);
    glitchLimEditable->setColour(Label::backgroundColourId, Colours::grey);
    glitchLimEditable->setColour(Label::textColourId, Colours::white);
    glitchLimEditable->setText(String(processor->getGlitchLimit()), dontSendNotification);
    glitchLimEditable->setTooltip(GLITCH_LIMIT_TOOLTIP);
    addAndMakeVisible(glitchLimEditable);

    glitchLimUnit = new Label("glitchLimU", "samp.");
    glitchLimUnit->setBounds(filterWidth + 200, 88, 45, 15);
    glitchLimUnit->setFont(Font("Small Text", 12, Font::plain));
    glitchLimUnit->setColour(Label::textColourId, Colours::darkgrey);
    addAndMakeVisible(glitchLimUnit);

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
        PhaseCalculator* processor = (PhaseCalculator*)(getProcessor());
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
            bool stringValid = parseInput(input, (1 << MIN_PLEN_POW), (1 << MAX_PLEN_POW), &newProcessLength);

            if (!stringValid)
            {
                processLengthBox->setText(String(lastProcessLength), dontSendNotification);
                return;
            }
           
            processLengthBox->setText(String(newProcessLength), dontSendNotification);
        }

        // calculate numFuture
        float currRatio = processor->getRatioFuture();
        float newNumFuture;
        if (newProcessLength <= AR_ORDER)
            newNumFuture = 0.0F;
        else
            newNumFuture = fminf(roundf(currRatio * (float)newProcessLength), (float)(newProcessLength - AR_ORDER));

        // choose order of setting parameters to avoid overflows
        int currProcessLength = processor->getProcessLength();
        if (currProcessLength < newProcessLength)
        {
            processor->setParameter(pQueueSize, (float)newProcessLength);
            processor->setParameter(pNumFuture, newNumFuture);
        }
        else if (currProcessLength > newProcessLength)
        {
            processor->setParameter(pNumFuture, newNumFuture);
            processor->setParameter(pQueueSize, (float)newProcessLength);
        }

        lastProcessLength = newProcessLength;

        // update slider
        numFutureSlider->updateFromProcessor(processor);
    }
}

void PhaseCalculatorEditor::labelTextChanged(Label* labelThatHasChanged)
{
    PhaseCalculator* processor = (PhaseCalculator*)(getProcessor());

    int sliderMin = (int) numFutureSlider->getRealMinValue();
    int sliderMax = (int) numFutureSlider->getMaximum();

    if (labelThatHasChanged == numPastEditable)
    {
        int intInput;
        bool valid = updateLabel<int>(labelThatHasChanged, sliderMin, sliderMax, numFutureSlider->getValue(), &intInput);

        if (valid)
        {
            int newNumFuture = sliderMax - intInput;
            numFutureSlider->setValue(intInput, dontSendNotification);
            numFutureEditable->setText(String(newNumFuture), dontSendNotification);
            processor->setParameter(pNumFuture, (float)newNumFuture);
        }        
    }
    else if (labelThatHasChanged == numFutureEditable)
    {
        int intInput;
        bool valid = updateLabel<int>(labelThatHasChanged, 0, sliderMax - sliderMin, sliderMax - numFutureSlider->getValue(), &intInput);
        
        if (valid)
        {
            int newNumPast = sliderMax - intInput;
            numFutureSlider->setValue(newNumPast, dontSendNotification);
            numPastEditable->setText(String(newNumPast), dontSendNotification);
            processor->setParameter(pNumFuture, (float)intInput);
        }
    }
    else if (labelThatHasChanged == recalcIntervalEditable)
    {
        int intInput;
        bool valid = updateLabel(labelThatHasChanged, 0, INT_MAX, processor->getCalcInterval(), &intInput);

        if (valid)
            processor->setParameter(pRecalcInterval, (float)intInput);
    }
    else if (labelThatHasChanged == glitchLimEditable)
    {
        int intInput;
        bool valid = updateLabel<int>(labelThatHasChanged, 0, INT_MAX, processor->getGlitchLimit(), &intInput);

        if (valid)
            processor->setParameter(pGlitchLimit, (float)intInput);
    }
    else if (labelThatHasChanged == lowCutEditable)
    {
        float floatInput;
        bool valid = updateLabel<float>(labelThatHasChanged, 0.01F, 10000.0F, (float)(processor->getLowCut()), &floatInput);

        if (valid)
            processor->setParameter(pLowcut, floatInput);
    }
    else if (labelThatHasChanged == highCutEditable)
    {
        float floatInput;
        bool valid = updateLabel<float>(labelThatHasChanged, 0.01F, 10000.0F, (float)(processor->getHighCut()), &floatInput);

        if (valid)
            processor->setParameter(pHighcut, floatInput);
    }
}

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

void PhaseCalculatorEditor::sliderEvent(Slider* slider)
{
    if (slider == numFutureSlider)
    {
        int newVal = (int)slider->getValue();
        int maxVal = (int)slider->getMaximum();
        numPastEditable->setText(String(newVal), dontSendNotification);
        numFutureEditable->setText(String(maxVal - newVal), dontSendNotification);
        
        getProcessor()->setParameter(pNumFuture, (float)(maxVal - newVal));
    }
}


// based on FilterEditor code
void PhaseCalculatorEditor::buttonEvent(Button* button)
{
    PhaseCalculator* pc = (PhaseCalculator*)getProcessor();
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
    PhaseCalculator* pc = (PhaseCalculator*)getProcessor();
    applyToChan->setToggleState(pc->getEnabledStateForChannel(chan), dontSendNotification);
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
}

void PhaseCalculatorEditor::saveCustomParameters(XmlElement* xml)
{
    xml->setAttribute("Type", "PhaseCalculatorEditor");

    PhaseCalculator* processor = (PhaseCalculator*)(getProcessor());
    XmlElement* paramValues = xml->createNewChildElement("VALUES");
    paramValues->setAttribute("processLength", processor->getProcessLength());
    paramValues->setAttribute("numFuture", processor->getNumFuture());
    paramValues->setAttribute("calcInterval", processor->getCalcInterval());
    paramValues->setAttribute("glitchLim", processor->getGlitchLimit());
    paramValues->setAttribute("processADC", processor->getProcessADC());
    paramValues->setAttribute("lowCut", processor->getLowCut());
    paramValues->setAttribute("highCut", processor->getHighCut());
}

void PhaseCalculatorEditor::loadCustomParameters(XmlElement* xml)
{
    forEachXmlChildElementWithTagName(*xml, xmlNode, "VALUES")
    {
        processLengthBox->setText(xmlNode->getStringAttribute("processLength", String(lastProcessLength)), sendNotificationSync);
        numFutureEditable->setText(xmlNode->getStringAttribute("numFuture", numFutureEditable->getText()), sendNotificationSync);
        recalcIntervalEditable->setText(xmlNode->getStringAttribute("calcInterval", recalcIntervalEditable->getText()), sendNotificationSync);
        glitchLimEditable->setText(xmlNode->getStringAttribute("glitchLim", glitchLimEditable->getText()), sendNotificationSync);
        applyToADC->setToggleState(xmlNode->getBoolAttribute("processADC", applyToADC->getToggleState()), sendNotificationSync);
        lowCutEditable->setText(xmlNode->getStringAttribute("lowCut", lowCutEditable->getText()), sendNotificationSync);
        highCutEditable->setText(xmlNode->getStringAttribute("highCut", highCutEditable->getText()), sendNotificationSync);
    }
}

// ProcessBufferSlider definitions
ProcessBufferSlider::ProcessBufferSlider(const String& componentName) : Slider(componentName)
{
    realMinValue = AR_ORDER;
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

void ProcessBufferSlider::updateFromProcessor(GenericProcessor* parentNode)
{
    PhaseCalculator* pCalculator = (PhaseCalculator*)(parentNode);
    int processLength = pCalculator->getProcessLength();
    int numFuture = pCalculator->getNumFuture();
    setRange(0, processLength, 1);
    setValue(0); // hack to ensure the listener gets called even if only the range is changed
    setValue(processLength - numFuture);
}

double ProcessBufferSlider::getRealMinValue()
{
    return realMinValue;
}
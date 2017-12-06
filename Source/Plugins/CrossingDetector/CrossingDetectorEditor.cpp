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

#include "CrossingDetectorEditor.h"
#include "CrossingDetector.h"

CrossingDetectorEditor::CrossingDetectorEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors)
    : VisualizerEditor(parentNode, 205, useDefaultParameterEditors)
{
    tabText = "Crossing Detector";
    CrossingDetector* processor = static_cast<CrossingDetector*>(parentNode);

    /* ------------- Top row (channels) ------------- */

    inputLabel = createLabel("InputChanL", "In:", Rectangle(12, 36, 30, 18));
    addAndMakeVisible(inputLabel);

    inputBox = new ComboBox("Input channel");
    inputBox->setTooltip("Continuous channel to analyze");
    inputBox->setBounds(45, 36, 40, 18);
    inputBox->addListener(this);
    addAndMakeVisible(inputBox);

    outputLabel = createLabel("OutL", "Out:", Rectangle(95, 36, 40, 18));
    addAndMakeVisible(outputLabel);

    outputBox = new ComboBox("Out event channel");
    for (int chan = 1; chan <= 8; chan++)
        outputBox->addItem(String(chan), chan);
    outputBox->setSelectedId(processor->eventChan + 1);
    outputBox->setBounds(140, 36, 40, 18);
    outputBox->setTooltip("Output event channel");
    outputBox->addListener(this);
    addAndMakeVisible(outputBox);

    /* ------------ Middle row (conditions) -------------- */

    risingButton = new UtilityButton("RISING", Font("Default", 10, Font::plain));
    risingButton->addListener(this);
    risingButton->setBounds(15, 65, 60, 18);
    risingButton->setClickingTogglesState(true);
    bool enable = processor->posOn;
    risingButton->setToggleState(enable, dontSendNotification);
    risingButton->setTooltip("Trigger events when past samples are below and future samples are above the threshold");
    addAndMakeVisible(risingButton);

    fallingButton = new UtilityButton("FALLING", Font("Default", 10, Font::plain));
    fallingButton->addListener(this);
    fallingButton->setBounds(15, 85, 60, 18);
    fallingButton->setClickingTogglesState(true);
    enable = processor->negOn;
    fallingButton->setToggleState(enable, dontSendNotification);
    fallingButton->setTooltip("Trigger events when past samples are above and future samples are below the threshold");
    addAndMakeVisible(fallingButton);

    acrossLabel = createLabel("AcrossL", "across", Rectangle(77, 75, 60, 18));
    addAndMakeVisible(acrossLabel);

    thresholdEditable = createEditable("Threshold", String(processor->threshold),
        "Threshold voltage", Rectangle(140, 75, 40, 18));
    addAndMakeVisible(thresholdEditable);

    /* --------- Bottom row (timeout) ------------- */

    timeoutLabel = createLabel("TimeoutL", "Timeout:", Rectangle(30, 108, 64, 18));
    addAndMakeVisible(timeoutLabel);

    timeoutEditable = createEditable("Timeout", String(processor->timeout),
        "Minimum length of time between consecutive events", Rectangle(97, 108, 50, 18));
    addAndMakeVisible(timeoutEditable);

    timeoutUnitLabel = createLabel("TimeoutUnitL", "ms", Rectangle(150, 108, 30, 18));
    addAndMakeVisible(timeoutUnitLabel);

    /************** Canvas elements *****************/

    pastSpanLabel = createLabel("PastSpanL", "Past:   Span:", Rectangle(8, 68, 100, 18));

    pastSpanEditable = createEditable("PastSpanE", String(processor->pastSpan),
        "Number of samples considered before a potential crossing", Rectangle(110, 68, 33, 18));

    pastStrictLabel = createLabel("PastStrictL", "Strictness:", Rectangle(155, 68, 110, 18));

    pastPctEditable = createEditable("PastPctE", String(100 * processor->pastStrict),
        "Percent of considered past samples required to be above/below threshold", Rectangle(250, 68, 33, 18));

    pastPctLabel = createLabel("pastPctL", "%", Rectangle(285, 68, 20, 18));

    futureSpanLabel = createLabel("FutureSpanL", "Future: Span:", Rectangle(8, 88, 100, 18));

    futureSpanEditable = createEditable("FutureSpanE", String(processor->futureSpan),
        "Number of samples considered after a potential crossing", Rectangle(110, 88, 33, 18));

    futureStrictLabel = createLabel("FutureStrictL", "Strictness:", Rectangle(155, 88, 110, 18));

    futurePctEditable = createEditable("FuturePctE", String(100 * processor->futureStrict),
        "Percent of considered future samples required to be above/below threshold", Rectangle(250, 88, 33, 18));

    futurePctLabel = createLabel("futurePctL", "%", Rectangle(285, 88, 20, 18));

    durLabel = createLabel("DurL", "Dur:", Rectangle(112, 108, 35, 18));

    durationEditable = createEditable("Event Duration", String(processor->eventDuration),
        "Duration of each event", Rectangle(151, 108, 50, 18));
}

CrossingDetectorEditor::~CrossingDetectorEditor() {}

void CrossingDetectorEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == inputBox)
        getProcessor()->setParameter(pInputChan, static_cast<float>(inputBox->getSelectedId() - 1));
    else if (comboBoxThatHasChanged == outputBox)
        getProcessor()->setParameter(pEventChan, static_cast<float>(outputBox->getSelectedId() - 1));
}

void CrossingDetectorEditor::labelTextChanged(Label* labelThatHasChanged)
{
    CrossingDetector* processor = static_cast<CrossingDetector*>(getProcessor());

    if (labelThatHasChanged == durationEditable)
    {
        int newVal;
        bool success = updateIntLabel(labelThatHasChanged, 0, INT_MAX, processor->eventDuration, &newVal);

        if (success)
            processor->setParameter(pEventDur, static_cast<float>(newVal));
    }
    else if (labelThatHasChanged == timeoutEditable)
    {
        int newVal;
        bool success = updateIntLabel(labelThatHasChanged, 0, INT_MAX, processor->timeout, &newVal);

        if (success)
            processor->setParameter(pTimeout, static_cast<float>(newVal));
    }
    else if (labelThatHasChanged == thresholdEditable)
    {
        float newVal;
        bool success = updateFloatLabel(labelThatHasChanged, -FLT_MAX, FLT_MAX, processor->threshold, &newVal);

        if (success)
            processor->setParameter(pThreshold, newVal);
    }
    else if (labelThatHasChanged == pastPctEditable)
    {
        float newVal;
        bool success = updateFloatLabel(labelThatHasChanged, 0, 100, 100 * processor->pastStrict, &newVal);

        if (success)
            processor->setParameter(pPastStrict, newVal / 100);
    }
    else if (labelThatHasChanged == pastSpanEditable)
    {
        int newVal;
        bool success = updateIntLabel(labelThatHasChanged, 0, processor->MAX_PAST_SPAN, processor->pastSpan, &newVal);

        if (success)
            processor->setParameter(pPastSpan, static_cast<float>(newVal));
    }
    else if (labelThatHasChanged == futurePctEditable)
    {
        float newVal;
        bool success = updateFloatLabel(labelThatHasChanged, 0, 100, 100 * processor->futureStrict, &newVal);

        if (success)
            processor->setParameter(pFutureStrict, newVal / 100);
    }
    else if (labelThatHasChanged == futureSpanEditable)
    {
        int newVal;
        bool success = updateIntLabel(labelThatHasChanged, 0, processor->MAX_FUTURE_SPAN, processor->futureSpan, &newVal);

        if (success)
            processor->setParameter(pFutureSpan, static_cast<float>(newVal));
    }
}

void CrossingDetectorEditor::buttonEvent(Button* button)
{
    if (button == risingButton)
        getProcessor()->setParameter(pPosOn, static_cast<float>(button->getToggleState()));
    else if (button == fallingButton)
        getProcessor()->setParameter(pNegOn, static_cast<float>(button->getToggleState()));
}

void CrossingDetectorEditor::updateSettings()
{
    CrossingDetector* processor = static_cast<CrossingDetector*>(getProcessor());

    // update input combo box
    int numInputs = processor->settings.numInputs;
    int numBoxItems = inputBox->getNumItems();
    if (numInputs != numBoxItems)
    {
        int currId = inputBox->getSelectedId();
        inputBox->clear(dontSendNotification);
        for (int chan = 1; chan <= numInputs; chan++)
            // using 1-based ids since 0 is reserved for "nothing selected"
            inputBox->addItem(String(chan), chan);
        if (numInputs > 0 && (currId < 1 || currId > numInputs))
            inputBox->setSelectedId(1, sendNotificationAsync);
        else
            inputBox->setSelectedId(currId, dontSendNotification);
    }
    
}

void CrossingDetectorEditor::startAcquisition()
{
    inputBox->setEnabled(false);
}

void CrossingDetectorEditor::stopAcquisition()
{
    inputBox->setEnabled(true);
}

Visualizer* CrossingDetectorEditor::createNewCanvas()
{
    canvas = new CrossingDetectorCanvas(getProcessor());
    return canvas;
}

Array<Component*> CrossingDetectorEditor::getCanvasElements()
{
    Array<Component*> canvEls({ /*randomizeButton.get(), limitButton.get()*/ });
    canvEls.addArray({
        //minThreshEditable.get(),
        //maxThreshEditable.get(),
        //limitEditable.get(),
        pastPctEditable.get(),
        pastSpanEditable.get(),
        futurePctEditable.get(),
        futureSpanEditable.get(),
        durationEditable.get(),
        //minThreshLabel.get(),
        //maxThreshLabel.get(),
        //limitLabel.get(),
        pastSpanLabel.get(),
        pastStrictLabel.get(),
        pastPctLabel.get(),
        futureSpanLabel.get(),
        futureStrictLabel.get(),
        futurePctLabel.get(),
        durLabel.get(),
        //durUnitLabel.get()
    });

    return canvEls;
}

void CrossingDetectorEditor::saveCustomParameters(XmlElement* xml)
{
    xml->setAttribute("Type", "CrossingDetectorEditor");

    CrossingDetector* processor = static_cast<CrossingDetector*>(getProcessor());
    XmlElement* paramValues = xml->createNewChildElement("VALUES");

    paramValues->setAttribute("inputChanId", inputBox->getSelectedId());
    paramValues->setAttribute("bRising", risingButton->getToggleState());
    paramValues->setAttribute("bFalling", fallingButton->getToggleState());
    paramValues->setAttribute("threshold", thresholdEditable->getText());
    paramValues->setAttribute("pastPct", pastPctEditable->getText());
    paramValues->setAttribute("pastSpan", pastSpanEditable->getText());
    paramValues->setAttribute("futurePct", futurePctEditable->getText());
    paramValues->setAttribute("futureSpan", futureSpanEditable->getText());
    paramValues->setAttribute("outputChanId", outputBox->getSelectedId());
    paramValues->setAttribute("duration", durationEditable->getText());
    paramValues->setAttribute("timeout", timeoutEditable->getText());
}

void CrossingDetectorEditor::loadCustomParameters(XmlElement* xml)
{
    forEachXmlChildElementWithTagName(*xml, xmlNode, "VALUES")
    {
        inputBox->setSelectedId(xmlNode->getIntAttribute("inputChanId", inputBox->getSelectedId()), sendNotificationSync);
        risingButton->setToggleState(xmlNode->getBoolAttribute("bRising", risingButton->getToggleState()), sendNotificationSync);
        fallingButton->setToggleState(xmlNode->getBoolAttribute("bFalling", fallingButton->getToggleState()), sendNotificationSync);
        thresholdEditable->setText(xmlNode->getStringAttribute("threshold", thresholdEditable->getText()), sendNotificationSync);
        pastPctEditable->setText(xmlNode->getStringAttribute("pastPct", pastPctEditable->getText()), sendNotificationSync);
        pastSpanEditable->setText(xmlNode->getStringAttribute("pastSpan", pastSpanEditable->getText()), sendNotificationSync);
        futurePctEditable->setText(xmlNode->getStringAttribute("futurePct", futurePctEditable->getText()), sendNotificationSync);
        futureSpanEditable->setText(xmlNode->getStringAttribute("futureSpan", futureSpanEditable->getText()), sendNotificationSync);
        outputBox->setSelectedId(xmlNode->getIntAttribute("outputChanId", outputBox->getSelectedId()), sendNotificationSync);
        durationEditable->setText(xmlNode->getStringAttribute("duration", durationEditable->getText()), sendNotificationSync);
        timeoutEditable->setText(xmlNode->getStringAttribute("timeout", timeoutEditable->getText()), sendNotificationSync);
    }
}

/**************** private ******************/

Label* CrossingDetectorEditor::createEditable(const String& name, const String& initialValue,
    const String& tooltip, const Rectangle bounds)
{
    Label* editable = new Label(name, initialValue);
    editable->setEditable(true);
    editable->addListener(this);
    editable->setBounds(bounds);
    editable->setColour(Label::backgroundColourId, Colours::grey);
    editable->setColour(Label::textColourId, Colours::white);
    editable->setTooltip(tooltip);
    return editable;
}

Label* CrossingDetectorEditor::createLabel(const String& name, const String& text,
    const Rectangle bounds)
{
    Label* label = new Label(name, text);
    label->setBounds(bounds);
    label->setFont(Font("Small Text", 12, Font::plain));
    label->setColour(Label::textColourId, Colours::darkgrey);
    return label;
}

/* Attempts to parse the current text of a label as an int between min and max inclusive.
*  If successful, sets "*out" and the label text to this value and and returns true.
*  Otherwise, sets the label text to defaultValue and returns false.
*/
bool CrossingDetectorEditor::updateIntLabel(Label* label, int min, int max, int defaultValue, int* out)
{
    const String& in = label->getText();
    int parsedInt;
    try
    {
        parsedInt = std::stoi(in.toRawUTF8());
    }
    catch (const std::exception& e)
    {
        label->setText(String(defaultValue), dontSendNotification);
        return false;
    }

    if (parsedInt < min)
        *out = min;
    else if (parsedInt > max)
        *out = max;
    else
        *out = parsedInt;

    label->setText(String(*out), dontSendNotification);
    return true;
}

// Like updateIntLabel, but for floats
bool CrossingDetectorEditor::updateFloatLabel(Label* label, float min, float max, float defaultValue, float* out)
{
    const String& in = label->getText();
    float parsedFloat;
    try
    {
        parsedFloat = std::stof(in.toRawUTF8());
    }
    catch (const std::exception& e)
    {
        label->setText(String(defaultValue), dontSendNotification);
        return false;
    }

    if (parsedFloat < min)
        *out = min;
    else if (parsedFloat > max)
        *out = max;
    else
        *out = parsedFloat;

    label->setText(String(*out), dontSendNotification);
    return true;
}

/*************** canvas (extra settings) *******************/

CrossingDetectorCanvas::CrossingDetectorCanvas(GenericProcessor* n)
    : processor(n)
{
    editor = static_cast<CrossingDetectorEditor*>(processor->editor.get());
    viewport = new Viewport();
    panel = new CDOptionsPanel(processor, this, viewport);
    viewport->setViewedComponent(panel, false);
    viewport->setScrollBarsShown(true, true);
    addAndMakeVisible(viewport);
}

CrossingDetectorCanvas::~CrossingDetectorCanvas() {}

void CrossingDetectorCanvas::refreshState() {}
void CrossingDetectorCanvas::update() {}
void CrossingDetectorCanvas::refresh() {}
void CrossingDetectorCanvas::beginAnimation() {}
void CrossingDetectorCanvas::endAnimation() {}
void CrossingDetectorCanvas::setParameter(int, float) {}
void CrossingDetectorCanvas::setParameter(int, int, int, float) {}

void CrossingDetectorCanvas::paint(Graphics& g)
{
    ColourGradient editorBg = editor->getBackgroundGradient();
    g.fillAll(editorBg.getColourAtPosition(0.5)); // roughly matches editor background (without gradient)
}

void CrossingDetectorCanvas::resized()
{
    viewport->setBounds(0, 0, getWidth(), getHeight());
}

CDOptionsPanel::CDOptionsPanel(GenericProcessor* proc, CrossingDetectorCanvas* canv, Viewport* view)
    : processor(proc), canvas(canv), viewport(view)
{
    CrossingDetectorEditor* ed = canvas->editor;

    auto parentBounds = juce::Rectangle<int>(0, 0, 1, 1);
    for (Component* c : ed->getCanvasElements())
    {
        addAndMakeVisible(c);
        parentBounds = parentBounds.getUnion(c->getBounds());
    }
    setBounds(parentBounds);
}

CDOptionsPanel::~CDOptionsPanel() {}
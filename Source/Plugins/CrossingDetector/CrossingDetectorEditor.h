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

#ifndef CROSSING_DETECTOR_EDITOR_H_INCLUDED
#define CROSSING_DETECTOR_EDITOR_H_INCLUDED

#include <VisualizerEditorHeaders.h>
#include <VisualizerWindowHeaders.h>
#include <string>
#include <climits>
#include <cfloat>

/*
Editor consists of:
-Combo box to select crossing direction to detect
-Combo box to select input (continuous) channel
-Combo box to select output (event) channel
-Editable label to specify the duration of each event, in samples
-Editable label to specify the timeout period after each event, in samples
-Editable label to enter the threshold sample value (crossing of which triggers an event)
-Editable label to enter the percentage of past values required
-Editable label to enter the number of past values to consider
-Editable label to enter the number of future values required
-Editable label to enter the number of future values to consider

@see GenericEditor

*/

class CrossingDetectorCanvas;
class CDOptionsPanel;

class CrossingDetectorEditor : public VisualizerEditor,
    public ComboBox::Listener, public Label::Listener
{
public:
    CrossingDetectorEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors = false);
    ~CrossingDetectorEditor();
    void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;
    void labelTextChanged(Label* labelThatHasChanged) override;

    // overrides GenericEditor
    void buttonEvent(Button* button) override;

    void updateSettings() override;

    // disable input channel selection during acquisition so that events work correctly
    void startAcquisition() override;
    void stopAcquisition() override;

    Visualizer* createNewCanvas() override;

    // provide pointers to the UI elements which should appear in the canvas
    Array<Component*> getCanvasElements();

    void saveCustomParameters(XmlElement* xml) override;
    void loadCustomParameters(XmlElement* xml) override;

private:
    typedef juce::Rectangle<int> Rectangle;

    // Basic UI element creation methods. Always register "this" (the editor) as the listener,
    // but may specify a different Component in which to actually display the element.
    Label* createEditable(const String& name, const String& initialValue,
        const String& tooltip, const Rectangle bounds);
    Label* createLabel(const String& name, const String& text, const Rectangle bounds);

    // Utilities for parsing entered values
    static bool updateIntLabel(Label* label, int min, int max, int defaultValue, int* out);
    static bool updateFloatLabel(Label* label, float min, float max, float defaultValue, float* out);

    // top row (channels)
    ScopedPointer<Label> inputLabel;
    ScopedPointer<ComboBox> inputBox;
    ScopedPointer<Label> outputLabel;
    ScopedPointer<ComboBox> outputBox;

    // middle row (threshold)
    ScopedPointer<UtilityButton> risingButton;
    ScopedPointer<UtilityButton> fallingButton;
    ScopedPointer<Label> acrossLabel;
    ScopedPointer<Label> thresholdEditable;

    // bottom row (timeout)
    ScopedPointer<Label> timeoutLabel;
    ScopedPointer<Label> timeoutEditable;
    ScopedPointer<Label> timeoutUnitLabel;

    // Canvas elements are managed by editor but invisible until visualizer is opened
    CrossingDetectorCanvas* canvas;
    
    ScopedPointer<ToggleButton> randomizeButton;
    ScopedPointer<ToggleButton> limitButton;

    // editable labels
    ScopedPointer<Label> minThreshEditable;
    ScopedPointer<Label> maxThreshEditable;
    ScopedPointer<Label> limitEditable;
    ScopedPointer<Label> pastPctEditable;
    ScopedPointer<Label> pastSpanEditable;
    ScopedPointer<Label> futurePctEditable;
    ScopedPointer<Label> futureSpanEditable;
    ScopedPointer<Label> durationEditable;

    // static labels
    ScopedPointer<Label> minThreshLabel;
    ScopedPointer<Label> maxThreshLabel;
    ScopedPointer<Label> limitLabel;
    ScopedPointer<Label> pastSpanLabel;
    ScopedPointer<Label> pastStrictLabel;
    ScopedPointer<Label> pastPctLabel;
    ScopedPointer<Label> futureSpanLabel;
    ScopedPointer<Label> futureStrictLabel;
    ScopedPointer<Label> futurePctLabel;
    ScopedPointer<Label> durLabel;
    ScopedPointer<Label> durUnitLabel;
};

// Visualizer window containing additional settings

class CrossingDetectorCanvas : public Visualizer
{
public:
    CrossingDetectorCanvas(GenericProcessor* n);
    ~CrossingDetectorCanvas();
    void refreshState() override;
    void update() override;
    void refresh() override;
    void beginAnimation() override;
    void endAnimation() override;
    void setParameter(int, float) override;
    void setParameter(int, int, int, float) override;

    void paint(Graphics& g) override;
    void resized() override;

    GenericProcessor* processor;
    CrossingDetectorEditor* editor;
private:
    ScopedPointer<Viewport> viewport;
    ScopedPointer<CDOptionsPanel> panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossingDetectorCanvas);
};

class CDOptionsPanel : public Component
{
public:
    CDOptionsPanel(GenericProcessor* proc, CrossingDetectorCanvas* canv, Viewport* view);
    ~CDOptionsPanel();
    GenericProcessor* processor;
private:
    Viewport* viewport;
    CrossingDetectorCanvas* canvas;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CDOptionsPanel);
};

#endif // CROSSING_DETECTOR_EDITOR_H_INCLUDED

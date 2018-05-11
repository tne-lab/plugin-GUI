/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2018 Translational NeuroEngineering Laboratory, MGH

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

#ifndef PHASE_CALCULATOR_CANVAS_H_INCLUDED
#define PHASE_CALCULATOR_CANVAS_H_INCLUDED

#include "PhaseCalculator.h"
#include <VisualizerWindowHeaders.h>
#include <set> // std::multiset

class RosePlot  
    : public Component
    , public Label::Listener
{
public:
    RosePlot();
    ~RosePlot();

    void paint(Graphics& g) override;
    
    // Change number of bins and repaint
    void setNumBins(int newNumBins);

    // Change reference angle and repaint
    void setReference(double newReference);

    // Add a new angle (in radians) and repaint
    void addAngle(double newAngle);

    // Remove all angles from the plot and repaint
    void clear();

    // implements Label::Listener
    void labelTextChanged(Label* labelThatHasChanged) override;

    static const int MAX_BINS = 120;
    static const int START_NUM_BINS = 24;
    static const int START_REFERENCE = 0;

private:
    struct circularBinComparator
    {
        circularBinComparator(int numBinsIn, double referenceAngleIn);

        // Depending on numBins and reference, returns true iff lhs belongs in a lower bin than rhs.
        bool operator() (const double& lhs, const double& rhs) const;

    private:
        int numBins;
        double referenceAngle;
    };

    class AngleDataMultiset : public std::multiset<double, circularBinComparator>
    {
    public:
        // create empty multiset
        AngleDataMultiset(int numBins, double referenceAngle);

        // copy nodes from dataSource to newly constructed multiset
        AngleDataMultiset(int numBins, double referenceAngle, AngleDataMultiset* dataSource);
    };

    // circular distance in radians (return value is in [0, 2*pi))
    static double circDist(double x, double ref);

    // make binMidpoints and segmentAngles reflect current numBins
    void updateAngles();

    // reassign angleData to refer to a new AngleDataMultiset with the current parameters
    // (but keep the same data points)
    void reorganizeAngleData();

    ScopedPointer<AngleDataMultiset> angleData;
    int numBins;
    double referenceAngle;

    // for each rose plot segment:
    // midpoint angle in radians CCW from positive x axis
    Array<double> binMidpoints;
    // inputs to addPieSegment (clockwise from top)
    Array<std::pair<float, float>> segmentAngles;

    Colour faceColor;
    Colour edgeColor;
    Colour bgColor;
    float edgeWeight;

    static const double PI;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RosePlot);
};

class PhaseCalculatorCanvas 
    : public Visualizer
    , public ComboBox::Listener
    , public Slider::Listener
    , public Button::Listener
{
public:
    PhaseCalculatorCanvas(PhaseCalculator* pc);
    ~PhaseCalculatorCanvas();
    void refreshState() override;
    void update() override;
    void refresh() override;
    void beginAnimation() override;
    void endAnimation() override;
    void setParameter(int, float) override;
    void setParameter(int, int, int, float) override;

    void paint(Graphics& g) override;
    void resized() override;

    void addAngle(double newAngle);
    void clearAngles();

    // implements ComboBox::Listener
    void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;

    // implements Slider::Listener
    void sliderValueChanged(Slider* slider) override;

    // implements Button::Listener
    void buttonClicked(Button* button) override;

    void saveVisualizerParameters(XmlElement* xml) override;
    void loadVisualizerParameters(XmlElement* xml) override;

private:
    int getRosePlotDiameter(int height, int* verticalPadding);
    int getContentWidth(int width, int diameter, int* leftPadding);

    // if false, we're not reading phases.
    bool eventChannelSelected;

    PhaseCalculator* processor;

    ScopedPointer<Viewport> viewport;
    ScopedPointer<Component> canvas;
    ScopedPointer<Component> rosePlotOptions;
    ScopedPointer<RosePlot> rosePlot;

    // options panel
    ScopedPointer<Label>          cChannelLabel;
    ScopedPointer<ComboBox>       cChannelBox;
    ScopedPointer<Label>          eChannelLabel;
    ScopedPointer<ComboBox>       eChannelBox;

    ScopedPointer<Label>    numBinsLabel;
    ScopedPointer<Slider>   numBinsSlider;

    ScopedPointer<UtilityButton> clearButton;

    ScopedPointer<Label>        referenceLabel;
    ScopedPointer<Label>        referenceEditable;
    
    // for future, maybe?
    //ScopedPointer<Label>          meanLabel;
    //ScopedPointer<Label>          meanIndicator;
    //ScopedPointer<Label>          varLabel;
    //ScopedPointer<Label>          varIndicator;

    static const int MIN_PADDING = 10;
    static const int MAX_LEFT_PADDING = 50;
    static const int MIN_DIAMETER = 250;
    static const int MAX_DIAMETER = 500;
    static const int OPTIONS_WIDTH = 300;

    const String C_CHAN_TOOLTIP = "Channel containing data whose high-accuracy phase is calculated for each event";
    const String REF_TOOLTIP = "Base phase (in degrees) to subtract from each calculated phase";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseCalculatorCanvas);
};

#endif // PHASE_CALCULATOR_CANVAS_H_INCLUDED
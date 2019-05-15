/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2019 Translational NeuroEngineering Laboratory

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

#include "CoherenceVisualizer.h"

CoherenceVisualizer::CoherenceVisualizer(CoherenceNode* n)
    : viewport  (new Viewport())
    , canvas    (new Component("canvas"))
    , processor (n)
{
    refreshRate = .5;
    juce::Rectangle<int> canvasBounds(0, 0, 1, 1);
    juce::Rectangle<int> bounds;
    juce::Rectangle<int> opBounds(0, 0, 1, 1);

    curComb = 0;

    const int TEXT_HT = 18;

    // ------- Options ------- //
    int xPos = 15;
    optionsTitle = new Label("OptionsTitle", "Coherence Viewer Additional Settings");
    optionsTitle->setBounds(bounds = { xPos, 30, 400, 50 });
    optionsTitle->setFont(Font(20, Font::bold));
    canvas->addAndMakeVisible(optionsTitle);
    opBounds = opBounds.getUnion(bounds);

    //// Grouping

    //// Combination choice
    combinationBox = new ComboBox("Combination Selection Box");
    combinationBox->setTooltip("Combination to graph");
    combinationBox->setBounds(xPos, 90, 40, TEXT_HT);
    combinationBox->addListener(this);
    canvas->addAndMakeVisible(combinationBox);

    // ------- Plot ------- //
    cohPlot = new MatlabLikePlot();
    cohPlot->setBounds(bounds = { 80, 70, 600, 500 });
    cohPlot->setRange(0, 40, 0, 1, true);
    cohPlot->setControlButtonsVisibile(false);

    canvas->addAndMakeVisible(cohPlot);
    canvasBounds = canvasBounds.getUnion(bounds);
    
    // some extra padding
    canvasBounds.setBottom(canvasBounds.getBottom() + 10);
    canvasBounds.setRight(canvasBounds.getRight() + 10);

    canvas->setBounds(canvasBounds);
    viewport->setViewedComponent(canvas, false);
    viewport->setScrollBarsShown(true, true);
    addAndMakeVisible(viewport);

    startCallbacks();
}


CoherenceVisualizer::~CoherenceVisualizer()
{
    stopCallbacks();
}


void CoherenceVisualizer::resized()
{
    viewport->setSize(getWidth(), getHeight());
}

void CoherenceVisualizer::refreshState() {}
void CoherenceVisualizer::update()
{
    int numInputs = processor->getNumInputs();
    freqStep = processor->freqStep;

    nCombs = processor->nGroupCombs;
    combinationBox->clear(dontSendNotification);
    for (int i = 1; i <= nCombs; i++)
    {
        // using 1-based ids since 0 is reserved for "nothing selected"
        combinationBox->addItem(String(i), i);
    }
}


void CoherenceVisualizer::refresh() 
{
    if (processor->meanCoherence.hasUpdate())
    {
        AtomicScopedReadPtr<std::vector<std::vector<double>>> coherenceReader(processor->meanCoherence);
        coherenceReader.pullUpdate();
        
        std::vector<float> coh(coherenceReader->at(curComb).size());
        for (int i = 0; i < coherenceReader->at(curComb).size() - 1; i++)
        {
            coh[i] = coherenceReader->at(curComb)[i];
        }

        XYline cohLine(0, freqStep, coh, 1, Colours::yellow);

        cohPlot->clearplot();
        cohPlot->plotxy(cohLine);
        cohPlot->repaint();
    }
}

void CoherenceVisualizer::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == combinationBox)
    {
        curComb = static_cast<int>(combinationBox->getSelectedId() - 1);
    }
}

void CoherenceVisualizer::beginAnimation() {}
void CoherenceVisualizer::endAnimation() {}
void CoherenceVisualizer::setParameter(int, float) {}
void CoherenceVisualizer::setParameter(int, int, int, float) {}
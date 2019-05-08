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

    cohPlot = new MatlabLikePlot();
    cohPlot->setBounds(bounds = { 50, 50, 800, 300 });
    cohPlot->setRange(0, 30, 0, 20, true);
    cohPlot->setControlButtonsVisibile(true);

    //XYline testLine(0.5, 0.5, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, 1, Colours::red);
    //testPlot->plotxy(testLine);

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
void CoherenceVisualizer::update() {}


void CoherenceVisualizer::refresh() 
{
    float freqStep = processor->freqStep;

    AtomicScopedReadPtr<std::vector<std::vector<double>>> coherenceReader(processor->meanCoherence);
    coherenceReader.pullUpdate();
    std::vector<float> coh;
    for (int i = 0; i < coherenceReader->at(0).size(); i++)
    {
         coh[i] = coherenceReader->at(0).at(i);
    }
    
    XYline cohLine(0, freqStep, coh, 1, Colours::yellow);
  
    cohPlot->plotxy(cohLine);
    cohPlot->repaint();
}


void CoherenceVisualizer::beginAnimation() {}
void CoherenceVisualizer::endAnimation() {}
void CoherenceVisualizer::setParameter(int, float) {}
void CoherenceVisualizer::setParameter(int, int, int, float) {}
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

CoherenceVisualizer::CoherenceVisualizer()
    : viewport  (new Viewport())
    , canvas    (new Component("canvas"))
{
    juce::Rectangle<int> canvasBounds(0, 0, 1, 1);
    juce::Rectangle<int> bounds;

    testPlot = new MatlabLikePlot();
    testPlot->setBounds(bounds = { 50, 50, 500, 200 });
    canvas->addAndMakeVisible(testPlot);
    canvasBounds = canvasBounds.getUnion(bounds);
    
    // some extra padding
    canvasBounds.setBottom(canvasBounds.getBottom() + 10);
    canvasBounds.setRight(canvasBounds.getRight() + 10);

    canvas->setBounds(canvasBounds);
    viewport->setViewedComponent(canvas, false);
    viewport->setScrollBarsShown(true, true);
    addAndMakeVisible(viewport);
}

void CoherenceVisualizer::resized()
{
    viewport->setSize(getWidth(), getHeight());
}

void CoherenceVisualizer::refreshState() {}
void CoherenceVisualizer::update() {}
void CoherenceVisualizer::refresh() {}
void CoherenceVisualizer::beginAnimation() {}
void CoherenceVisualizer::endAnimation() {}
void CoherenceVisualizer::setParameter(int, float) {}
void CoherenceVisualizer::setParameter(int, int, int, float) {}
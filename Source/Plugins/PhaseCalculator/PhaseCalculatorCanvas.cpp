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

#include "PhaseCalculatorCanvas.h"
#include <cmath>    // std::atan, std::fmod, std::floor
#include <iterator> // std::move_iterator

PhaseCalculatorCanvas::PhaseCalculatorCanvas(PhaseCalculator* pc)
    : processor     (pc)
    , viewport      (new Viewport())
    , canvas        (new Component())
    , rosePlot      (new RosePlot())
{
    refreshRate = 5;

    juce::Rectangle<int> bounds;
    
    rosePlot->setBounds(bounds = { 0, 0, 600, 500 });
    canvas->addAndMakeVisible(rosePlot);

    canvas->setBounds(bounds);

    viewport->setViewedComponent(canvas, false);
    viewport->setScrollBarsShown(true, true);
    addAndMakeVisible(viewport);
}

PhaseCalculatorCanvas::~PhaseCalculatorCanvas() {}

void PhaseCalculatorCanvas::refreshState() {}
void PhaseCalculatorCanvas::update() {}

void PhaseCalculatorCanvas::refresh() 
{
    // get new angles from visualization phase buffer
    ScopedPointer<ScopedLock> bufferLock;
    std::queue<double>& buffer = processor->getVisPhaseBuffer(bufferLock);

    while (!buffer.empty())
    {
        addAngle(buffer.front());
        buffer.pop();
    }
}

void PhaseCalculatorCanvas::beginAnimation() 
{
    // TODO: when have a variable for the event channel, don't start callbacks if it's disabled
    startCallbacks();
}

void PhaseCalculatorCanvas::endAnimation()
{
    // TODO: use event channel variable
    stopCallbacks();
}

void PhaseCalculatorCanvas::setParameter(int, float) {}
void PhaseCalculatorCanvas::setParameter(int, int, int, float) {}

void PhaseCalculatorCanvas::paint(Graphics& g)
{
    g.fillAll(Colours::grey);
}

void PhaseCalculatorCanvas::resized()
{
    viewport->setBounds(0, 0, getWidth(), getHeight());
}

void PhaseCalculatorCanvas::addAngle(double newAngle)
{
    rosePlot->addAngle(newAngle);
}

void PhaseCalculatorCanvas::clearAngles()
{
    rosePlot->clear();
}

/**** RosePlot ****/

RosePlot::RosePlot()
    : referenceAngle(0.0)
    , useReference  (true)
    , numBins       (24)
    , faceColor     (Colours::blanchedalmond)
    , edgeColor     (Colours::black)
    , bgColor       (Colours::black)
    , edgeWeight    (1)
{
    updateAngles();
    reorganizeAngleData();
}

RosePlot::~RosePlot() {}

void RosePlot::paint(Graphics& g)
{
    // dimensions
    juce::Rectangle<int> bounds = getBounds();
    int squareSide = jmin(bounds.getHeight(), bounds.getWidth());
    juce::Rectangle<float> plotBounds = bounds.withSizeKeepingCentre(squareSide, squareSide).toFloat();
    g.setColour(bgColor);
    g.fillEllipse(plotBounds);

    // get count for each rose plot segment
    int nSegs = binMidpoints.size();
    Array<int> segmentCounts;
    segmentCounts.resize(nSegs);
    int maxCount = 0;
    int totalCount = 0;
    for (int seg = 0; seg < nSegs; ++seg)
    {
        int count = angleData->count(binMidpoints[seg]);
        segmentCounts.set(seg, count);
        maxCount = jmax(maxCount, count);
        totalCount += count;
    }

    jassert(totalCount == angleData->size());
    jassert((maxCount == 0) == (angleData->empty()));

    // construct path
    Path rosePath;
    for (int seg = 0; seg < nSegs; ++seg)
    {
        if (segmentCounts[seg] == 0)
            continue;

        float size = squareSide * segmentCounts[seg] / static_cast<float>(maxCount);
        rosePath.addPieSegment(plotBounds.withSizeKeepingCentre(size, size),
            segmentAngles[seg].first, segmentAngles[seg].second, 0);
    }

    // paint path
    g.setColour(faceColor);
    g.fillPath(rosePath);
    g.setColour(edgeColor);
    g.strokePath(rosePath, PathStrokeType(edgeWeight));
}

void RosePlot::setNumBins(int newNumBins)
{
    if (newNumBins != numBins && newNumBins > 0 && newNumBins <= MAX_BINS)
    {
        numBins = newNumBins;
        updateAngles();
        reorganizeAngleData();
        repaint();
    }
}

void RosePlot::setReference(double newReference)
{
    if (newReference != referenceAngle)
    {
        referenceAngle = newReference;
        if (useReference)
        {
            reorganizeAngleData();
            repaint();
        }
    }
}

void RosePlot::setUseReference(bool newUseReference)
{
    if (newUseReference != useReference)
    {
        useReference = newUseReference;
        if (referenceAngle != 0.0)
        {
            reorganizeAngleData();
            repaint();
        }
    }
}

void RosePlot::addAngle(double newAngle)
{
    angleData->insert(circDist(newAngle, 0));
    repaint();
}

void RosePlot::clear()
{
    angleData->clear();
    repaint();
}

/*** RosePlot private members ***/

const double RosePlot::PI = 4 * std::atan(1.0);

RosePlot::circularBinComparator::circularBinComparator(int numBinsIn, double referenceAngleIn)
    : numBins           (numBinsIn)
    , referenceAngle    (referenceAngleIn)
{}

bool RosePlot::circularBinComparator::operator() (const double& lhs, const double& rhs) const
{
    int lhsBin = static_cast<int>(std::floor(circDist(lhs, referenceAngle) * numBins / (2 * PI)));
    int rhsBin = static_cast<int>(std::floor(circDist(rhs, referenceAngle) * numBins / (2 * PI)));
    return lhsBin < rhsBin;
}

RosePlot::AngleDataMultiset::AngleDataMultiset(int numBins, double referenceAngle)
    : std::multiset<double, circularBinComparator>(circularBinComparator(numBins, referenceAngle))
{}

RosePlot::AngleDataMultiset::AngleDataMultiset(int numBins, double referenceAngle,
    AngleDataMultiset* dataSource)
    : std::multiset<double, circularBinComparator>(dataSource->begin(), dataSource->end(),
    circularBinComparator(numBins, referenceAngle))
{}

void RosePlot::reorganizeAngleData()
{
    ScopedPointer<AngleDataMultiset> newAngleData;
    double realReferenceAngle = useReference ? referenceAngle : 0.0;
    if (angleData == nullptr)
    {
        // construct empty container
        newAngleData = new AngleDataMultiset(numBins, realReferenceAngle);
    }
    else
    {
        // copy existing data to new container
        newAngleData = new AngleDataMultiset(numBins, realReferenceAngle, angleData.get());
    }

    angleData.swapWith(newAngleData);
}

double RosePlot::circDist(double x, double ref)
{
    double xMod = std::fmod(x - ref, 2 * PI);
    return (xMod < 0 ? xMod + 2 * PI : xMod);
}

void RosePlot::updateAngles()
{
    double step = 2 * PI / numBins;
    binMidpoints.resize(numBins);
    for (int i = 0; i < numBins; ++i)
    {
        binMidpoints.set(i, step * (i + 0.5));
        float firstAngle = circDist(PI / 2, step * (i + 1));
        segmentAngles.set(i, { firstAngle, firstAngle + step });
    }
}
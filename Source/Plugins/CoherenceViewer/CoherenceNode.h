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

#ifndef COHERENCE_NODE_H_INCLUDED
#define COHERENCE_NODE_H_INCLUDED

/*

Coherence Node - continuously compute and display magnitude-squared coherence
(measure of phase synchrony) between pairs of LFP signals for a set of frequencies
of interest. Displays either raw coherence values or change from a saved baseline,
in units of z-score. 

*/

#include <ProcessorHeaders.h>
#include <VisualizerEditorHeaders.h>

#include "CoherenceVisualizer.h"
#include "AtomicSynchronizer.h"
#include "CumulativeTFR.h"

#include <vector>

class CoherenceNode : public GenericProcessor, public Thread
{
public:
    CoherenceNode();

    AudioProcessorEditor* createEditor() override;

    void process(AudioSampleBuffer& continuousBuffer) override;

    // thread function - coherence calculation
    void run() override;

private:
    AtomicSynchronizer dataSync;        // writer = process function , reader = thread
    AtomicSynchronizer coherenceSync;   // writer = thread, reader = visualizer (message thread)

    // group of 3, controlled by coherenceSync:
    std::vector<std::vector<double>> meanCoherence;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoherenceNode);
};


class CoherenceEditor : public VisualizerEditor
{
public:
    CoherenceEditor(CoherenceNode* n);

    Visualizer* createNewCanvas() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoherenceEditor);
};

#endif // COHERENCE_NODE_H_INCLUDED
/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2018 Translational NeuroEngineering Lab

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

#ifndef H_TRANSFORMERS_H_INCLUDED
#define H_TRANSFORMERS_H_INCLUDED

#include "../../../JuceLibraryCode/JuceHeader.h"

/*

*/

enum Band
{
    HIGH_GAM = 0,
    MID_GAM,
    LOW_GAM,
    BETA,
    ALPHA_THETA,
    DELTA,
    NUM_BANDS
};

namespace Hilbert
{
    const int FS = 500;

    const String BAND_NAME[NUM_BANDS];

    // each is a pair (lower limit, upper limit)
    const double* const VALID_BAND[NUM_BANDS];

    // samples of group delay (= order of filter / 2)
    const int DELAY[NUM_BANDS];
    
    // contain the first delay[band] coefficients; the rest are redundant and can be inferred
    const double* const TRANSFORMER[NUM_BANDS];
}

#endif // H_TRANSFORMERS_H_INCLUDED
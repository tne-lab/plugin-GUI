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

#ifndef CUMULATIVE_TFR_H_INCLUDED
#define CUMULATIVE_TFR_H_INCLUDED

#include <FFTWWrapper.h>

#include "CircularArray.h"

#include <vector>
#include <complex>

class CumulativeTFR
{
    // shorten some things
    template<typename T>
    using vector = std::vector<T>;

    using RealAccum = StatisticsAccumulator<double>;
    using ComplexAccum = StatisticsAccumulator<std::complex<double>>;

public:
    CumulativeTFR(int ng1, int ng2, int nf, int nt,
        int Fs,
        double fftSec = 10.0);

    // Handle a new buffer of data. Preform FFT and create pxxs, pyys.
    void addTrial(AudioBuffer<float> dataBuffer, int chan, int region);

    // Functions to get coherence data
    vector<vector<double>> getCurrentMeanCoherence();
    vector<vector<double>> getCurrentStdCoherence();

private:
    void CumulativeTFR::generateWavelet(int nfft, int nFreqs, int segLen);
    // calc pxys
	double CumulativeTFR::calcCrssspctrm();
    int nGroup1Chans;
    int nGroup2Chans;
    int nFreqs;
    int nTimes;
    
    // Keeps track of which buffer we are setting
    double curTime;

    // Time of interest
    Array<int> toi;
    // Freq of interest
    Array<int> foi;

    Array<FFTWArray> tempBuffer;

    const int nfft;

    FFTWArray convInput;
    FFTWArray freqData;
    FFTWArray region2Data;
    FFTWArray convOutput;

    FFTWPlan fftPlan;
    FFTWPlan ifftPlan;

    // # group 1 channels x # frequencies x # times
    vector<vector<vector<RealAccum>>> pxxs;
    vector<vector<vector<RealAccum>>> pyys;

    // # channel combinations x # frequencies x # times
    vector<vector<vector<ComplexAccum>>> pxys;

    // statistics for all trials, combined over trial times
    // # channel combinations x # frequencies
    vector<vector<double>> meanCoherence;
    vector<vector<double>> stdCoherence;

    // update meanCoherence and stdCoherence from pxxs, pyys, and pxys
    void updateCoherenceStats();

    // calculate a single magnitude-squared coherence from cross spectrum and auto-power values
    static double singleCoherence(double pxx, double pyy, std::complex<double> pxy);


    // Create wavelet
    // complex sinusoid
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CumulativeTFR);
};

#endif // CUMULATIVE_TFR_H_INCLUDED
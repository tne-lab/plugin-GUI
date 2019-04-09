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

#include "CumulativeTFR.h"
#include <cmath>

CumulativeTFR::CumulativeTFR(int ng1, int ng2, int nf, int nt, int Fs, double fftSec)
    : nGroup1Chans  (ng1)
    , nGroup2Chans  (ng2)
    , nFreqs        (nf)
    , nTimes        (nt)
    , nfft          (int(fftSec * Fs))
    , convInput     (nfft)
    , freqData      (nfft)
    , convOutput    (nfft)
    , fftPlan       (nfft, &convInput, &freqData, FFTW_FORWARD, FFTW_MEASURE)
    , ifftPlan      (nfft, &freqData, &convOutput, FFTW_BACKWARD, FFTW_MEASURE)
    , pxxs          (ng1,
                    vector<vector<RealAccum>>(nf,
                    vector<RealAccum>(nt)))
    , pyys          (ng2,
                    vector<vector<RealAccum>>(nf,
                    vector<RealAccum>(nt)))
    , pxys          (ng1 * ng2,
                    vector<vector<ComplexAccum>>(nf,
                    vector<ComplexAccum>(nt)))
    , tempBuffer    (nGroup1Chans+nGroup2Chans,nfft) // how to an array of fftwarrays?
{}


void CumulativeTFR::addTrial(AudioBuffer<float> dataBuffer, int chan, int region)
{
    int region = region; // Either 1 or 2. 1 => pxx, 2 => pyy 
    const float* rpChan = dataBuffer.getReadPointer(chan);

    int segmentLen = 8;
    int windowLen = 2;
    float stepLen = 0.1;
    int interpRatio = 2;

    float winsPerSegment = (segmentLen - windowLen) / stepLen;

    //// Update convInput ////

    // copy dataBuffer to fft input
    int dataSize = dataBuffer.getNumSamples();
    convInput.copyFrom(rpChan, dataSize, 0); // Double to float...?

    //// Execute fft ////
    fftPlan.execute();

    tempBuffer.insert(chan, freqData);

    //// Use freqData to find pxx or pyy ////
    int channel = chan;

    // Get FFT values at frequencies/times of interest
    for (int time_it; time_it < nTimes; time_it++)
    {
        float time = toi[time_it];
        for (int freq_it = 0; freq_it < nFreqs; freq_it++)
        {
            float freq = foi[freq_it];
            std::complex<double> * cp = freqData.getComplexPointer(freq);
            // Get power. Is this one timeframe? Whats the window for?
                        
        }
    }
    
    /// Notes I remember from talking, not sure about this.
    // fft over wavelet?
    // Multiply the two fft datas?
    // Use this to find power
    // Update the vector
}

std::vector<std::vector<double>> CumulativeTFR::getCurrentMeanCoherence()
{
    // Calculate pxys
    calcCrssspctrm();

    // Update coherence (make sure stdCoherence hasn't already calc coherence)
    if (meanCoherence.size() != curTime)
    {
        updateCoherenceStats();
    }

    return meanCoherence;
}


// > Private Methods


void CumulativeTFR::updateCoherenceStats()
{
    for (int c1 = 0, comb = 0; c1 < nGroup1Chans; ++c1)
    {
        for (int c2 = 0; c2 < nGroup2Chans; ++c2, ++comb)
        {
            double* meanDest = meanCoherence[comb].data();
            double* stdDest = stdCoherence[comb].data();

            for (int f = 0; f < nFreqs; ++f)
            {
                // compute coherence at each time
                RealAccum coh;

                for (int t = 0; t < nTimes; ++t)
                {
                    coh.addValue(singleCoherence(
                        pxxs[c1][f][t].getAverage(),
                        pyys[c2][f][t].getAverage(),
                        pxys[comb][f][t].getAverage()));
                }

                meanDest[f] = coh.getAverage();
                if (nTimes < 2)
                {
                    stdDest[f] = 0;
                }
                else
                {
                    stdDest[f] = std::sqrt(coh.getVariance() * nTimes / (nTimes - 1));
                }
            }
        }
    }
}


double CumulativeTFR::singleCoherence(double pxx, double pyy, std::complex<double> pxy)
{
    return std::norm(pxy) / (pxx * pyy);
}


double CumulativeTFR::calcCrssspctrm()
{
    // fourier transform of https://dsp.stackexchange.com/questions/736/how-do-i-implement-cross-correlation-to-prove-two-audio-files-are-similar

    int comb = 0; // loop over combinations
    for (int freq; freq < nFreqs; freq++)
    {
        for (int chanX = 0; chanX < nGroup1Chans; chanX++)
        {
            for (int chanY = 0; chanY < nGroup2Chans; chanY++)
            {
                // Get complex fft output for both chanX and chanY
                std::complex<double> complexDataX = tempBuffer[chanX].getAsComplex(freq); 
                FFTWArray * bufferY = &tempBuffer[chanY];
                ifftPlan.execute();
                //ifft(y) * conj(x) = cross-correlation
                pxys.at(comb).at(freq).at(curTime).addValue(convOutput.getAsComplex(freq)*std::conj(complexDataX));
                comb++;
                
            }
        }
    }
}



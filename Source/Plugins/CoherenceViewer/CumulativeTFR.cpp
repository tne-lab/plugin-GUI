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

CumulativeTFR::CumulativeTFR(int ng1, int ng2, int nf, int nt, int Fs, int winLen, float stepLen, float freqStep,
    int freqStart, double fftSec, double alpha)
    : nFreqs        (nf)
    , Fs            (Fs)
    , stepLen       (stepLen)
    , nTimes        (nt)
    , nfft          (int(fftSec * Fs))
    , fftInput      (nfft)
    , fftOutput     (nfft)
    , ifftInput     (nfft)
    , ifftOutput    (nfft)
    , fftPlan       (nfft, &fftInput, &fftOutput, FFTW_FORWARD, FFTW_ESTIMATE)
    , ifftPlan      (nfft, &ifftInput, &ifftOutput, FFTW_BACKWARD, FFTW_ESTIMATE)
    , alpha         (alpha)
    , pxys          (ng1 * ng2,
                    vector<vector<ComplexWeightedAccum>>(nf,
                    vector<ComplexWeightedAccum>(nt, ComplexWeightedAccum(alpha))))
    , windowLen     (winLen)
    , waveletArray  (nf, vector<std::complex<double>>(nfft))
    , spectrumBuffer(ng1 + ng2, 
                     vector<vector<const std::complex<double>>>(nf,
                     vector<const std::complex<double>>(nt)))
    , powBuffer     (ng1 + ng2,
                    vector<vector<RealWeightedAccum>>(nf,
                    vector<RealWeightedAccum>(nt, RealWeightedAccum(alpha))))
    , freqStep      (freqStep)
    , freqStart     (freqStart)
{
    // Create array of wavelets
    generateWavelet();

    // Trim time close to edge
    trimTime = windowLen / 2;
}

void CumulativeTFR::addTrial(const double* fftIn, int chanIt)
{
    float winsPerSegment = (segmentLen - windowLen) / stepLen;
    
    //// Update convInput ////
    // copy dataBuffer input to fft input
    fftInput.copyFrom(fftIn, nfft);

    //// Execute fft ////
    fftPlan.execute();
    float nWindow = Fs * windowLen;
    //// Use freqData to find generate spectrum and get power ////
	for (int freq = 0; freq < nFreqs; freq++)
	{
		// Multiple fft data by wavelet
		for (int n = 0; n < nfft; n++)
		{
            ifftInput.set(n, fftOutput.getAsComplex(n) * waveletArray[freq][n] ); // Divide by 2 so we don't go over double limits later..
		}
		// Inverse FFT on data multiplied by wavelet
		ifftPlan.execute();
        
        // Loop over time of interest
		for (int t = 0; t < nTimes; t++)
		{
            int tIndex = int(((t * stepLen) + trimTime)  * Fs); // get index of time of interest
            std::complex<double> complex = ifftOutput.getAsComplex(tIndex);
            complex *= sqrt(2.0 / nWindow) / double(nfft); // divide by nfft from matlab ifft
                                                           // sqrt(2/nWindow) from ft_specest_mtmconvol.m 
            // Save convOutput for crss later
            spectrumBuffer[chanIt][freq][t] = complex;
            // Get power
            double power = std::norm(complex);
            
            powBuffer[chanIt][freq][t].addValue(power);
		}
	}
}

void CumulativeTFR::getMeanCoherence(int itX, int itY, double* meanDest, int comb)
{
    // Cross spectra
    for (int f = 0; f < nFreqs; ++f)
    {
        // Get crss from specturm of both chanX and chanY
        for (int t = 0; t < nTimes; t++)
        {
            std::complex<double> crss = spectrumBuffer[itX][f][t] * std::conj(spectrumBuffer[itY][f][t]);
            pxys[comb][f][t].addValue(crss);
        }
    }
    
    // Coherence
    std::vector<double> stdDest(nFreqs); // Not used yet.. Probably add it as input to function
    for (int f = 0; f < nFreqs; ++f)
    {
        // compute coherence at each time
        RealAccum coh;

        for (int t = 0; t < nTimes; t++)
        {
            coh.addValue(singleCoherence(
                powBuffer[itX][f][t].getAverage(),
                powBuffer[itY][f][t].getAverage(),
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

    return;
}


// > Private Methods

double CumulativeTFR::singleCoherence(double pxx, double pyy, std::complex<double> pxy)
{
    return std::norm(pxy) / (pxx * pyy);
}


void CumulativeTFR::generateWavelet() 
{
    std::vector<double> hann(nfft);
    std::vector<double> sinWave(nfft);
	std::vector<double> cosWave(nfft);
    
	waveletArray.resize(nFreqs);

    // Hann window

    float nSampWindow = Fs * windowLen;
    for (int position = 0; position < nfft; position++)
    {
        //// Hann Window //// = sin^2(PI*n/N) where N=length of window
        // Create first half hann function
        if (position <= nSampWindow / 2)
        {
            // Shifted by one half cycle (pi/2)
            hann[position] = square(std::sin(double_Pi*position / nSampWindow + (double_Pi/2.0))); 
        }
        // Pad with zeroes
        else if (position <= (nfft - nSampWindow / 2)) 
        {
            hann[position] = 0;
        }
        // Finish off hann function
        else
        {
            // Move start of wave to nfft - windowSize/2
            int hannPosition = position - (nfft - nSampWindow / 2); 
            hann[position] = square(std::sin(hannPosition*double_Pi / nSampWindow));
        }
    }

    // Wavelet
    float freqNormalized = freqStart;
    for (int freq = 0; freq < nFreqs; freq++)
    {
        freqNormalized += freqStep;
        
        for (int position = 0; position < nfft; position++)
        {
            // Make sin and cos wave.
            sinWave[position] = std::sin(position * freqNormalized * (2*double_Pi) / Fs);
            cosWave[position] = std::cos(position * freqNormalized * (2*double_Pi) / Fs);
        }

		//// Wavelet ////
		// Put into fft input array
		for (int position = 0; position < nfft; position++)
		{
            fftInput.set(position, std::complex<double>(cosWave.at(position) * hann.at(position), sinWave.at(position) * hann.at(position)));
        }
		
		fftPlan.execute();

		// Save fft output for use later
        for (int i = 0; i < nfft; i++)
        {
            waveletArray[freq][i] = fftOutput.getAsComplex(i);
        }
    }	
}



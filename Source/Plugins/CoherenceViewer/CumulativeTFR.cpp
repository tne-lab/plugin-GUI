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
    int freqStart, float interpRatio, double fftSec, double alpha)
    : nGroup1Chans  (ng1)
    , nGroup2Chans  (ng2)
    , nFreqs        (nf)
    , Fs            (Fs)
    , stepLen       (stepLen)
    , nTimes        (nt)
    , nfft          (int(fftSec * Fs))
    , fftArray      (nfft)
    , ifftArray     (nfft)
    , fftPlan       (nfft, &fftArray, FFTW_FORWARD, FFTW_ESTIMATE)
    , ifftPlan      (nfft, &ifftArray, FFTW_BACKWARD, FFTW_ESTIMATE)
    , alpha         (alpha)
    , pxys          (ng1 * ng2,
                    vector<vector<ComplexWeightedAccum>>(nf,
                    vector<ComplexWeightedAccum>(nt, ComplexWeightedAccum(alpha))))
    , windowLen     (winLen)
    , interpRatio   (interpRatio)
    , waveletArray  (nf, vector<std::complex<double>>(nfft))
    , spectrumBuffer(nGroup1Chans + nGroup2Chans, 
                     vector<vector<const std::complex<double>>>(nf,
                     vector<const std::complex<double>>(nt)))
    , powBuffer     (nGroup1Chans + nGroup2Chans,
                    vector<vector<RealWeightedAccum>>(nf,
                    vector<RealWeightedAccum>(nt, RealWeightedAccum(alpha))))
    , meanCoherence (ng1 * ng2, vector<double>(nFreqs))
    , stdCoherence  (ng1 * ng2, vector<double>(nFreqs))
    , freqStep      (freqStep)
    , freqStart     (freqStart)
{
    // Create array of wavelets
    generateWavelet();

    // Trim time close to edge
    trimTime = windowLen / 2;
}

void CumulativeTFR::addTrial(const double* fftIn, int chan)
{
    float winsPerSegment = (segmentLen - windowLen) / stepLen;
    
    //// Update convInput ////
    // copy dataBuffer input to fft input
    fftArray.copyFrom(fftIn, nfft);

    //// Execute fft ////
    fftPlan.execute();
    float nWindow = Fs * windowLen;
    //// Use freqData to find generate spectrum and get power ////
	for (int freq = 0; freq < nFreqs; freq++)
	{
		// Multiple fft data by wavelet
		for (int n = 0; n < nfft; n++)
		{
            ifftArray.set(n, fftArray.getAsComplex(n) * waveletArray[freq][n]);
		}
		// Inverse FFT on data multiplied by wavelet
		ifftPlan.execute();
        
        // Loop over time of interest
		for (int t = 0; t < nTimes; t++)
		{
            int tIndex = int(((t * stepLen) + trimTime)  * Fs); // get index of time of interest
            std::complex<double> complex = ifftArray.getAsComplex(tIndex);
            complex *= sqrt(2.0 / nWindow) / double(nfft); // divide by nfft from matlab ifft
                                                           // sqrt(2/nWindow) from ft_specest_mtmconvol.m 
            // Save convOutput for crss later
            spectrumBuffer[chan][freq][t] = complex;
            // Get power
            double power = std::norm(complex);
            powBuffer[chan][freq][t].addValue(power);
		}
	}
}

void CumulativeTFR::getMeanCoherence(int chanX, int chanY, double* meanDest, int comb)
{
    // Cross spectra
    for (int f = 0; f < nFreqs; ++f)
    {
        // Get crss from specturm of both chanX and chanY
        for (int t = 0; t < nTimes; t++)
        {
            std::complex<double> crss = spectrumBuffer[chanX][f][t] * std::conj(spectrumBuffer[chanY][f][t]);
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
                powBuffer[chanX][f][t].getAverage(),
                powBuffer[chanY][f][t].getAverage(),
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


void CumulativeTFR::updateCoherenceStats() // Not Updated!!
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

                for (int t = 0; t < nTimes; t++)
                {
                    coh.addValue(singleCoherence(
                        powBuffer[c1][f][t].getAverage(),
                        powBuffer[c2][f][t].getAverage(),
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


void CumulativeTFR::calcCrssspctrm()
{
    /*
	std::complex<double> crss;

    for (int freq = 0; freq < nFreqs; freq++)
    {
        for (int chanX = 0, comb = 0; chanX < nGroup1Chans; chanX++)
        {
            for (int chanY = 0; chanY < nGroup2Chans; chanY++, comb++)
            {
                // Get crss from specturm of both chanX and chanY
				for (int t = 0; t < nTimes; t++)
				{
					crss = spectrumBuffer[chanX][t] * conj(spectrumBuffer[chanY][t]);
                    pxySum += crss;
                    pxyCount++;
                    pxys.at(comb).at(freq).at(t) = std::complex<double>(pxySum.real() / pxyCount, pxySum.imag()/pxyCount);
				}            
            }
        }
    }
	// Update coherence now that crss is found
	updateCoherenceStats();
    */
}


void CumulativeTFR::generateWavelet() 
{
    std::vector<double> hann(nfft);
    std::vector<double> sinWave(nfft);
	std::vector<double> cosWave(nfft);
    
	waveletArray.resize(nFreqs);

    // Hann window
    hannNorm = 0;
    float nSampWindow = Fs * windowLen;
    for (int position = 0; position < nfft; position++)
    {
        //// Hann Window //// = sin^2(PI*n/N) where N=length of window
        // Create first half hann function
        if (position <= nSampWindow / 2)
        {
            hann[position] = square(std::sin(position*double_Pi / nSampWindow + double_Pi / 2.0)); // Shift half over cos^2(pi*x*freq/(n+1))
        }
        // Pad with zeroes
        else if (position <= (nfft - nSampWindow / 2)) // 0's until one half cycle left
        {
            hann[position] = 0;
        }
        // Finish off hann function
        else
        {
            //std::cout << "in third chunk of hann" << std::endl;
            int hannPosition = position - (nfft - nSampWindow / 2); // Move start of wave to nfft - windowSize/2
            hann[position] = square(std::sin(hannPosition*double_Pi / nSampWindow));
            //std::cout << "hann coords: " << hann[position] << std::endl;
        }
        // Normalize Hann window using Frobenius-ish alg. hann = hann/norm(hann)
        // norm(hann) = sum(abs(hann).^P)^(1/P) ... use p=2
        //hannNorm += pow(abs(hann[position]), 2); Does nothing currently?
    }
    /* // Checked and doesn't change the data (think it was used to make sure the matrix was equal in matlab code?)
    hannNorm = pow(hannNorm, 1 / 2);
    for (int position = 0; position < nfft; position++)
    {
        hann[position] /= hannNorm;
    }
    */

    // Wavelet
    float freqNormalized = freqStart;
    for (int freq = 0; freq < nFreqs; freq++)
    {
        freqNormalized += freqStep;
        
        for (int position = 0; position < nfft; position++)
        {
            // Make sin and cos wave. Also noramlize hann here.
            sinWave[position] = std::sin(position * freqNormalized * (2 * double_Pi) / Fs); // Shift by pi/2 to put peak at time 0
            cosWave[position] = std::cos(position * freqNormalized * (2 * double_Pi) / Fs);
        }

		//// Wavelet ////
		// Put into fft input array
		for (int position = 0; position < nfft; position++)
		{
			fftArray.set(position, std::complex<double>(cosWave.at(position) * hann.at(position), sinWave.at(position) * hann.at(position)));
        }
		
		fftPlan.execute();

		// Save fft output for use later
        for (int i = 0; i < nfft; i++)
        {
            waveletArray[freq][i] = fftArray.getAsComplex(i);
        }
    }	
}



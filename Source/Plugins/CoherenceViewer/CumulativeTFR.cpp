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

CumulativeTFR::CumulativeTFR(int ng1, int ng2, int nf, int nt, int Fs, Array<float> foi, int segLen, int winLen, int stepLen, float interpRatio, double fftSec)
    : nGroup1Chans  (ng1)
    , nGroup2Chans  (ng2)
    , nFreqs        (nf)
    , Fs            (Fs)
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
                    vector<vector<std::complex<double>>>(nf,
                    vector<std::complex<double>>(nt)))
    , segmentLen    (segLen)
    , windowLen     (winLen)
    , stepLen       (stepLen)
    , interpRatio   (interpRatio)
    , foi           (foi)
{
    generateWavelet();

    // Trim time close to edge
    int nSamplesWin = windowLen * Fs;
    trimTime = nSamplesWin / 2;
}

void CumulativeTFR::addTrial(const float* fftIn, int chan, int region)
{
    float winsPerSegment = (segmentLen - windowLen) / stepLen;
    
    //// Update convInput ////
    // copy dataBuffer input to fft input
    convInput.copyFrom(fftIn, nfft);

    //// Execute fft ////
    fftPlan.execute();

    //// Use freqData to find generate spectrum and get power ////
	for (int freq = 1; freq < nFreqs; freq++)
	{
		// Multiple fft data by wavelet
		for (int n = 0; n < nfft; n++)
		{
			freqData.set(n, freqData.getAsComplex(n) * waveletArray.getUnchecked(freq)[n]);
		}
		// Inverse FFT on data multiplied by wavelet
		ifftPlan.execute();

        // Loop over time of interest
		for (float t = trimTime; t < nfft - trimTime; t+=stepLen)
		{
            std::complex<double> complex = convOutput.getAsComplex(t);
            // Save convOutput for crss later
            spectrumBuffer.getReference(chan)[t] = complex;
            // Get power
			double power = pow(abs(complex),2); 
			// Region either 1 or 2. 1 => pxx, 2 => pyy
			if (region == 1)
			{
				pxxs.at(chan).at(freq).at(t).addValue(power);
			}
			else
			{
				pyys.at(chan).at(freq).at(t).addValue(power);
			}
		}
	}
}

std::vector<std::vector<double>> CumulativeTFR::getCurrentMeanCoherence()
{
    calcCrssspctrm();
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

                for (int t = trimTime; t < nfft - trimTime; t+=stepLen)
                {
                    coh.addValue(singleCoherence(
                        pxxs[c1][f][t].getAverage(),
                        pyys[c2][f][t].getAverage(),
                        pxys[comb][f][t]));
                }

                meanDest[f] = coh.getAverage();
                int nTimes = nfft - (2 * trimTime); // number of samples minus trimming on both sides
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
	std::complex<double> crss;

    for (int freq; freq < nFreqs; freq++)
    {
        for (int chanX = 0, comb = 0; chanX < nGroup1Chans; chanX++)
        {
            for (int chanY = 0; chanY < nGroup2Chans; chanY++, comb++)
            {
                // Get crss from specturm of both chanX and chanY
				for (int t = trimTime; t < nfft - trimTime; t += stepLen) // Time of interest here instead of every point
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
}


void CumulativeTFR::generateWavelet() 
{
    std::vector<double> hann(nfft);
    std::vector<double> sinWave(nfft);
	std::vector<double> cosWave(nfft);
    
	waveletArray.resize(nFreqs);
	const double PI = 3.14;
    // Hann window
    FFTWArray waveletIn(nfft);
    for (int position = 0; position < nfft; position++)
    {
        //// Hann Window //// = sin^2(PI*n/N) where N=length of window
        // Create first half hann function
        if (position <= windowLen / 2)
        {
            hann[position] = pow(sin(position*PI / windowLen + PI / 2), 2); // Shift half over cos^2(pi*x*freq/(n+1))
        }
        // Pad with zeroes
        else if (position <= (nfft - windowLen / 2)) // 0's until one half cycle left
        {
            hann[position] = 0;
        }
        // Finish off hann function
        else
        {
            int hannPosition = position - (nfft - windowLen / 2); // Move start of wave to nfft - windowSize/2
            hann[position] = pow(sin((hannPosition*PI / windowLen)), 2);
        }
        // Normalize Hann window using Frobenius-ish alg. hann = hann/norm(hann)
        // norm(hann) = sum(abs(hann).^P)^(1/P) ... use p=2
        hannNorm += pow(abs(hann[position]), 2);
    }
    
    hannNorm = pow(hannNorm, 1 / 2);

    // Wavelet
    for (int freq = 1; freq < nFreqs; freq++)
    {
        for (int position = 0; position < nfft; position++)
        {
            // Make sin and cos wave. Also noramlize hann here.
            sinWave[position] = sin(position * freq * (2 * PI)); // Shift by pi/2 to put peak at time 0
            cosWave[position] = cos(position * freq * (2 * PI));
            hann[position] /= hannNorm;
        }

		//// Wavelet ////
		// Put into fft input array
		for (int position = 0; position < nfft; position++)
		{
			convInput.set(position, std::complex<double>(cosWave.at(position) * hann.at(position), sinWave.at(position) * hann.at(position)));
		}
		
		fftPlan.execute();
		// Save fft output for use later
        for (int i = 0; i < nfft; i++)
        {
            waveletArray[freq][i] = freqData.getAsComplex(i);
        }
    }	
}



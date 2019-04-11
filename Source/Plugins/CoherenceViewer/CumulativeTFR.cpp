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
    , tempBuffer    (nGroup1Chans+nGroup2Chans,nfft) // how to init an array of fftwarrays?
{}

void CumulativeTFR::addTrial(AudioBuffer<float> dataBuffer, int chan, int region)
{
    int region = region; // Either 1 or 2. 1 => pxx, 2 => pyy 
    const float* rpChan = dataBuffer.getReadPointer(chan);
	int channel = chan;

    int segmentLen = 8;
    int windowLen = 2;
    float stepLen = 0.1;
    int interpRatio = 2;

    float winsPerSegment = (segmentLen - windowLen) / stepLen;

    //// Update convInput ////

    // copy dataBuffer to fft input
    int dataSize = dataBuffer.getNumSamples();
    for (int i = 0; i < dataSize; i++)
    {
        convInput.set(i, rpChan[i]);
    }

    //// Execute fft ////
    fftPlan.execute();

    //// Use freqData to find generate spectrum and get power ////
	for (int freq = 1; freq < nFreqs; freq++)
	{
		// Multiple fft data by wavelet
		for (int n = 0; n < nfft; n++)
		{
			freqData.set(n, freqData.getAsComplex(n) * waveletArray.getUnchecked(freq).getAsComplex(n));
		}
		// Inverse FFT on data multiplied by wavelet
		ifftPlan.execute();

		// Loop over time of interest
		for (int time = 0; time < nTimes; time += stepLen)
		{
			//double pow = convOutput.getPower(time);
			if (group1Array.contains(channel))
			{
				pxxs.at(channel).at(freq).at(time).addValue(pow);
			}
			else
			{
				pyys.at(channel).at(freq).at(time).addValue(pow);
			}
		}
	}
	

	/// Set spectrum and power
	//spectrumChan = ifft(fft(chan) * waveletSpectrum)
	//spectrumBuffer.set(channel, spectrumChan);
	//
    
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

	std::complex<double> crss;

    int comb = 0; // loop over combinations at each freq
    for (int freq; freq < nFreqs; freq++)
    {
        for (int chanX = 0; chanX < nGroup1Chans; chanX++)
        {
            for (int chanY = 0; chanY < nGroup2Chans; chanY++)
            {
                // Get crss from specturm of both chanX and chanY
				for (int i = 0; i < nfft; i++) // Time of interest here instead of every point
				{
					crss = spectrumBuffer[chanX].getAsComplex(i) * conj(spectrumBuffer[chanY].getAsComplex(i));
					pxys.at(comb).at(freq).at(curTime).addValue(crss);
				}
                comb++;              
            }
        }
    }
}


void CumulativeTFR::generateWavelet(int nfft, int nFreqs, int segLen) 
{
    std::vector<double> hann(nfft);
    std::vector<std::complex<double>> sine(nfft);

    
	waveletArray.resize(nFreqs);
    int position = 0;
    int maxPosition = nfft;
    for (int freq = 1; freq < nFreqs; freq++)
    {
		FFTWArray waveletIn(nfft);
        for (int position = 0; position < maxPosition; position++)
        {
            //// Hann Window ////
            // Create first half hann function
            if (sin(position) >= 0)
            {
                hann.assign(position, pow(cos((position*freq + M_PI)),2)); // Shift half over cos^2(pi*x*freq/(n+1))
            }
            // Pad with zeroes
            else if (position <= (segLen - (M_PI_2 / freq))) // 0's until one half cycle left
            {
                hann.assign(position, -1);
            }
			// Finish off hann function
            else
            {
				int hannPos = (position - int(segLen - (M_PI_2 / freq))); // Shift hann to be at position 0 with one half cycle left
				hann.assign(position, pow(cos((hannPos*freq)), 2)); // check this math
            }

            //// Sine wave //// Does this need to be complex?
            sine.assign(position, sin(position * freq * (M_PI * 2) - M_PI_2)); // Shift by pi/2 to put peak at time 0

            
        } 
		// Normalize Hann window Frobenius 

		//// Wavelet ////
		// for loop here
		waveletIn.set(position, sine.at(position) * hann.at(position));
		// Move into fft array
		for (int i = 0; i < nfft; i++)
		{
			convInput.set(i, waveletIn.getAsComplex(i));
		}
		fftPlan.execute();
		// Save fft output for use later
		waveletArray.set(freq, freqData);
    }

	
}



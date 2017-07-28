/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2014 Open Ephys

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

#ifndef PHASE_CALCULATOR_H_INCLUDED
#define PHASE_CALCULATOR_H_INCLUDED

/*

The Phase Calculator generates an estimate of the current phase of its input signal in degrees,
using the Hilbert transform, and outputs this as a continuous stream. Works best when
given a bandpass-filtered signal.

@see GenericProcessor, PhaseCalculatorEditor

*/

// debugging options

/* MARK_BUFFERS: Triggers an event on event channel 1 that starts at the first sample of each
 * processing buffer of input channel 1 and ends halfway between the first and last samples.
 * Aids in determining how features in the output (e.g. glitches) interact with edges between buffers.
 */
//#define MARK_BUFFERS

#ifdef _WIN32
#include <Windows.h>
#endif

#include <ProcessorHeaders.h>
#include <cstring>       // memset
#include <numeric>       // inner_product
#include <DspLib\Dsp.h>     // Filtering
#include "FFTWWrapper.h" // Fourier transform
#include "burg.h"        // Autoregressive modeling

// log2(starting length of the processing buffer)
#define START_PLEN_POW 13
#define MIN_PLEN_POW 9
#define MAX_PLEN_POW 16

// starting portion of the processing buffer that is AR predicted
#define START_NUM_FUTURE (1 << (START_PLEN_POW - 3))

// starting "glitch limit" (how long of a segment is allowed to be unwrapped or smoothed, in samples)
#define START_GL 200

// how often to recalculate AR model, in ms (initial value)
#define START_AR_INTERVAL 50

// order of the AR model
#define AR_ORDER 20
// priority of the AR model calculating thread (0 = lowest, 10 = highest)
#define AR_PRIORITY 3

// filter parameters
#define START_LOW_CUT 4.0
#define START_HIGH_CUT 8.0

// parameter indices
enum 
{
    pNumFuture,
    pEnabledState,
    pRecalcInterval,
    pGlitchLimit,
    pAdcEnabled,
    pLowcut,
    pHighcut
};

/* each continuous channel has three possible states while acquisition is running:

    - NOT_FULL:     Not enough samples have arrived to fill the history fifo for this channel.
                    Wait for more  samples before calculating the autoregressive model parameters.

    - FULL_NO_AR:   The history fifo for this channel is now full, but AR parameters have not been calculated yet.
                    Tells the AR thread that it can start calculating the model and the main thread that it should still output zeros.

    - FULL_AR:      The history fifo is full and AR parameters have been calculated at least once.
                    In this state, the main thread uses the parameters to predict the future signal and output and calculate the phase.
*/
enum ChannelState {NOT_FULL, FULL_NO_AR, FULL_AR};

class PhaseCalculator : public GenericProcessor, public Thread
{
    friend class PhaseCalculatorEditor;
    friend class ProcessBufferSlider;
public:

    PhaseCalculator();
    ~PhaseCalculator();

    bool hasEditor() const override;

    AudioProcessorEditor* createEditor() override;

#ifdef MARK_BUFFERS
    void createEventChannels() override;
private:
    EventChannel* eventChannelPtr;
public:
#endif

    void setParameter(int parameterIndex, float newValue) override;

    void process(AudioSampleBuffer& buffer) override;

    bool enable() override;
    bool disable() override;

    // calculate the fraction of the processing length is AR-predicted data points (i.e. numFuture / processLength)
    float getRatioFuture();

    // thread code - recalculates AR parameters.
    void run() override;

    void saveCustomChannelParametersToXml(XmlElement* channelElement, int channelNumber, InfoObjectCommon::InfoObjectType channelType) override;
    void loadCustomChannelParametersFromXml(XmlElement* channelElement, InfoObjectCommon::InfoObjectType channelType) override;

    // handle changing number of channels
    void updateSettings() override;

private:

    // ---- methods ----

    // update processLength and numFuture, reallocating fields that depend on these.
    void setProcessLength(int newProcessLength, int newNumFuture);

    // update numFuture without reallocating processing arrays
    void setNumFuture(int newNumFuture);

    // Update the filters. From FilterNode code.
    void setFilterParameters();

    // do glitch unwrapping
    void unwrapBuffer(float* wp, int nSamples, int chan);

    // do start-of-buffer smoothing
    void smoothBuffer(float* wp, int nSamples, int chan);

    // ---- static utility methods ----

    /*
    * arPredict: use autoregressive model of order to predict future data.
    * Input params is an array of coefficients of an AR model of length AR_ORDER.
    * Writes writeNum future data values starting at location writeStart.
    * *** assumes there are at least AR_ORDER existing data points *before* writeStart
    * to use to calculate future data points.
    */
    static void arPredict(double* writeStart, int writeNum, const double* params);

    /*
    * hilbertManip: Hilbert transforms data in the frequency domain (including normalization by length of data).
    * Modifies fftData in place.
    */
    static void hilbertManip(FFTWArray<complex<double>>& fftData);

    // ---- customizable parameters ------
    
    // number of samples to process each round
    int processLength;

    // number of future values to predict (0 <= numFuture <= processLength - AR_ORDER)
    int numFuture;

    // size of sharedDataBuffer ( = processLength - numFuture)
    int bufferLength;
    
    // ADC/AUX processing (overriedes shouldProcessChannel)
    bool processADC;

    // enable / disable channels
    Array<bool> shouldProcessChannel;

    // time to wait between AR model recalculations in ms
    int calcInterval;

    // maximum number of samples to unwrap or smooth at the start of a buffer
    int glitchLimit;

    // ---- internals -------

    // Storage area for filtered data to be read by the thread to calculate AR model,
    // and also copied to enter the phase calculation pipeline
    AudioBuffer<double> sharedDataBuffer;

    // Everything else is an array with one entry per input.

    // Keep track of how much of the sharedDataBuffer is empty (per channel)
    Array<int> bufferFreeSpace;

    // Keeps track of each channel's state (see enum definition above)
    Array<ChannelState> chanState;

    // Plans for the FFTW Fourier Transform library
    OwnedArray<FFTWPlan> pForward;     // dataToProcess -> fftData
    OwnedArray<FFTWPlan> pBackward;    // fftData -> dataOut

    // Input/output vectors for FFTW processing
    OwnedArray<FFTWArray<double>>          dataToProcess;    // Input (real)
    OwnedArray<FFTWArray<complex<double>>> fftData;  // DFT of input (complex)
    OwnedArray<FFTWArray<complex<double>>> dataOut;  // Hilbert transform of input (complex)

    // mutexes for sharedDataBuffer arrays, which must be used in the side thread to calculate AR parameters.
    // since the side thread only READS sharedDataBuffer, only needs to be locked in the main thread when it's WRITTEN to.
    OwnedArray<CriticalSection> sdbLock;

    // temporary storage for AR parameter calculation (see burg.cpp)
    Array<double> per, pef, h, g;

    // keeps track of last output sample, to be used for smoothing.
    Array<float> lastSample;

    // AR parameters
    OwnedArray<Array<double>> arParams;

    // so that the warning message only gets sent once per run
    bool haveSentWarning;

    // ------ filtering --------
    double highCut;
    double lowCut;

    OwnedArray<Dsp::Filter> forwardFilters;
    OwnedArray<Dsp::Filter> backwardFilters;

    // -------static------------

    // so that fftw_cleanup doesn't choke
    static unsigned int numInstances;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseCalculator);
};

// timer class for use when calculating AR parameters
class ARTimer : public Timer
{
public:
    ARTimer();
    ~ARTimer();
    void timerCallback();
    // Returns whether hasRung is true and resets it to false.
    bool check();
private:
    // True if timer has reached 0 since last time it was checked.
    bool hasRung;
};

#endif // PHASE_CALCULATOR_H_INCLUDED
/*
------------------------------------------------------------------

This file is part of a library for the Open Ephys GUI
Copyright (C) 2017 Translational NeuroEngineering Laboratory, MGH

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

/*
Object-oriented / RAII-friendly wrapper for relevant parts of the FFTW Fourier
transform library
*/

#ifndef OEP_FFTW_H_INCLUDED
#define OEP_FFTW_H_INCLUDED

#include <CommonLibHeader.h>
#include <complex>

// forward-declare:
struct fftw_plan_s;

/*
FFTW-friendly array that can hold complex or real doubles.
*/
class COMMON_LIB FFTWArray
{
public:
    // creation / deletion

    FFTWArray(int complexLen = 0);

    FFTWArray(const FFTWArray& other);
    FFTWArray(FFTWArray&& other);

    ~FFTWArray();

    // assignment

    FFTWArray& operator=(const FFTWArray& other);
    FFTWArray& operator=(FFTWArray&& other);

    // access

    // gets the ith complex value 
    std::complex<double> getAsComplex(int i);

    // gets the ith real value, i.e. the (i % 2)th component of the (i / 2)th
    // complex value if the array stores complex data.
    double getAsReal(int i);

    std::complex<double>* getComplexPointer(int index = 0);
    double* getRealPointer(int index = 0);

    const double* getReadPointer(int index = 0) const;

    int getLength() const;

    // modification

    // returns true if a resize actually occurred
    virtual bool resize(int newLength);

    void set(int i, std::complex<double> val);
    void set(int i, double val);

    // Reverses first reverseLength complex values (default = all)
    void reverseComplex(int reverseLength = -1);

    // Reverses first reverseLength real values (default = all)
    void reverseReal(int reverseLength = -1);

    /* Copies up to num elements starting at fromArr to the array starting at startInd.
    * Returns the number of elements actually copied.
    */
    int copyFrom(const std::complex<double>* fromArr, int num, int startInd = 0);
    int copyFrom(const double* fromArr, int num, int startInd = 0);

    // Does the part of the Hilbert transform in the frequency domain (see FFTWTransformableArray::hilbert)
    void freqDomainHilbert();

private:
    std::complex<double>* data;
    int length;

    JUCE_LEAK_DETECTOR(FFTWArray);
};

class COMMON_LIB FFTWPlan
{
public:
    // r2c constructor
    FFTWPlan(int n, FFTWArray* in, FFTWArray* out, unsigned int flags);

    // r2c in-place
    FFTWPlan(int n, FFTWArray* buf, unsigned int flags);

    // c2c constructor
    FFTWPlan(int n, FFTWArray* in, FFTWArray* out, int sign, unsigned int flags);

    // c2c in-place
    FFTWPlan(int n, FFTWArray* buf, int sign, unsigned int flags);

    ~FFTWPlan();

    void execute();

    int getLength();

private:
    fftw_plan_s* plan;
    const int length;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFTWPlan);
};


// copyable, movable array that can do an in-place transform
class COMMON_LIB FFTWTransformableArray : public FFTWArray
{
public:
    FFTWTransformableArray(int n = 0, unsigned int flags = 0U /* FFTW_MEASURE */);

    FFTWTransformableArray(const FFTWTransformableArray& other);
    FFTWTransformableArray(FFTWTransformableArray&& other);

    FFTWTransformableArray& operator=(const FFTWTransformableArray& other);
    FFTWTransformableArray& operator=(FFTWTransformableArray&& other);

    bool resize(int newLength) override;

    void fftComplex();
    void fftReal();
    void ifft();

    // Do Hilbert transform of real data by taking the fft, zeroing out negative frequencies,
    // and taking the ifft. The result is not technically the Hilbert transform but the
    // analytic signal, defined as x + H[x]*i. This is consistent with Matlab's 'hilbert'.
    void hilbert();

private:
    unsigned int flags;
    ScopedPointer<FFTWPlan> forwardPlan;
    ScopedPointer<FFTWPlan> inversePlan;
    ScopedPointer<FFTWPlan> r2cPlan;

    JUCE_LEAK_DETECTOR(FFTWTransformableArray);
};


// version with different flags that's still default-constructible
// TODO think of a better name?
template<unsigned int f>
class FFTWTransformableArrayUsing : public FFTWTransformableArray
{
public:
    FFTWTransformableArrayUsing(int n = 0)
        : FFTWTransformableArray(n, f)
    {}
};

#endif // OEP_FFTW_H_INCLUDED
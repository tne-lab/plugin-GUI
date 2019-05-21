/*
------------------------------------------------------------------

This file is part of a library for the Open Ephys GUI
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

#include "OpenEphysFFTW.h"
#include <fftw3.h>

#include <algorithm>  // reverse array
#include <utility>

// FFTWArray

// creation / deletion

FFTWArray::FFTWArray(int complexLen)
{
    jassert(complexLen >= 0);
    length = complexLen;
    data = reinterpret_cast<std::complex<double>*>(fftw_alloc_complex(complexLen));
}

FFTWArray::FFTWArray(const FFTWArray& other)
    : FFTWArray(other.length)
{
    // delegate to copy assignment
    *this = other;
}

FFTWArray::FFTWArray(FFTWArray&& other)
{
    // delegate to move assignment
    *this = std::move(other);
}

FFTWArray::~FFTWArray()
{
    fftw_free(data);
}

// assignment

FFTWArray& FFTWArray::operator=(const FFTWArray& other)
{
    if (this != &other)
    {
        resize(other.length);
        copyFrom(other.data, other.length);
    }
    return *this;
}

FFTWArray& FFTWArray::operator=(FFTWArray&& other)
{
    if (this != &other)
    {
        data = other.data;
        length = other.length;
        other.data = nullptr;
        other.length = 0;
    }
    return *this;
}

// access

std::complex<double> FFTWArray::getAsComplex(int i)
{
    jassert(i >= 0 && i < length);
    return data[i];
}

double FFTWArray::getAsReal(int i)
{
    jassert(i >= 0 && i < length * 2);
    return reinterpret_cast<double*>(data)[i];
}

std::complex<double>* FFTWArray::getComplexPointer(int index)
{
    if (index < length && index >= 0)
    {
        return data + index;
    }
    return nullptr;
}

double* FFTWArray::getRealPointer(int index)
{
    if (index < length * 2 && index >= 0)
    {
        return reinterpret_cast<double*>(data)+index;
    }
    return nullptr;
}

const double* FFTWArray::getReadPointer(int index) const
{
    if (index < length * 2 && index >= 0)
    {
        return reinterpret_cast<const double*>(data)+index;
    }
    return nullptr;
}

int FFTWArray::getLength() const
{
    return length;
}

// modification

bool FFTWArray::resize(int newLength)
{
    jassert(newLength >= 0);
    if (newLength != length)
    {
        length = newLength;
        fftw_free(data);
        data = reinterpret_cast<std::complex<double>*>(fftw_alloc_complex(newLength));
        return true;
    }
    return false;
}

void FFTWArray::set(int i, std::complex<double> val)
{
    jassert(i >= 0 && i < length);
    data[i] = val;
}

void FFTWArray::set(int i, double val)
{
    jassert(i >= 0 && i < length * 2);
    reinterpret_cast<double*>(data)[i] = val;
}

void FFTWArray::reverseComplex(int reverseLength)
{
    reverseLength = reverseLength >= 0 ? reverseLength : length;
    std::complex<double>* first = data;
    std::complex<double>* last = data + reverseLength;
    std::reverse<std::complex<double>*>(first, last);
}

void FFTWArray::reverseReal(int reverseLength)
{
    reverseLength = reverseLength >= 0 ? reverseLength : length * 2;
    double* first = reinterpret_cast<double*>(data);
    double* last = first + reverseLength;
    std::reverse<double*>(first, last);
}

int FFTWArray::copyFrom(const std::complex<double>* fromArr, int num, int startInd)
{
    int numToCopy = jmin(num, length - startInd);
    std::complex<double>* wp = getComplexPointer(startInd);
    for (int i = 0; i < numToCopy; ++i)
    {
        wp[i] = fromArr[i];
    }

    return numToCopy;
}

int FFTWArray::copyFrom(const double* fromArr, int num, int startInd)
{
    int numToCopy = jmin(num, 2 * length - startInd);
    double* wpReal = getRealPointer(startInd);
    for (int i = 0; i < numToCopy; ++i)
    {
        wpReal[i] = fromArr[i];
    }

    return numToCopy;
}

void FFTWArray::freqDomainHilbert()
{
    if (length <= 0) { return; }

    // normalize DC and Nyquist, normalize and double positive freqs, and set negative freqs to 0.
    int lastPosFreq = (length + 1) / 2 - 1;
    int firstNegFreq = length / 2 + 1;
    int numPosNegFreqDoubles = lastPosFreq * 2; // sizeof(complex<double>) = 2 * sizeof(double)
    bool hasNyquist = (length % 2 == 0);

    // normalize but don't double DC value
    data[0] /= length;

    // normalize and double positive frequencies
    FloatVectorOperations::multiply(reinterpret_cast<double*>(data + 1), 2.0 / length, numPosNegFreqDoubles);

    if (hasNyquist)
    {
        // normalize but don't double Nyquist frequency
        data[lastPosFreq + 1] /= length;
    }

    // set negative frequencies to 0
    FloatVectorOperations::clear(reinterpret_cast<double*>(data + firstNegFreq), numPosNegFreqDoubles);
}


// FFTWPlan

FFTWPlan::FFTWPlan(int n, FFTWArray* in, FFTWArray* out, unsigned int flags)
    : length(n)
{
    double* ptr_in = in->getRealPointer();
    fftw_complex* ptr_out = reinterpret_cast<fftw_complex*>(out->getComplexPointer());
    plan = fftw_plan_dft_r2c_1d(n, ptr_in, ptr_out, flags);
}

FFTWPlan::FFTWPlan(int n, FFTWArray* buf, unsigned int flags)
    : FFTWPlan(n, buf, buf, flags)
{}

FFTWPlan::FFTWPlan(int n, FFTWArray* in, FFTWArray* out, int sign, unsigned int flags)
    : length(n)
{
    fftw_complex* ptr_in = reinterpret_cast<fftw_complex*>(in->getComplexPointer());
    fftw_complex* ptr_out = reinterpret_cast<fftw_complex*>(out->getComplexPointer());
    plan = fftw_plan_dft_1d(n, ptr_in, ptr_out, sign, flags);
}

FFTWPlan::FFTWPlan(int n, FFTWArray* buf, int sign, unsigned int flags)
    : FFTWPlan(n, buf, buf, sign, flags)
{}

FFTWPlan::~FFTWPlan()
{
    fftw_destroy_plan(plan);
}

void FFTWPlan::execute()
{
    fftw_execute(plan);
}

int FFTWPlan::getLength()
{
    return length;
}


// FFTWTransformableArray

FFTWTransformableArray::FFTWTransformableArray(int n, unsigned int flags)
    : FFTWArray(0)
    , flags(flags)
{
    resize(n);
}

FFTWTransformableArray::FFTWTransformableArray(const FFTWTransformableArray& other)
    : FFTWTransformableArray(other.getLength(), other.flags)
{
    // delegate to copy assignment
    *this = other;
}

FFTWTransformableArray::FFTWTransformableArray(FFTWTransformableArray&& other)
{
    // delegate to move assignment
    *this = std::move(other);
}

FFTWTransformableArray& FFTWTransformableArray::operator=(const FFTWTransformableArray& other)
{
    if (this != &other)
    {
        if (flags != other.flags)
        {
            flags = other.flags;
            resize(0); // force plans to be remade
        }
        FFTWArray::operator=(other);
        // the plans will be copied if/when the overridden "resize" is called
    }
    return *this;
}

FFTWTransformableArray& FFTWTransformableArray::operator=(FFTWTransformableArray&& other)
{
    if (this != &other)
    {
        flags = other.flags;
        forwardPlan = other.forwardPlan;
        inversePlan = other.inversePlan;
        r2cPlan = other.r2cPlan;
        FFTWArray::operator=(std::move(other));
    }
    return *this;
}

bool FFTWTransformableArray::resize(int newLength)
{
    if (FFTWArray::resize(newLength))
    {
        forwardPlan = newLength > 0 ? new FFTWPlan(newLength, this, FFTW_FORWARD, flags) : nullptr;
        inversePlan = newLength > 0 ? new FFTWPlan(newLength, this, FFTW_BACKWARD, flags) : nullptr;
        r2cPlan = newLength > 0 ? new FFTWPlan(newLength, this, flags) : nullptr;
        return true;
    }
    return false;
}

void FFTWTransformableArray::fftComplex()
{
    if (forwardPlan != nullptr)
    {
        forwardPlan->execute();
    }
}

void FFTWTransformableArray::fftReal()
{
    if (r2cPlan != nullptr)
    {
        r2cPlan->execute();
    }
}

void FFTWTransformableArray::ifft()
{
    if (inversePlan != nullptr)
    {
        inversePlan->execute();
    }
}

void FFTWTransformableArray::hilbert()
{
    fftReal();
    freqDomainHilbert();
    ifft();
}
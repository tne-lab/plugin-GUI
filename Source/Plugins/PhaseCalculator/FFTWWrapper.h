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

/*
Object-oriented / RAII-friendly wrapper for relevant parts of the FFTW Fourier
transform library
*/

#ifndef FFTW_WRAPPER_H_INCLUDED
#define FFTW_WRAPPER_H_INCLUDED

#include "fftw3.h" // Fast Fourier Transform library
#include <complex>
#include <algorithm> // reverse array

using namespace std;

template<class T>
class FFTWArray
{
public:
    // creation / deletion
    FFTWArray(int len)
    {
        length = len;
        data = static_cast<T*>(fftw_malloc(sizeof(T) * len));
    }

    ~FFTWArray()
    {
        fftw_free(data);
    }

    void resize(int newLength)
    {
        if (newLength != length)
        {
            length = newLength;
            fftw_free(data);
            data = static_cast<T*>(fftw_malloc(sizeof(T) * newLength));
        }
    }

    // accession
    T operator[](int i)
    {
        return data[i];
    }

    const T* getReadPointer(int index = 0)
    {
        return getWritePointer(index);
    }

    T* getWritePointer(int index = 0)
    {
        if (index < length && index >= 0)
            return data + index;
        return nullptr;
    }

    int getLength()
    {
        return length;
    }

    // modification
    void set(int i, T val)
    {
        data[i] = val;
    }

    void reverse(void)
    {
        T* first = data;
        T* last = data + length;

        std::reverse<T*>(first, last);
    }

    /* Copies up to num elements starting at fromArr to the array starting at startInd.
     * Returns the number of elements actually copied.
     */
    int copyFrom(const T* fromArr, int num, int startInd = 0)
    {
        int i;
        for (i = 0; i < num && i < (length - startInd); i++)
            data[i + startInd] = fromArr[i];
        return i;
    }

private:
    T* data;
    int length;
};

class FFTWPlan
{
public:
    // r2c constructor
    FFTWPlan(int n, FFTWArray<double>* in, FFTWArray<complex<double>>* out, unsigned int flags)
    {
        double* ptr_in = in->getWritePointer();
        fftw_complex* ptr_out = reinterpret_cast<fftw_complex*>(out->getWritePointer());
        plan = fftw_plan_dft_r2c_1d(n, ptr_in, ptr_out, flags);
    }

    // c2c constructor
    FFTWPlan(int n, FFTWArray<complex<double>>* in, FFTWArray<complex<double>>* out, int sign, unsigned int flags)
    {
        fftw_complex* ptr_in = reinterpret_cast<fftw_complex*>(in->getWritePointer());
        fftw_complex* ptr_out = reinterpret_cast<fftw_complex*>(out->getWritePointer());
        plan = fftw_plan_dft_1d(n, ptr_in, ptr_out, sign, flags);
    }

    ~FFTWPlan()
    {
        fftw_destroy_plan(plan);
    }

    void execute()
    {
        fftw_execute(plan);
    }

private:
    fftw_plan plan;
};

#endif // FFTW_WRAPPER_H_INCLUDED
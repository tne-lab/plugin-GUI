/*
   Previously called mempar()
   Originally in FORTRAN, hence the array offsets of 1, Yuk.
   Original code from Kay, 1988, appendix 8D.

   Perform Burg's Maximum Entropy AR parameter estimation
   outputting (or not) successive models en passant. Sourced from Alex Sergejew

   Two small changes made by NH in November 1998:
   tstarz.h no longer included, just say "typedef double REAL" instead
   Declare ar by "REAL **ar" instead of "REAL ar[MAXA][MAXA]

   Further "cleaning" by Paul Bourke.....for personal style only.

   Converted to zero-based arrays by Paul Sanders, June 2007

   Simplified and 'g' removed, plus class wrapper added by Ethan Blackwood, 2018
*/

#ifndef AR_MODELER_H_INCLUDED
#define AR_MODELER_H_INCLUDED

#include "../../../JuceLibraryCode/JuceHeader.h"

class ARModeler {
public:
    ARModeler() = delete;
    ARModeler(int order, int length, int strideIn = 1, bool* success = nullptr) 
        : arOrder       (order)
        , stride        (strideIn)
        , inputLength   (length)
        , stridedLength (calcStridedLength(length, strideIn))
    {
        if (order < 1 || stridedLength < order + 1)
        {
            // invalid length/order/stride combination
            jassertfalse;
            if (success != nullptr) { *success = false; }
            arOrder = 1;
            inputLength = 2;
            stridedLength = 2;
            stride = 1;
        }
        else if (success != nullptr) { *success = true; }
        reallocateStorage();
    }

    ~ARModeler() { }

    // Set order; returns true if successful.
    bool setOrder(int order)
    {
        if (order < 1 || stridedLength < order + 1)
        {
            jassertfalse;
            return false;
        }
        arOrder = order;
        reallocateStorage();
        return true;
    }

    // Set length; returns true if successful.
    bool setInputLength(int length)
    {
        int newStridedLength = calcStridedLength(length, stride);
        if (newStridedLength < arOrder + 1)
        {
            jassertfalse;
            return false;
        }
        inputLength = length;
        stridedLength = newStridedLength;
        reallocateStorage();
        return true;
    }

    // Set stride; returns true if successful.
    bool setStride(int newStride)
    {
        int newStridedLength = calcStridedLength(inputLength, newStride);
        if (newStridedLength < arOrder + 1)
        {
            jassertfalse;
            return false;
        }
        stride = newStride;
        stridedLength = newStridedLength;
        reallocateStorage();
        return true;
    }

    void fitModel(const Array<double>& inputseries, Array<double>& coef)
    {
        jassert(inputseries.size() == inputLength);
        jassert(coef.size() == arOrder);
        double t1, t2;
        int n;

        // reset per and pef
        resetPredictionError();

        for (n = 1; n <= arOrder; n++)
        {
            double sn = 0.0;
            double sd = 0.0;
            int j;
            int jj = stridedLength - n;

            for (j = 0; j < jj; j++)
            {
                t1 = inputseries[stride * (j + n)] + pef[j];
                t2 = inputseries[stride * j] + per[j];
                sn -= 2.0 * t1 * t2;
                sd += (t1 * t1) + (t2 * t2);
            }

            t1 = sn / sd;
            coef.setUnchecked(n - 1, t1);
            if (n != 1)
            {
                for (j = 1; j < n; j++)
                    h.setUnchecked(j - 1, coef[j - 1] + t1 * coef[n - j - 1]);
                for (j = 1; j < n; j++)
                    coef.setUnchecked(j - 1, h[j - 1]);
                jj--;
            }

            for (j = 0; j < jj; j++)
            {
                per.setUnchecked(j, per[j] + t1 * pef[j] + t1 * inputseries[stride * (j + n)]);
                pef.setUnchecked(j, pef[j + 1] + t1 * per[j + 1] + t1 * inputseries[stride * (j + 1)]);
            }
        }
    }

private:

    void reallocateStorage()
    {
        h.resize(arOrder - 1);
        resetPredictionError();
    }

    void resetPredictionError()
    {
        per.clearQuick();
        per.insertMultiple(0, 0, stridedLength);
        pef.clearQuick();
        pef.insertMultiple(0, 0, stridedLength);
    }

    static int calcStridedLength(int inputLength, int stride) inline
    {
        jassert(stride > 0);
        return (inputLength + (stride - 1)) / stride;
    }

    int arOrder;
    int inputLength;
    int stridedLength;
    int stride;
    Array<double> per;
    Array<double> pef;
    Array<double> h;
};

#endif AR_MODELER_H_INCLUDED
/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once
#include "IPlugPlatform.h"
#include <algorithm>

BEGIN_IPLUG_NAMESPACE

// A static delayline used to delay bypassed signals to match mLatency in AAX/VST3/AU
template<typename T>
class NChanDelayLine
{
public:
  NChanDelayLine(int nInputChans = 2, int nOutputChans = 2)
  : mNInChans(nInputChans)
  , mNOutChans(nOutputChans)
  {}

  // Local modification: reserve only while rendering is genuinely excluded.
  // Delay changes never resize or clear history. Audio owns all indices/taps.
  void Prepare(int maximumDelaySamples)
  {
    mCapacity = std::max(1, maximumDelaySamples + 1);
    mBuffer.Resize(mNInChans * mCapacity);
    mWriteAddress = 0;
    ClearBuffer();
  }

  bool SetDelayTime(int samples, unsigned fadeSamples = 0)
  {
    if (samples < 0 || static_cast<unsigned>(samples) >= mCapacity) return false;
    if (static_cast<unsigned>(samples) == mDTSamples) return true;
    // Caller coalesces requests until the preceding transition is complete.
    if (mRemaining) return false;
    mPreviousDelay = mDTSamples;
    mDTSamples = static_cast<unsigned>(samples);
    mRemaining = mFadeSamples = fadeSamples;
    return true;
  }
  unsigned DelayTime() const { return mDTSamples; }
  unsigned Remaining() const { return mRemaining; }

  void ClearBuffer()
  {
    memset(mBuffer.Get(), 0, mNInChans * mCapacity * sizeof(T));
    mRemaining = 0;
  }

  // outputs == nullptr keeps history warm during normal plugin processing.
  void ProcessBlock(T** inputs, T** outputs, int nFrames)
  {
    T* buffer = mBuffer.Get();

    for (auto s = 0 ; s < nFrames; ++s)
    {
      const double weight = mRemaining ? double(mFadeSamples - mRemaining) / mFadeSamples : 1.;
      for (unsigned c = 0; c < mNInChans; c++)
      {
        if (c < mNOutChans)
        {
          T input = inputs[c][s];
          const unsigned offset = c * mCapacity;
          buffer[offset + mWriteAddress] = input;
          if (outputs)
          {
            const T next = buffer[offset + (mWriteAddress + mCapacity - mDTSamples) % mCapacity];
            const T previous = mRemaining
              ? buffer[offset + (mWriteAddress + mCapacity - mPreviousDelay) % mCapacity] : next;
            outputs[c][s] = mRemaining ? T((1. - weight) * previous + weight * next) : next;
          }
        }
      }

      mWriteAddress++;
      mWriteAddress %= mCapacity;
      if (mRemaining) --mRemaining;
    }
    if (outputs)
      for (unsigned c = mNInChans; c < mNOutChans; ++c)
        memset(outputs[c], 0, nFrames * sizeof(T));
  }

private:
  WDL_TypedBuf<T> mBuffer;
  uint32_t mNInChans, mNOutChans;
  uint32_t mWriteAddress = 0;
  uint32_t mDTSamples = 0;
  uint32_t mCapacity = 1, mPreviousDelay = 0, mFadeSamples = 0, mRemaining = 0;
} WDL_FIXALIGN;

END_IPLUG_NAMESPACE

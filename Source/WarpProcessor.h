#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include "signalsmith-stretch.h"

class WarpProcessor
{
public:
   struct WarpMarker
   {
      int samplePos{ 0 };
      float beatPos{ 0 };
      bool isAnchor{ false };
   };

   WarpProcessor() {}

   void Reset()
   {
      mWarpMarkers.clear();
      mStretcher.reset();
      mPrevStretchRatio = 1.0f;
   }

   void SetSampleRate(float sampleRate)
   {
      mSampleRate = sampleRate;
   }

   void SetWarpMarkers(const std::vector<WarpMarker>& markers)
   {
      mWarpMarkers = markers;
      std::sort(mWarpMarkers.begin(), mWarpMarkers.end(),
         [](const WarpMarker& a, const WarpMarker& b) { return a.samplePos < b.samplePos; });
   }

   void SetTargetBPM(float bpm)
   {
      mTargetBPM = bpm;
   }

   void Configure(int numChannels, float sampleRate)
   {
      mNumChannels = numChannels;
      mSampleRate = sampleRate;
      mStretcher.presetDefault(numChannels, sampleRate);
      mStretcher.reset();
      mPrevStretchRatio = 1.0f;
   }

   void NotifySeek()
   {
      mStretcher.reset();
      mPrevStretchRatio = 1.0f;
   }

   bool HasWarpMarkers() const { return mWarpMarkers.size() >= 2; }

   // Get the stretch ratio at a given sample position.
   // ratio > 1.0 = audio is shorter than expected (plays faster to stay on grid)
   // ratio < 1.0 = audio is longer than expected (plays slower to stay on grid)
   float GetStretchRatio(int samplePos) const
   {
      if (mWarpMarkers.size() < 2 || mTargetBPM <= 0)
         return 1.0f;

      float samplesPerBeat = mSampleRate * 60.0f / mTargetBPM;

      int idx = 0;
      for (int i = 0; i < (int)mWarpMarkers.size() - 1; ++i)
      {
         if (samplePos >= mWarpMarkers[i].samplePos && samplePos < mWarpMarkers[i + 1].samplePos)
         {
            idx = i;
            break;
         }
         if (i == (int)mWarpMarkers.size() - 2)
            idx = i;
      }

      const WarpMarker& a = mWarpMarkers[idx];
      const WarpMarker& b = mWarpMarkers[idx + 1];

      int audioLen = b.samplePos - a.samplePos;
      if (audioLen <= 0) return 1.0f;

      float beatLen = (b.beatPos - a.beatPos) * samplesPerBeat;
      if (beatLen <= 0) return 1.0f;

      return beatLen / (float)audioLen;
   }

   // True time-stretch via Signalsmith Stretch.
   // inputSamples: number of samples available per channel in inputChannels
   // outputSamples: max samples to write per channel in outputChannels
   // playbackRate: base playback rate (1.0 = normal speed)
   // currentSamplePos: current read position in the sample
   // Returns: number of output samples written per channel
   int ProcessAudio(const float* const* inputChannels, int inputSamples,
                    float* const* outputChannels, int outputSamples,
                    float playbackRate, int currentSamplePos)
   {
      if (!HasWarpMarkers() || inputSamples <= 0 || outputSamples <= 0)
         return 0;

      float stretchRatio = GetStretchRatio(currentSamplePos);

      // If ratio is very close to 1.0, just copy through
      if (std::abs(stretchRatio - 1.0f) < 0.001f && std::abs(playbackRate - 1.0f) < 0.001f)
      {
         int samplesToCopy = std::min(inputSamples, outputSamples);
         for (int ch = 0; ch < mNumChannels; ++ch)
            std::memcpy(outputChannels[ch], inputChannels[ch], samplesToCopy * sizeof(float));
         return samplesToCopy;
      }

      // Signalsmith Stretch adapter structs
      struct InAdapter {
         const float* data;
         const float& operator[](int i) const { return data[i]; }
      };
      struct OutAdapter {
         float* data;
         float& operator[](int i) { return data[i]; }
      };

      std::vector<InAdapter> in(mNumChannels);
      std::vector<OutAdapter> out(mNumChannels);
      for (int ch = 0; ch < mNumChannels; ++ch)
      {
         in[ch].data = inputChannels[ch];
         out[ch].data = outputChannels[ch];
      }

      // Signalsmith uses inputSamples/outputSamples to determine the effective stretch
      // We want: outputSamples = inputSamples / (playbackRate * stretchRatio)
      float effectiveRate = playbackRate * stretchRatio;
      int actualOutput = std::min(outputSamples, std::max(1, (int)(inputSamples / effectiveRate)));

      mStretcher.process(in, inputSamples, out, actualOutput);
      mPrevStretchRatio = stretchRatio;
      return actualOutput;
   }

private:
   std::vector<WarpMarker> mWarpMarkers;
   float mSampleRate{ 44100 };
   float mTargetBPM{ 120 };
   int mNumChannels{ 2 };
   float mPrevStretchRatio{ 1.0f };

   signalsmith::stretch::SignalsmithStretch<float> mStretcher;
};

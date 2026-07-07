#pragma once

#include <cmath>

enum LadderFilterMode
{
   kLadderMode_Lowpass,
   kLadderMode_Bandpass,
   kLadderMode_Highpass
};

class LadderFilter
{
public:
   LadderFilter();

   void SetSampleRate(double sampleRate) { mSampleRate = sampleRate; UpdateCoeffs(); }
   void Clear();

   void SetCutoff(float freq);
   void SetResonance(float res);
   void SetMode(LadderFilterMode mode) { mMode = mode; Clear(); }
   void SetDrive(float drive) { mDrive = drive; }

   float Filter(float in);
   void Filter(float* buffer, int bufferSize);

   float mCutoff{ 800.0f };
   float mResonance{ 0.0f };
   float mDrive{ 0.0f };
   LadderFilterMode mMode{ kLadderMode_Lowpass };

private:
   void UpdateCoeffs();

   double mSampleRate{ 44100.0 };
   float mStage[4]{};
   float mG{ 0.0f };
   float mGCorrect{ 0.0f };
};

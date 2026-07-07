#include "LadderFilter.h"
#include "SynthGlobals.h"
#include <algorithm>

LadderFilter::LadderFilter()
: mSampleRate(gSampleRate)
{
   Clear();
   UpdateCoeffs();
}

void LadderFilter::Clear()
{
   for (auto& s : mStage)
      s = 0.0f;
}

void LadderFilter::SetCutoff(float freq)
{
   if (mCutoff != freq)
   {
      mCutoff = freq;
      UpdateCoeffs();
   }
}

void LadderFilter::SetResonance(float res)
{
   mResonance = res;
}

void LadderFilter::UpdateCoeffs()
{
   float fc = ofClamp(mCutoff / mSampleRate, 0.0f, 0.49f);
   float wc = 2.0f * float(M_PI) * fc;
   mG = 1.0f - std::exp(-wc);
   mGCorrect = 1.0f / (1.0f - mResonance * 0.95f + 0.05f);
}

float LadderFilter::Filter(float in)
{
   float fb = 4.0f * mResonance * mStage[3];
   float u = std::tanh((in - fb) * (1.0f + mDrive * 4.0f));

   for (int i = 0; i < 4; ++i)
   {
      u = mStage[i] + mG * (std::tanh(u) - mStage[i]);
      mStage[i] = u;
   }

   float output;
   switch (mMode)
   {
      case kLadderMode_Lowpass:
         output = mStage[3];
         break;
      case kLadderMode_Bandpass:
         output = (mStage[1] - mStage[3]) * 2.0f;
         break;
      case kLadderMode_Highpass:
         output = std::tanh(in - fb) - mStage[3];
         break;
   }

   output = std::tanh(output * mGCorrect);
   return output;
}

void LadderFilter::Filter(float* buffer, int bufferSize)
{
   for (int i = 0; i < bufferSize; ++i)
      buffer[i] = Filter(buffer[i]);
}

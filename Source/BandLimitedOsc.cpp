#include "BandLimitedOsc.h"
#include "SynthGlobals.h"

BandLimitedOsc::BandLimitedOsc()
{
}

float BandLimitedOsc::PolyBLEP(float t, float dt)
{
   if (t < dt)
   {
      float x = t / dt;
      return x * x - 1.0f;
   }
   else if (t > 1.0f - dt)
   {
      float x = (t - 1.0f) / dt;
      return -(x * x) + 1.0f;
   }
   return 0.0f;
}

float BandLimitedOsc::Saw(float phase, float phaseInc)
{
   float naive = 2.0f * phase - 1.0f;
   float blep = PolyBLEP(phase, phaseInc);
   return naive - blep;
}

float BandLimitedOsc::Square(float phase, float phaseInc)
{
   float naive = phase < 0.5f ? 1.0f : -1.0f;
   float blep1 = PolyBLEP(phase, phaseInc);
   float blep2 = PolyBLEP(fmodf(phase + 0.5f, 1.0f), phaseInc);
   return naive + blep1 - blep2;
}

float BandLimitedOsc::Triangle(float phase, float phaseInc)
{
   float naive = 2.0f * fabsf(2.0f * phase - 1.0f) - 1.0f;
   float blep1 = PolyBLEP(phase, phaseInc);
   float blep2 = PolyBLEP(fmodf(phase + 0.5f, 1.0f), phaseInc);
   float slope = 2.0f * phaseInc; // slope change magnitude
   return naive + slope * (blep1 + blep2);
}

float BandLimitedOsc::Pulse(float phase, float phaseInc)
{
   float naive = phase < mPulseWidth ? 1.0f : -1.0f;
   float blep1 = PolyBLEP(phase, phaseInc);
   float blep2 = PolyBLEP(fmodf(phase + (1.0f - mPulseWidth), 1.0f), phaseInc);
   return naive + blep1 - blep2;
}

float BandLimitedOsc::Value(float phase, float phaseInc)
{
   switch (mType)
   {
      case kBLType_Saw:      return Saw(phase, phaseInc);
      case kBLType_Square:   return Square(phase, phaseInc);
      case kBLType_Triangle: return Triangle(phase, phaseInc);
      case kBLType_Sin:      return sinf(phase * 2.0f * float(M_PI));
      case kBLType_Pulse:    return Pulse(phase, phaseInc);
   }
   return 0.0f;
}

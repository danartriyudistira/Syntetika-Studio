#pragma once

#include <cmath>

#include "OpenFrameworksPort.h"

enum BandLimitedOscType
{
   kBLType_Saw,
   kBLType_Square,
   kBLType_Triangle,
   kBLType_Sin,
   kBLType_Pulse
};

class BandLimitedOsc
{
public:
   BandLimitedOsc();

   void SetType(BandLimitedOscType type) { mType = type; }
   void SetPulseWidth(float pw) { mPulseWidth = ofClamp(pw, 0.01f, 0.99f); }

   float Value(float phase, float phaseInc);

   static float PolyBLEP(float t, float dt);

private:
   float Saw(float phase, float phaseInc);
   float Square(float phase, float phaseInc);
   float Triangle(float phase, float phaseInc);
   float Pulse(float phase, float phaseInc);

   BandLimitedOscType mType{ kBLType_Saw };
   float mPulseWidth{ 0.5f };
};

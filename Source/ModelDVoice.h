#pragma once

#include "IMidiVoice.h"
#include "IVoiceParams.h"
#include "ADSR.h"
#include "Oscillator.h"
#include "BiquadFilter.h"

#define MODELD_NOISE_BUF 256

class ModelDParams : public IVoiceParams
{
public:
   OscillatorType mOsc1Type{ kOsc_Saw };
   float mOsc1Vol{ 0.5f };
   int mOsc1Octave{ 0 };
   int mOsc1Semitone{ 0 };
   float mOsc1Fine{ 0.0f };
   bool mOsc1Enabled{ true };
   float mOsc1PW{ 0.5f };

   OscillatorType mOsc2Type{ kOsc_Saw };
   float mOsc2Vol{ 0.5f };
   int mOsc2Octave{ 0 };
   int mOsc2Semitone{ 0 };
   float mOsc2Fine{ 0.0f };
   bool mOsc2Enabled{ true };
   float mOsc2PW{ 0.5f };

   OscillatorType mOsc3Type{ kOsc_Saw };
   float mOsc3Vol{ 0.5f };
   int mOsc3Octave{ 0 };
   int mOsc3Semitone{ 0 };
   float mOsc3Fine{ 0.0f };
   bool mOsc3Enabled{ true };
   float mOsc3PW{ 0.5f };

   float mNoiseAmount{ 0.0f };

   float mFilterCutoff{ 800.0f };
   float mFilterResonance{ 0.707f };
   float mFilterDrive{ 0.0f };
   float mFilterKbdTrack{ 0.0f };
   float mFilterEnvAmount{ 0.0f };

   ::ADSR mAdsr{ 10, 0, 1, 10 };
   ::ADSR mFilterAdsr{ 10, 0, 1, 10 };

   float mLFORate{ 2.0f };
   float mLFODepth{ 0.0f };
   OscillatorType mLFOType{ kOsc_Tri };
   int mLFOTarget{ 0 };

   float mPortamento{ 0.0f };
   float mVol{ 1.0f };
};

class ModelDVoice : public IMidiVoice
{
public:
   ModelDVoice(IDrawableModule* owner = nullptr);
   ~ModelDVoice();

   void Start(double time, float amount) override;
   void Stop(double time) override;
   void ClearVoice() override;
   bool Process(double time, ChannelBuffer* out, int oversampling) override;
   void SetVoiceParams(IVoiceParams* params) override;
   bool IsDone(double time) override;

   static const int kNoiseBufSize = MODELD_NOISE_BUF;

private:
   struct OscData
   {
      float mPhase{ 0.0f };
      Oscillator mOsc{ kOsc_Saw };
   };

   void UpdatePinkNoise(float* buf, int len);

   OscData mOsc1, mOsc2, mOsc3;
   ::ADSR mAdsr;
   ::ADSR mFilterAdsr;
   ModelDParams* mVoiceParams{ nullptr };

   BiquadFilter mFilterL;
   BiquadFilter mFilterR;

   float mLFOPhase{ 0.0f };
   float mPinkNoiseBuf[MODELD_NOISE_BUF];
   int mPinkNoiseIdx{ 0 };
   bool mPinkNoiseInit{ false };
   float mPinkNoiseState[4]{};

   IDrawableModule* mOwner;
};

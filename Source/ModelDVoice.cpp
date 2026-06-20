#include "ModelDVoice.h"
#include "SynthGlobals.h"
#include "Scale.h"
#include "Profiler.h"
#include "ChannelBuffer.h"
#include "LFO.h"

ModelDVoice::ModelDVoice(IDrawableModule* owner)
: mOwner(owner)
{
   mOsc1.mOsc.SetPulseWidth(0.5f);
   mOsc2.mOsc.SetPulseWidth(0.5f);
   mOsc3.mOsc.SetPulseWidth(0.5f);
}

ModelDVoice::~ModelDVoice()
{
}

bool ModelDVoice::IsDone(double time)
{
   return mAdsr.IsDone(time);
}

void ModelDVoice::UpdatePinkNoise(float* buf, int len)
{
   for (int i = 0; i < len; ++i)
   {
      float white = ofRandom(-1.0f, 1.0f);
      mPinkNoiseState[0] = 0.99886f * mPinkNoiseState[0] + white * 0.0555179f;
      mPinkNoiseState[1] = 0.99332f * mPinkNoiseState[1] + white * 0.0750759f;
      mPinkNoiseState[2] = 0.96900f * mPinkNoiseState[2] + white * 0.1538520f;
      mPinkNoiseState[3] = 0.86650f * mPinkNoiseState[3] + white * 0.3104856f;
      float pink = (mPinkNoiseState[0] + mPinkNoiseState[1] + mPinkNoiseState[2] + mPinkNoiseState[3] + white * 0.536524f) * 0.11f;
      buf[i] = ofClamp(pink, -1.0f, 1.0f);
   }
}

bool ModelDVoice::Process(double time, ChannelBuffer* out, int oversampling)
{
   PROFILER(ModelDVoice);

   if (IsDone(time))
      return false;

   int bufferSize = out->BufferSize();
   bool mono = (out->NumActiveChannels() == 1);

   mOsc1.mOsc.SetType(mVoiceParams->mOsc1Type);
   mOsc1.mOsc.SetPulseWidth(mVoiceParams->mOsc1PW);
   mOsc2.mOsc.SetType(mVoiceParams->mOsc2Type);
   mOsc2.mOsc.SetPulseWidth(mVoiceParams->mOsc2PW);
   mOsc3.mOsc.SetType(mVoiceParams->mOsc3Type);
   mOsc3.mOsc.SetPulseWidth(mVoiceParams->mOsc3PW);

   if (!mPinkNoiseInit)
   {
      UpdatePinkNoise(mPinkNoiseBuf, MODELD_NOISE_BUF);
      mPinkNoiseInit = true;
   }

   for (int pos = 0; pos < bufferSize; ++pos)
   {
      if (mOwner)
         mOwner->ComputeSliders(pos);

      float pitch = GetPitch(pos);
      float freq = TheScale->PitchToFreq(pitch);

      float adsrVal = mAdsr.Value(time);
      float filterAdsrVal = mFilterAdsr.Value(time);

      float sample = 0.0f;

      auto renderOsc = [&](OscData& od, OscillatorType type, float vol, int oct, int semi, float fine, bool enabled) -> float
      {
         if (!enabled || vol == 0.0f)
            return 0.0f;
          float oscFreq = freq;
          oscFreq *= std::pow(2.0f, (float)oct);
          oscFreq *= std::pow(2.0f, (semi + fine) / 12.0f);
         float phaseInc = GetPhaseInc(oscFreq);
         od.mPhase += phaseInc;
         while (od.mPhase > FTWO_PI * 2)
            od.mPhase -= FTWO_PI * 2;
         return od.mOsc.Value(od.mPhase) * vol;
      };

      sample += renderOsc(mOsc1, mVoiceParams->mOsc1Type, mVoiceParams->mOsc1Vol,
                          mVoiceParams->mOsc1Octave, mVoiceParams->mOsc1Semitone, mVoiceParams->mOsc1Fine * (1.0f/100.0f),
                          mVoiceParams->mOsc1Enabled);

      sample += renderOsc(mOsc2, mVoiceParams->mOsc2Type, mVoiceParams->mOsc2Vol,
                          mVoiceParams->mOsc2Octave, mVoiceParams->mOsc2Semitone, mVoiceParams->mOsc2Fine * (1.0f/100.0f),
                          mVoiceParams->mOsc2Enabled);

      sample += renderOsc(mOsc3, mVoiceParams->mOsc3Type, mVoiceParams->mOsc3Vol,
                          mVoiceParams->mOsc3Octave, mVoiceParams->mOsc3Semitone, mVoiceParams->mOsc3Fine * (1.0f/100.0f),
                          mVoiceParams->mOsc3Enabled);

      if (mVoiceParams->mNoiseAmount > 0.0f)
      {
         float noise = mPinkNoiseBuf[mPinkNoiseIdx];
         mPinkNoiseIdx = (mPinkNoiseIdx + 1) % MODELD_NOISE_BUF;
         sample += noise * mVoiceParams->mNoiseAmount * 0.3f;
      }

      float filterFreq = mVoiceParams->mFilterCutoff;

      float kbdOffset = (pitch - 60.0f) * mVoiceParams->mFilterKbdTrack * 5.0f;
      filterFreq *= std::pow(2.0f, kbdOffset / 12.0f);

      float envMod = filterAdsrVal * mVoiceParams->mFilterEnvAmount * 48.0f;
      filterFreq *= std::pow(2.0f, envMod / 12.0f);

      if (mVoiceParams->mLFODepth > 0.0f)
      {
         mLFOPhase += GetPhaseInc(mVoiceParams->mLFORate);
         while (mLFOPhase > FTWO_PI * 2)
            mLFOPhase -= FTWO_PI * 2;

         LFO tmpLFO;
         float lfoVal = tmpLFO.Value(0, mLFOPhase / FTWO_PI);

         switch (mVoiceParams->mLFOTarget)
         {
            case 0: break;
            case 1:
            {
               float mod = lfoVal * mVoiceParams->mLFODepth * 12.0f;
               filterFreq *= std::pow(2.0f, mod / 12.0f);
               break;
            }
            case 2:
               sample *= 1.0f + lfoVal * mVoiceParams->mLFODepth * 0.5f;
               break;
         }
      }

      filterFreq = ofClamp(filterFreq, 20.0f, gSampleRate * 0.45f);

      mFilterL.SetFilterParams(filterFreq, mVoiceParams->mFilterResonance);
      mFilterR.SetFilterParams(filterFreq, mVoiceParams->mFilterResonance);

      float drive = mVoiceParams->mFilterDrive;
      if (drive > 0.0f)
         sample = std::tanh(sample * (1.0f + drive * 4.0f));

      sample = mFilterL.Filter(sample);
      float vol = mVoiceParams ? mVoiceParams->mVol : 1.0f;
      out->GetChannel(0)[pos] += sample * adsrVal * vol;
      if (!mono)
         out->GetChannel(1)[pos] += sample * adsrVal * vol;

      time += gInvSampleRateMs;
   }

   return true;
}

void ModelDVoice::Start(double time, float target)
{
   mAdsr.Start(time, 1, mVoiceParams->mAdsr);
   mFilterAdsr.Start(time, 1, mVoiceParams->mFilterAdsr);

   mFilterL.SetFilterType(kFilterType_Lowpass);
   mFilterR.SetFilterType(kFilterType_Lowpass);
   mFilterL.Clear();
   mFilterR.Clear();

   mLFOPhase = 0.0f;
}

void ModelDVoice::Stop(double time)
{
   mAdsr.Stop(time);
   mFilterAdsr.Stop(time);
}

void ModelDVoice::ClearVoice()
{
   mAdsr.Clear();
   mFilterAdsr.Clear();
   mOsc1.mPhase = 0.0f;
   mOsc2.mPhase = 0.0f;
   mOsc3.mPhase = 0.0f;
   mLFOPhase = 0.0f;
   mPinkNoiseInit = false;
   mFilterL.Clear();
   mFilterR.Clear();
}

void ModelDVoice::SetVoiceParams(IVoiceParams* params)
{
   mVoiceParams = dynamic_cast<ModelDParams*>(params);
}

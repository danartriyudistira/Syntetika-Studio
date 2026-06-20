#include "SpatialObject.h"
#include "Spatial2DSpace.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "SynthGlobals.h"
#include "PatchCableSource.h"
#include <cmath>

SpatialObject::SpatialObject()
: IAudioProcessor(gBufferSize)
{
   mDistanceFilterL.SetFilterType(kFilterType_Lowpass);
   mDistanceFilterR.SetFilterType(kFilterType_Lowpass);
   mFrontBackFilterL.SetFilterType(kFilterType_LowShelf);
   mFrontBackFilterR.SetFilterType(kFilterType_LowShelf);
}

void SpatialObject::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
    mXSlider = new FloatSlider(this, "x (cm)", 5, 2, 120, 15, &mX, -2000, 2000);
   mYSlider = new FloatSlider(this, "y (cm)", 5, 20, 120, 15, &mY, -2000, 2000);
   mZSlider = new FloatSlider(this, "z (cm)", 5, 38, 120, 15, &mZ, 0, 1000);
}

SpatialObject::~SpatialObject()
{
   if (mRegisteredSpace)
      mRegisteredSpace->UnregisterObject(this);
}

void SpatialObject::Process(double time)
{
   PROFILER(SpatialObject);

   IAudioReceiver* target = GetTarget();

   if (!mEnabled)
   {
      if (target)
      {
         SyncBuffers();
         for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
         {
            Add(target->GetBuffer()->GetChannel(ch), GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize());
            GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize(), ch);
         }
      }
      GetBuffer()->Reset();
      return;
   }

   SyncBuffers();
   float* audioIn = GetBuffer()->GetChannel(0);
   int bufferSize = GetBuffer()->BufferSize();

   Spatial2DSpace* space = dynamic_cast<Spatial2DSpace*>(target);
   if (space && space != mRegisteredSpace)
   {
      if (mRegisteredSpace)
         mRegisteredSpace->UnregisterObject(this);
      space->RegisterObject(this);
      mRegisteredSpace = space;
   }
   else if (!space && mRegisteredSpace)
   {
      mRegisteredSpace->UnregisterObject(this);
      mRegisteredSpace = nullptr;
   }

   bool headphoneMode = mRegisteredSpace && mRegisteredSpace->GetOutputMode() == 1;

   float* outL = TheSynth->GetOutputBuffer(0);
   float* outR = TheSynth->GetOutputBuffer(1);

   if (mRegisteredSpace && !headphoneMode)
   {
      int numSpeakers = mRegisteredSpace->GetNumSpeakers();
      ofVec2f userPos = mRegisteredSpace->GetUserPosition();

      for (int spk = 0; spk < numSpeakers && spk < TheSynth->GetNumOutputChannels(); ++spk)
      {
         const auto& speakerPos = mRegisteredSpace->GetSpeakerPositions();
         float dx = speakerPos[spk].x - userPos.x;
         float dy = speakerPos[spk].y - userPos.y;
         float dist = std::sqrt(dx * dx + dy * dy);
         float invDist = 1.0f / (1.0f + dist * 0.003f);

         float objDx = mX - userPos.x;
         float objDy = mY - userPos.y;
         float objAngle = std::atan2(objDy, objDx);
         float spkAngle = std::atan2(dy, dx);
         float angleDiff = objAngle - spkAngle;
         while (angleDiff > FPI) angleDiff -= 2 * FPI;
         while (angleDiff < -FPI) angleDiff += 2 * FPI;

         float gain = std::max(0.0f, std::cos(angleDiff * 0.5f));
         gain = std::pow(gain, 1.5f);

         float* outBuffer = TheSynth->GetOutputBuffer(spk);
         if (outBuffer)
         {
            for (int s = 0; s < bufferSize; ++s)
               outBuffer[s] += audioIn[s] * gain * invDist;
         }
      }
   }
   else if (outL && outR)
   {
      ofVec2f userPos = mRegisteredSpace ? mRegisteredSpace->GetUserPosition() : ofVec2f(500.0f, 400.0f);
      float userEarZ = 170.0f;
      float objDx = mX - userPos.x;
      float objDy = mY - userPos.y;
      float objDz = mZ - userEarZ;

      float distance = std::sqrt(objDx * objDx + objDy * objDy + objDz * objDz);
      float distAttn = 1.0f / (1.0f + distance * 0.002f);

      bool useVirtualSpeakers = mRegisteredSpace && mRegisteredSpace->GetShowVirtualSpeakers();
      float roomWidth = mRegisteredSpace ? mRegisteredSpace->GetRoomWidth() : 600.0f;

      if (useVirtualSpeakers)
      {
         float splGain = std::pow(10.0f, (mRegisteredSpace->GetVirtualSpeakerSPL() - 85.0f) / 20.0f);
         int numSpeakers = mRegisteredSpace->GetNumSpeakers();
         const auto& speakerPos = mRegisteredSpace->GetSpeakerPositions();
         float objAngle = std::atan2(objDy, objDx);

         struct SpkData { float vbapGain, spkDistAttn, spkLeftGain, spkRightGain; };
         SpkData spkData[16];
         for (int spk = 0; spk < numSpeakers; ++spk)
         {
            float spkDx = speakerPos[spk].x - userPos.x;
            float spkDy = speakerPos[spk].y - userPos.y;
            float spkAngle = std::atan2(spkDy, spkDx);
            float angleDiff = objAngle - spkAngle;
            while (angleDiff > FPI) angleDiff -= 2 * FPI;
            while (angleDiff < -FPI) angleDiff += 2 * FPI;
            float vbapGain = std::max(0.0f, std::cos(angleDiff * 0.5f));
            spkData[spk].vbapGain = std::pow(vbapGain, 1.5f);

            float spkDist = std::sqrt(spkDx * spkDx + spkDy * spkDy);
            spkData[spk].spkDistAttn = 1.0f / (1.0f + spkDist * 0.002f);

            float speakerPan = ofClamp(spkDx / (roomWidth * 0.4f), -1, 1);
            float panAngle = (speakerPan + 1.0f) * FPI / 4.0f;
            spkData[spk].spkLeftGain = std::cos(panAngle);
            spkData[spk].spkRightGain = std::sin(panAngle);
         }

         for (int s = 0; s < bufferSize; ++s)
         {
            float sample = audioIn[s] * distAttn;
            float sumL = 0, sumR = 0;
            for (int spk = 0; spk < numSpeakers; ++spk)
            {
               float spkOut = sample * spkData[spk].vbapGain * splGain * spkData[spk].spkDistAttn;
               sumL += spkOut * spkData[spk].spkLeftGain;
               sumR += spkOut * spkData[spk].spkRightGain;
            }
            outL[s] += sumL;
            outR[s] += sumR;
         }
      }
      else
      {
         float pan = ofClamp(objDx / (roomWidth * 0.4f), -1, 1);
         float panAngle = (pan + 1.0f) * FPI / 4.0f;
         float leftGain = std::cos(panAngle);
         float rightGain = std::sin(panAngle);

         float lpFreq = 20000.0f * std::max(0.01f, 1.0f - distance * 0.001f);
         lpFreq = std::max(lpFreq, 200.0f);
         mDistanceFilterL.SetFilterParams(lpFreq, 0.707f);
         mDistanceFilterR.SetFilterParams(lpFreq, 0.707f);

         bool isBehind = (objDy > 0);
         if (isBehind)
         {
            float behindDepth = std::min(objDy / 200.0f, 1.0f);
            float dbCut = -4.0f - behindDepth * 6.0f;
            mFrontBackFilterL.SetFilterParams(800.0f, 0.707f);
            mFrontBackFilterL.mDbGain = dbCut;
            mFrontBackFilterR.SetFilterParams(800.0f, 0.707f);
            mFrontBackFilterR.mDbGain = dbCut;
         }
         else
         {
            float frontDepth = std::min(-objDy / 200.0f, 1.0f);
            float dbBoost = frontDepth * 3.0f;
            mFrontBackFilterL.SetFilterParams(4000.0f, 1.0f);
            mFrontBackFilterL.mDbGain = dbBoost;
            mFrontBackFilterR.SetFilterParams(4000.0f, 1.0f);
            mFrontBackFilterR.mDbGain = dbBoost;
         }
         mFrontBackFilterL.UpdateFilterCoeff();
         mFrontBackFilterR.UpdateFilterCoeff();

         for (int s = 0; s < bufferSize; ++s)
         {
            float sample = audioIn[s] * distAttn;
            sample = mDistanceFilterL.Filter(sample);
            sample = mFrontBackFilterL.Filter(sample);
            outL[s] += sample * leftGain;
            outR[s] += sample * rightGain;
         }
      }

      GetVizBuffer()->WriteChunk(outL, bufferSize, 0);
      GetVizBuffer()->WriteChunk(outR, bufferSize, 1);
   }

   if (target)
   {
      ChannelBuffer* targetBuf = target->GetBuffer();
      targetBuf->SetNumActiveChannels(2);
      float earZ = 170.0f;
      float dZ = mZ - earZ;
      for (int s = 0; s < bufferSize; ++s)
      {
         float send = audioIn[s] / (1.0f + std::sqrt(dZ * dZ) * 0.002f);
         targetBuf->GetChannel(0)[s] += send;
         targetBuf->GetChannel(1)[s] += send;
      }
   }

   GetBuffer()->Reset();
}

void SpatialObject::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   IAudioSource::PostRepatch(cableSource, fromUserClick);
   IAudioReceiver* target = GetTarget();
   Spatial2DSpace* space = dynamic_cast<Spatial2DSpace*>(target);
   if (space == nullptr && mRegisteredSpace)
   {
      mRegisteredSpace->UnregisterObject(this);
      mRegisteredSpace = nullptr;
   }
}

void SpatialObject::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mXSlider->Draw();
   mYSlider->Draw();
   mZSlider->Draw();
}

void SpatialObject::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
}

void SpatialObject::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("x", moduleInfo, 0.0f, mXSlider);
   mModuleSaveData.LoadFloat("y", moduleInfo, -200.0f, mYSlider);
   mModuleSaveData.LoadFloat("z", moduleInfo, 100.0f, mZSlider);
   SetUpFromSaveData();
}

void SpatialObject::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mX = mModuleSaveData.GetFloat("x");
   mY = mModuleSaveData.GetFloat("y");
   mZ = mModuleSaveData.GetFloat("z");
}

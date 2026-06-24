#include "TriggerWaveEffect.h"
#include "SynthGlobals.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include "ModularSynth.h"
#include "VisualFBO.h"

TriggerWaveEffect::TriggerWaveEffect()
: IAudioProcessor(gBufferSize)
{
   for (int i = 0; i < TRIGGERWAVE_ENV_HISTORY; ++i)
      mEnvHistory[i] = 0.001f;
   mAvgEnergy = 0.001f;
}

TriggerWaveEffect::~TriggerWaveEffect()
{
   delete mFBO;
}

void TriggerWaveEffect::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   UIBLOCK0();
   DROPDOWN(mEffectModeDropdown, "mode", &mEffectMode, 50);
   DROPDOWN(mColorDropdown, "color", &mColorSelect, 55);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mSensitivitySlider, "sensitivity", &mSensitivity, 0.5f, 5.0f);
   FLOATSLIDER(mIntensitySlider, "intensity", &mIntensity, 0.0f, 2.0f);
   ENDUIBLOCK(mWidth, mHeight);

   mEffectModeDropdown->AddLabel("pulse", 0);
   mEffectModeDropdown->AddLabel("glitch", 1);
   mEffectModeDropdown->AddLabel("scanlines", 2);
   mEffectModeDropdown->AddLabel("all", 3);

   mColorDropdown->AddLabel("cyan", 0);
   mColorDropdown->AddLabel("magenta", 1);
   mColorDropdown->AddLabel("green", 2);
   mColorDropdown->AddLabel("white", 3);
   mColorDropdown->AddLabel("rainbow", 4);
}

void TriggerWaveEffect::Resize(float w, float h)
{
   mWidth = ofClamp(w, 200, 2000);
   mHeight = ofClamp(h, 150, 1500);
}

void TriggerWaveEffect::Process(double time)
{
   PROFILER(TriggerWaveEffect);

   SyncBuffers();

   int bufferSize = GetBuffer()->BufferSize();
   IAudioReceiver* target = GetTarget();
   if (target)
   {
      for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
      {
         Add(target->GetBuffer()->GetChannel(ch), GetBuffer()->GetChannel(ch), bufferSize);
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize(), ch);
      }
   }

   if (mEnabled)
   {
      int ch = 0;
      for (int i = 0; i < bufferSize; ++i)
      {
         float sample = GetBuffer()->GetChannel(ch)[i];
         mBuffer[mWritePos] = sample;
         mWritePos = (mWritePos + 1) % TRIGGERWAVE_BUFFER_SIZE;
         if (mNumStored < TRIGGERWAVE_BUFFER_SIZE)
            ++mNumStored;

         CheckBeat(sample);
      }
   }

   GetBuffer()->Reset();
}

void TriggerWaveEffect::CheckBeat(float sample)
{
   float env = fabsf(sample);

   mEnvSum -= mEnvHistory[mEnvPos];
   mEnvHistory[mEnvPos] = env;
   mEnvSum += env;
   mEnvPos = (mEnvPos + 1) % TRIGGERWAVE_ENV_HISTORY;

   mLocalEnergy = env;
   mAvgEnergy = mEnvSum / TRIGGERWAVE_ENV_HISTORY;

   if (mBeatHold <= 0 && mLocalEnergy > mAvgEnergy * mSensitivity && mAvgEnergy > 0.001f)
   {
      mBeatFlash = 1.0f;
      mBeatHold = (int)(gSampleRate * 0.05f);
      ++mBeatCount;
      mGlitchSeed = ofRandom(0, 1000);
   }

   if (mBeatHold > 0)
      --mBeatHold;

   mBeatFlash *= 0.998f;
   if (mBeatFlash < 0.001f)
      mBeatFlash = 0;
}

void TriggerWaveEffect::PostRender()
{
   if (!mEnabled || mWidth < 20 || mHeight < 50)
      return;

   float areaY = 42;
   float areaH = mHeight - areaY - 4;
   if (areaH < 10)
      return;

   if (!mFBO || !mFBO->IsValid() ||
       mFBO->GetWidth() != (int)mWidth ||
       mFBO->GetHeight() != (int)mHeight)
   {
      delete mFBO;
      mFBO = new VisualFBO();
      mFBO->Create(std::max(64, (int)mWidth), std::max(64, (int)mHeight));
   }

   if (!mFBO || !mFBO->IsValid())
      return;

   mFBO->Bind();

   ofSetColor(10, 10, 15, 255);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   switch (mEffectMode)
   {
   case 0: DrawPulse(mWidth, areaH); break;
   case 1: DrawGlitch(mWidth, areaH); break;
   case 2: DrawScanlines(mWidth, areaH); break;
   case 3:
   default:
      DrawPulse(mWidth, areaH);
      DrawGlitch(mWidth, areaH);
      DrawScanlines(mWidth, areaH);
      break;
   }

   DrawFlash(mWidth, mHeight);

   mFBO->Unbind();
}

namespace
{
   void GetTWColor(int colorSelect, int beatCount, float& cr, float& cg, float& cb)
   {
      switch (colorSelect)
      {
      case 0: cr=0;   cg=1;   cb=1;   break;
      case 1: cr=1;   cg=0.2f; cb=1;  break;
      case 2: cr=0;   cg=1;   cb=0;   break;
      case 3: cr=1;   cg=1;   cb=1;   break;
      case 4:
      default:
      {
         float hue = fmodf(beatCount * 30.0f, 360.0f);
         float h = hue / 60.0f;
         float f = h - floorf(h);
         float q = 1.0f - f;
         int hi = (int)h % 6;
         switch (hi)
         {
         case 0: cr=1; cg=f; cb=0; break;
         case 1: cr=q; cg=1; cb=0; break;
         case 2: cr=0; cg=1; cb=f; break;
         case 3: cr=0; cg=q; cb=1; break;
         case 4: cr=f; cg=0; cb=1; break;
         case 5: cr=1; cg=0; cb=q; break;
         default: cr=1; cg=1; cb=1;
         }
         break;
      }
      }
   }
}

void TriggerWaveEffect::DrawBaseWaveform(float w, float h)
{
   if (mNumStored < 2)
      return;

   ofSetLineWidth(2);
   ofNoFill();

   float cr=1, cg=1, cb=1;
   int numDraw = ofClamp(mNumStored, 2, TRIGGERWAVE_BUFFER_SIZE);
   int startIdx = (mWritePos - numDraw + TRIGGERWAVE_BUFFER_SIZE) % TRIGGERWAVE_BUFFER_SIZE;

   ofBeginShape();
   for (int i = 0; i < numDraw; ++i)
   {
      int idx = (startIdx + i) % TRIGGERWAVE_BUFFER_SIZE;
      GetTWColor(mColorSelect, mBeatCount + i, cr, cg, cb);
      ofSetColor((int)(cr*255), (int)(cg*255), (int)(cb*255), 200);

      float sx = (float)i / numDraw * w;
      float sy = h * 0.5f + mBuffer[idx] * h * 0.4f;
      sy = ofClamp(sy, 0, h);
      ofVertex(sx, sy);
   }
   ofEndShape(false);
}

void TriggerWaveEffect::DrawPulse(float w, float h)
{
   DrawBaseWaveform(w, h);

   if (mBeatFlash > 0.01f)
   {
      float radius = (1.0f - mBeatFlash) * fminf(w, h) * 0.4f * mIntensity;
      float alpha = mBeatFlash * 150;
      float cr=1, cg=1, cb=1;
      GetTWColor(mColorSelect, mBeatCount, cr, cg, cb);

      ofSetColor((int)(cr*255), (int)(cg*255), (int)(cb*255), (int)alpha);
      ofNoFill();
      ofSetLineWidth(3);
      ofCircle(w * 0.5f, h * 0.5f, radius);
   }
}

void TriggerWaveEffect::DrawGlitch(float w, float h)
{
   if (mBeatFlash < 0.01f)
   {
      DrawBaseWaveform(w, h);
      return;
   }

   float intensity = mBeatFlash * mIntensity;
   int numStrips = (int)(5 + intensity * 15);
   float stripH = h / numStrips;

   for (int s = 0; s < numStrips; ++s)
   {
      float offset = 0;
      if (ofRandom(0, 1) < intensity * 0.5f)
         offset = (ofRandom(-1, 1)) * w * 0.1f * intensity;

      float cr=1, cg=1, cb=1;
      GetTWColor(mColorSelect, mBeatCount + s * 10, cr, cg, cb);

      float y0 = s * stripH;
      float y1 = y0 + stripH;

      ofFill();
      if (ofRandom(0, 1) < intensity * 0.3f)
         ofSetColor((int)(cr*80*intensity), (int)(cg*80*intensity), (int)(cb*80*intensity), (int)(100*intensity));
      else
         ofSetColor(0, 0, 0, 200);
      ofRect(0, y0, w, stripH);

      if (mNumStored >= 2)
      {
         ofSetLineWidth(1.5f);
         ofNoFill();
         int numDraw = ofClamp(mNumStored, 2, TRIGGERWAVE_BUFFER_SIZE);
         int startIdx = (mWritePos - numDraw + TRIGGERWAVE_BUFFER_SIZE) % TRIGGERWAVE_BUFFER_SIZE;

         ofBeginShape();
         for (int i = 0; i < numDraw; ++i)
         {
            int idx = (startIdx + i) % TRIGGERWAVE_BUFFER_SIZE;
            float sx = (float)i / numDraw * w + offset;
            float sy = y0 + stripH * 0.5f + mBuffer[idx] * stripH * 0.3f;
            sy = ofClamp(sy, y0, y1);

            GetTWColor(mColorSelect, mBeatCount + s * 10 + i, cr, cg, cb);
            ofSetColor((int)(cr*255), (int)(cg*255), (int)(cb*255), (int)(150*intensity));
            ofVertex(sx, sy);
         }
         ofEndShape(false);
      }
   }
}

void TriggerWaveEffect::DrawScanlines(float w, float h)
{
   DrawBaseWaveform(w, h);

   float intensity = mBeatFlash * mIntensity;
   int numLines = (int)(20 + intensity * 40);
   float lineSpacing = h / numLines;

   float cr=1, cg=1, cb=1;
   GetTWColor(mColorSelect, mBeatCount, cr, cg, cb);

   ofSetLineWidth(1);
   for (int i = 0; i < numLines; ++i)
   {
      float alpha = 30 + intensity * 80 * (0.5f + 0.5f * sinf(i * 0.5f + mBeatCount * 2.0f));
      float y = i * lineSpacing;
      ofSetColor((int)(cr*alpha), (int)(cg*alpha), (int)(cb*alpha), (int)alpha);
      ofLine(0, y, w, y);
   }
}

void TriggerWaveEffect::DrawFlash(float w, float h)
{
   if (mBeatFlash < 0.01f)
      return;

   float alpha = mBeatFlash * 60 * mIntensity;
   if (alpha < 1)
      return;

   float cr=1, cg=1, cb=1;
   GetTWColor(mColorSelect, mBeatCount, cr, cg, cb);

   ofSetColor((int)(cr*alpha), (int)(cg*alpha), (int)(cb*alpha), (int)alpha);
   ofFill();
   ofRect(0, 0, w, h);
}

void TriggerWaveEffect::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   if (mFBO && mFBO->IsValid())
      mFBO->Draw(0, 0, mWidth, mHeight);

   ofPushStyle();
   ofSetColor(80, 80, 80, 100);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   mEffectModeDropdown->Draw();
   mColorDropdown->Draw();
   mSensitivitySlider->Draw();
   mIntensitySlider->Draw();

   ofPushStyle();
   float beatR = 4;
   float beatX = mWidth - 12;
   float beatY = 10;
   if (mBeatFlash > 0.01f)
      ofSetColor(255, 200, 50, (int)(mBeatFlash * 255));
   else
      ofSetColor(60, 60, 60, 100);
   ofFill();
   ofCircle(beatX, beatY, beatR);
   ofPopStyle();

   ofPushStyle();
   ofSetColor(180, 180, 180);
   DrawTextRightJustify("beats: " + ofToString(mBeatCount), mWidth - 20, mHeight - 10);
   ofPopStyle();
}

VisualFBO* TriggerWaveEffect::GetFBO()
{
   return mFBO;
}

void TriggerWaveEffect::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("sensitivity", moduleInfo, 1.5f, 0.5f, 5.0f, true);
   mModuleSaveData.LoadFloat("intensity", moduleInfo, 1.0f, 0.0f, 2.0f, true);
   mModuleSaveData.LoadInt("mode", moduleInfo, 3, 0, 3, true);
   mModuleSaveData.LoadInt("color", moduleInfo, 0, 0, 4, true);

   SetUpFromSaveData();
}

void TriggerWaveEffect::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["target"] = mModuleSaveData.GetString("target");
   moduleInfo["sensitivity"] = mSensitivity;
   moduleInfo["intensity"] = mIntensity;
   moduleInfo["mode"] = mEffectMode;
   moduleInfo["color"] = mColorSelect;
}

void TriggerWaveEffect::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mSensitivity = mModuleSaveData.GetFloat("sensitivity");
   mIntensity = mModuleSaveData.GetFloat("intensity");
   mEffectMode = mModuleSaveData.GetInt("mode");
   mColorSelect = mModuleSaveData.GetInt("color");
}

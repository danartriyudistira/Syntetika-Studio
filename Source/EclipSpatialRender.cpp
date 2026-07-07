#include "EclipSpatialRender.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include <cmath>
#include <algorithm>
#include "OpenFrameworksPort.h"
#include "VisualFBO.h"
#include "PatchCableSource.h"
#include "SynthGlobals.h"

EclipSpatialRender::EclipSpatialRender()
: IAudioProcessor(gBufferSize)
{
   mObjects.resize(kMaxObjects);
}

EclipSpatialRender::~EclipSpatialRender()
{
   delete mFBO;
}

int EclipSpatialRender::GetNumOutputChannels() const
{
   switch (mSpeakerLayout)
   {
   case kSpeakerLayout_Stereo:      return 2;
   case kSpeakerLayout_5_1:         return 6;
   case kSpeakerLayout_7_1:         return 8;
   case kSpeakerLayout_5_1_2:       return 8;
   case kSpeakerLayout_5_1_4:       return 10;
   case kSpeakerLayout_7_1_4:       return 12;
   case kSpeakerLayout_Ambisonics1: return 4;
   case kSpeakerLayout_Ambisonics3: return 16;
   default:                         return 2;
   }
}

void EclipSpatialRender::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mVisualCable = new PatchCableSource(this, kConnectionType_Special);
   mVisualCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
   mVisualCable->SetManualSide(PatchCableSource::Side::kRight);
   mVisualCable->SetManualPosition(mWidth - 8, mHeight / 2);
   AddPatchCableSource(mVisualCable);

   UIBLOCK0();
   FLOATSLIDER(mMasterVolumeSlider, "master vol", &mMasterVolume, 0, 2);
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mSpeakerLayoutDropdown, "layout", &mSpeakerLayout, 80);
   ENDUIBLOCK(mHeight);

   mSpeakerLayoutDropdown->AddLabel("Stereo", kSpeakerLayout_Stereo);
   mSpeakerLayoutDropdown->AddLabel("5.1", kSpeakerLayout_5_1);
   mSpeakerLayoutDropdown->AddLabel("7.1", kSpeakerLayout_7_1);
   mSpeakerLayoutDropdown->AddLabel("5.1.2", kSpeakerLayout_5_1_2);
   mSpeakerLayoutDropdown->AddLabel("5.1.4", kSpeakerLayout_5_1_4);
   mSpeakerLayoutDropdown->AddLabel("7.1.4", kSpeakerLayout_7_1_4);
   mSpeakerLayoutDropdown->AddLabel("Ambi 1", kSpeakerLayout_Ambisonics1);
   mSpeakerLayoutDropdown->AddLabel("Ambi 3", kSpeakerLayout_Ambisonics3);

   UIBLOCK0();
   UIBLOCK_PUSHSLIDERWIDTH(50);
   FLOATSLIDER(mDistanceMinSlider, "dist min", &mDistanceMin, 0, 1);
   FLOATSLIDER(mDistanceMaxSlider, "dist max", &mDistanceMax, 0, 2);
   FLOATSLIDER(mRolloffSlider, "rolloff", &mRolloff, 0, 4);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mReverbMixSlider, "reverb mix", &mReverbMix, 0, 1);
   FLOATSLIDER(mReverbDecaySlider, "decay", &mReverbDecay, 0, 1);
   FLOATSLIDER(mReverbSizeSlider, "size", &mReverbSize, 0, 1);
   FLOATSLIDER(mReverbDampingSlider, "damping", &mReverbDamping, 0, 1);
   mRoomControlWidth = UIBLOCKWIDTH();
   mRoomControlHeight = UIBLOCKHEIGHT();
   ENDUIBLOCK0();

   UIBLOCK0();
   mHRTFEnabledCheckbox = new Checkbox(this, "HRTF", 5, 130, &mHRTFEnabled);
   mHRTFQualityDropdown = new DropdownList(this, "hrtf quality", 60, 130, &mHRTFQuality);
   mHRTFQualityDropdown->AddLabel("ITD", 0);
   mHRTFQualityDropdown->AddLabel("ITD+ILD", 1);
   mHRTFQualityDropdown->AddLabel("Full", 2);
   mHeadRadiusSlider = new FloatSlider(this, "head radius", 160, 130, 100, 15, &mHeadRadius, 5, 15, 2);
   ENDUIBLOCK0();
}

void EclipSpatialRender::Process(double time)
{
   PROFILER(EclipSpatialRender);

   IAudioReceiver* target = GetTarget();
   if (target == nullptr)
      return;

   UpdateAnimations(time);

   int bufferSize = GetBuffer()->BufferSize();
   int numChannels = GetNumOutputChannels();

   target->GetBuffer()->SetNumActiveChannels(numChannels);

   std::vector<float*> outputs(numChannels);
   for (int ch = 0; ch < numChannels; ++ch)
      outputs[ch] = target->GetBuffer()->GetChannel(ch);

   for (int ch = 0; ch < numChannels; ++ch)
   {
      for (int s = 0; s < bufferSize; ++s)
         outputs[ch][s] = 0;
   }

   if (mHRTFEnabled && numChannels >= 2)
   {
      memset(mBinauralL, 0, bufferSize * sizeof(float));
      memset(mBinauralR, 0, bufferSize * sizeof(float));
   }

   for (int i = 0; i < kMaxObjects; ++i)
   {
      const ObjectData& obj = mObjects[i];
      if (!obj.mActive || obj.mMuted)
         continue;

      int needed = bufferSize * 2;
      if (obj.mAudioBuffer.size() < needed)
         continue;

      SpatializeObject(i, outputs.data(), numChannels, bufferSize);

      if (mHRTFEnabled && numChannels >= 2)
         ProcessHRTF(i, outputs.data(), numChannels, bufferSize);
   }

   if (mHRTFEnabled && numChannels >= 2)
   {
      for (int s = 0; s < bufferSize; ++s)
      {
         outputs[0][s] = mBinauralL[s];
         outputs[1][s] = mBinauralR[s];
      }
   }

   if (mReverbMix > 0 && numChannels >= 2)
   {
      ProcessReverb(outputs[0], outputs[1], bufferSize);
   }

   GetVizBuffer()->WriteChunk(outputs[0], bufferSize, 0);
   if (numChannels > 1)
      GetVizBuffer()->WriteChunk(outputs[1], bufferSize, 1);

   GetBuffer()->Reset();
}

void EclipSpatialRender::SpatializeObject(int index, float** outputs, int numChannels, int bufferSize)
{
   const ObjectData& obj = mObjects[index];
   const float* buf = obj.mAudioBuffer.data();

   float dist = sqrtf(obj.mX * obj.mX + obj.mY * obj.mY);
   float clampedDist = ofClamp(dist, mDistanceMin, mDistanceMax);
   float distNorm = (clampedDist - mDistanceMin) / (mDistanceMax - mDistanceMin + 0.001f);
   float distGain = powf(1.0f - distNorm, mRolloff);
   distGain = ofClamp(distGain, 0, 1);

   float occlusionLP = ofClamp(1.0f - obj.mOcclusion, 0, 1);
   if (obj.mZ < 0)
      occlusionLP *= ofClamp(1.0f + obj.mZ * 0.5f, 0.1f, 1.0f);

   float vol = obj.mVolume * mMasterVolume * distGain;

   float pan = ofClamp(obj.mX, -1.0f, 1.0f);
   float leftGain = cosf((pan + 1.0f) * PI / 4.0f);
   float rightGain = sinf((pan + 1.0f) * PI / 4.0f);
   float zGain = ofClamp(1.0f - fabs(obj.mZ) * 0.3f, 0.5f, 1.0f);

   if (numChannels == 2)
   {
      for (int s = 0; s < bufferSize; ++s)
      {
         float sL = buf[s * 2] * vol * zGain;
         float sR = buf[s * 2 + 1] * vol * zGain;
         outputs[0][s] += sL * leftGain + sR * leftGain * 0.5f;
         outputs[1][s] += sR * rightGain + sL * rightGain * 0.5f;
      }
   }
   else
   {
      float frontGain = ofClamp(1.0f - fabs(obj.mY) * 0.7f, 0.3f, 1.0f);
      float rearGain = ofClamp(fabs(obj.mY) * 0.7f, 0, 0.7f);
      float centerGain = ofClamp(1.0f - fabs(pan) * 2.0f, 0, 1) * frontGain;
      float sideGain = ofClamp(fabs(pan), 0.3f, 1.0f) * 0.5f;

      for (int s = 0; s < bufferSize; ++s)
      {
         float sL = buf[s * 2];
         float sR = buf[s * 2 + 1];
         float mono = (sL + sR) * 0.5f;
         float dryL = sL * vol * leftGain * zGain;
         float dryR = sR * vol * rightGain * zGain;
         float dryC = mono * vol * centerGain;
         float dryLFE = mono * vol * 0.1f;

         outputs[0][s] += dryL;
         outputs[1][s] += dryR;
         if (numChannels >= 3) outputs[2][s] += dryC;
         if (numChannels >= 4) outputs[3][s] += dryLFE;
         if (numChannels >= 5) outputs[4][s] += sL * vol * rearGain * 0.6f;
         if (numChannels >= 6) outputs[5][s] += sR * vol * rearGain * 0.6f;
         if (numChannels >= 7) outputs[6][s] += sL * vol * sideGain;
         if (numChannels >= 8)
         {
            outputs[7][s] += sR * vol * sideGain;
            if (numChannels >= 9)
            {
               float hGain = ofClamp(obj.mZ * 0.5f + 0.5f, 0, 1) * 0.5f;
               outputs[8][s] += sL * vol * hGain;
               if (numChannels >= 10) outputs[9][s] += sR * vol * hGain;
               if (numChannels >= 11) outputs[10][s] += mono * vol * hGain;
               if (numChannels >= 12) outputs[11][s] += mono * vol * hGain;
            }
         }
      }
   }
}

void EclipSpatialRender::ProcessHRTF(int index, float** outputs, int numChannels, int bufferSize)
{
   const ObjectData& obj = mObjects[index];
   const float* buf = obj.mAudioBuffer.data();

   float dx = obj.mX;
   float dy = obj.mY;
   float dz = obj.mZ;
   float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
   float azimuth = std::atan2(dx, dy);
   float elevation = std::atan2(dz, std::sqrt(dx * dx + dy * dy));
   float headAz = azimuth;
   if (headAz > FPI * 0.5f)
      headAz = FPI - headAz;
   else if (headAz < -FPI * 0.5f)
      headAz = -FPI - headAz;

   float distGain = 1.0f / (1.0f + dist * 0.002f);
   float c = 34300.0f;
   float cosElev = std::cos(elevation);
   float itdSec = (mHeadRadius / c) * (headAz + std::sin(headAz)) * cosElev;
   if (dist < 10.0f)
      itdSec *= 1.0f + (10.0f - dist) / 10.0f * 0.1f;
   float itdAbs = std::fabs(itdSec);
   int itdSamp = (int)(itdAbs * gSampleRate);
   float itdFrac = (itdAbs * gSampleRate) - itdSamp;
   itdSamp = ofClamp(itdSamp, 0, 220);

   for (int s = 0; s < bufferSize; ++s)
   {
      float vol = obj.mVolume * mMasterVolume * distGain;
      float smp = (buf[s * 2] + buf[s * 2 + 1]) * 0.5f * vol;

      int wp = obj.mHRTF.delayWritePos;
      const_cast<ObjectData&>(obj).mHRTF.delayLineL[wp] = smp;
      const_cast<ObjectData&>(obj).mHRTF.delayLineR[wp] = smp;
      int writtenPos = wp;
      wp = (wp + 1) & 255;
      const_cast<ObjectData&>(obj).mHRTF.delayWritePos = wp;

      float smpL, smpR;
      if (itdSamp == 0)
      {
         smpL = obj.mHRTF.delayLineL[writtenPos];
         smpR = obj.mHRTF.delayLineR[writtenPos];
      }
      else if (headAz > 0.01f)
      {
         int rp = (writtenPos - itdSamp) & 255;
         int rp2 = (writtenPos - itdSamp - 1) & 255;
         smpL = obj.mHRTF.delayLineL[rp] * (1.0f - itdFrac) + obj.mHRTF.delayLineL[rp2] * itdFrac;
         smpR = obj.mHRTF.delayLineR[writtenPos];
      }
      else if (headAz < -0.01f)
      {
         int rp = (writtenPos - itdSamp) & 255;
         int rp2 = (writtenPos - itdSamp - 1) & 255;
         smpL = obj.mHRTF.delayLineL[writtenPos];
         smpR = obj.mHRTF.delayLineR[rp] * (1.0f - itdFrac) + obj.mHRTF.delayLineR[rp2] * itdFrac;
      }
      else
      {
         smpL = smp;
         smpR = smp;
      }

      if (mHRTFQuality >= 1)
      {
         float ildNorm = 2.0f * std::fabs(headAz) / FPI * std::max(0.0f, cosElev);
         float ildGain = 1.0f - ildNorm * 0.6f;
         if (headAz > 0.01f)
            smpL *= ildGain;
         else if (headAz < -0.01f)
            smpR *= ildGain;
      }

      mBinauralL[s] += smpL;
      mBinauralR[s] += smpR;
   }
}

void EclipSpatialRender::ProcessReverb(float* outL, float* outR, int bufferSize)
{
   int delayLen = (int)(1000 + mReverbSize * 4000);
   delayLen = ofClamp(delayLen, 100, kReverbMaxDelay - bufferSize);
   int delayOffR = delayLen / 3;

   for (int s = 0; s < bufferSize; ++s)
   {
      int readL = (mReverbWritePos - delayLen + kReverbMaxDelay) % kReverbMaxDelay;
      int readR = (mReverbWritePos - delayLen + delayOffR + kReverbMaxDelay) % kReverbMaxDelay;

      float wetL = mReverbDelayL[readL];
      float wetR = mReverbDelayR[readR];

      float damp = ofClamp(1.0f - mReverbDamping, 0, 1);
      wetL += (outL[s] - wetL) * damp * 0.3f;
      wetR += (outR[s] - wetR) * damp * 0.3f;

      float inputL = outL[s] * mReverbMix;
      float inputR = outR[s] * mReverbMix;

      mReverbDelayL[mReverbWritePos] = inputL + wetL * mReverbDecay;
      mReverbDelayR[(mReverbWritePos + delayOffR) % kReverbMaxDelay] = inputR + wetR * mReverbDecay;

      outL[s] += wetL * (1.0f - mReverbMix);
      outR[s] += wetR * (1.0f - mReverbMix);

      mReverbWritePos = (mReverbWritePos + 1) % kReverbMaxDelay;
   }
}

void EclipSpatialRender::UpdateAnimations(double time)
{
   static double lastTime = 0;
   double dt = (time - lastTime) / 1000.0;
   if (dt < 0 || dt > 0.1)
      dt = 0.001;
   lastTime = time;

   for (int i = 0; i < kMaxObjects; ++i)
   {
      ObjectData& obj = mObjects[i];
      if (!obj.mActive) continue;
      if (obj.mAnim.mMode == kAnim_Static) continue;

      obj.mAnim.mPhase += obj.mAnim.mRate * dt * 2.0f * PI;
      if (obj.mAnim.mPhase > PI * 2)
         obj.mAnim.mPhase -= PI * 2;

      float d = obj.mAnim.mDepth;
      float p = obj.mAnim.mPhase;

      switch (obj.mAnim.mMode)
      {
      case kAnim_Orbit:
         obj.mX = cosf(p) * d;
         obj.mZ = sinf(p) * d;
         break;
      case kAnim_LFO_X:
         obj.mX = sinf(p) * d;
         break;
      case kAnim_LFO_XY:
         obj.mX = sinf(p) * d;
         obj.mY = cosf(p * 0.7f) * d * 0.5f;
         break;
      case kAnim_LFO_XYZ:
         obj.mX = sinf(p) * d;
         obj.mY = sinf(p * 0.7f + 1.0f) * d * 0.5f;
         obj.mZ = cosf(p * 0.5f) * d * 0.5f;
         break;
      default: break;
      }
   }
}

void EclipSpatialRender::SetObjectAudio(int index, const float* left, const float* right, int bufferSize,
                                        float x, float y, float z, float volume)
{
   if (index < 0 || index >= kMaxObjects)
      return;

   ObjectData& obj = mObjects[index];
   obj.mActive = true;
   if (obj.mAnim.mMode == kAnim_Static)
   {
      obj.mX = x;
      obj.mY = y;
      obj.mZ = z;
   }
   if (volume >= 0)
      obj.mVolume = volume;

   int needed = bufferSize * 2;
   if (obj.mAudioBuffer.size() < needed)
      obj.mAudioBuffer.resize(needed);

   for (int i = 0; i < bufferSize; ++i)
   {
      obj.mAudioBuffer[i * 2] = left[i];
      obj.mAudioBuffer[i * 2 + 1] = right ? right[i] : left[i];
   }
}

void EclipSpatialRender::SetObjectProperties(int index, int colorHue, float occlusion,
                                              AnimMode animMode, float animRate, float animDepth)
{
   if (index < 0 || index >= kMaxObjects)
      return;
   ObjectData& obj = mObjects[index];
   obj.mColorHue = colorHue;
   obj.mOcclusion = occlusion;
   obj.mAnim.mMode = animMode;
   obj.mAnim.mRate = animRate;
   obj.mAnim.mDepth = animDepth;
}

int EclipSpatialRender::GetNumActiveObjects() const
{
   int count = 0;
   for (int i = 0; i < kMaxObjects; ++i)
   {
      if (mObjects[i].mActive)
         ++count;
   }
   return count;
}

const EclipSpatialRender::ObjectData* EclipSpatialRender::GetObject(int index) const
{
   if (index < 0 || index >= kMaxObjects)
      return nullptr;
   return &mObjects[index];
}

int EclipSpatialRender::GetNumSpatialSources() const
{
   return GetNumActiveObjects();
}

bool EclipSpatialRender::GetSpatialSourceInfo(int index, SpatialSourceInfo& out) const
{
   const ObjectData* obj = GetObject(index);
   if (!obj || !obj->mActive)
      return false;
   out.x = obj->mX;
   out.y = obj->mY;
   out.z = obj->mZ;
   out.volume = obj->mVolume;
   out.colorHue = obj->mColorHue;
   out.occlusion = obj->mOcclusion;
   snprintf(out.name, sizeof(out.name), "%d", index);
   return true;
}

bool EclipSpatialRender::GetSpatialRoomInfo(SpatialRoomInfo& out) const
{
   out.roomW = 2;
   out.roomD = 2;
   out.roomH = 2;
   out.listenerX = 0;
   out.listenerY = 0;
   out.listenerZ = 0;
   out.numSpeakers = GetNumSpeakers();
   out.hrtfEnabled = mHRTFEnabled;
   out.hrtfQuality = mHRTFQuality;
   return true;
}

int EclipSpatialRender::GetNumSpeakers() const
{
   return GetNumOutputChannels();
}

bool EclipSpatialRender::GetSpeakerInfo(int index, SpatialSpeakerInfo& out) const
{
   int numCh = GetNumOutputChannels();
   if (index < 0 || index >= numCh)
      return false;

   out.x = 0;
   out.y = 0;
   out.z = 0;
   out.channel = index;

   int numSide = numCh / 2;
   float angleStep = PI / (numSide + 1);
   float angle = -PI * 0.5f + (index % numSide + 1) * angleStep;
   if (index < numSide)
   {
      out.x = cosf(angle);
      out.y = sinf(angle);
   }
   else
   {
      out.x = cosf(angle + PI);
      out.y = sinf(angle + PI) * 0.5f;
   }
   out.z = 0;
   return true;
}

void EclipSpatialRender::PostRender()
{
   if (!mEnabled || mWidth < 10 || mHeight < 10)
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

   ofPushStyle();

   ofSetColor(10, 10, 15);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   float cx = mWidth * 0.5f;
   float horizonY = mHeight * 0.55f;
   float groundBottom = mHeight - 10;
   float groundH = groundBottom - horizonY;

   for (int y = (int)horizonY; y < (int)groundBottom; ++y)
   {
      float t = (y - horizonY) / groundH;
      int c = (int)(15 + t * 20);
      ofSetColor(c, c, c + 5);
      ofLine(0, y, mWidth, y);
   }

   for (int y = 0; y < (int)horizonY; ++y)
   {
      float t = y / horizonY;
      int c = (int)(5 + t * 10);
      ofSetColor(c, c, c + 10);
      ofLine(0, y, mWidth, y);
   }

   ofSetLineWidth(1);
   int gridLines = 8;
   for (int i = -gridLines; i <= gridLines; ++i)
   {
      float xDepth = i / (float)gridLines;
      float sx = cx + xDepth * mWidth * 0.35f;
      float ex = cx + xDepth * mWidth * 0.35f * 0.3f;
      int alpha = (int)(ofClamp(1.0f - fabs(xDepth), 0, 1) * 40);
      ofSetColor(60, 60, 80, alpha);
      ofLine(sx, groundBottom, ex, horizonY);
   }
   for (int j = 0; j <= 8; ++j)
   {
      float t = j / 8.0f;
      float sy = groundBottom - t * groundH;
      float shrink = 1.0f - t * 0.7f;
      float hw = mWidth * 0.35f * shrink;
      ofSetColor(50, 50, 70, (int)(t * 30));
      ofLine(cx - hw, sy, cx + hw, sy);
   }

   struct ObjSort { int index; float y; };
   ObjSort sorted[kMaxObjects];
   int numSorted = 0;
   for (int i = 0; i < kMaxObjects; ++i)
   {
      if (mObjects[i].mActive)
      {
         sorted[numSorted].index = i;
         sorted[numSorted].y = mObjects[i].mY;
         ++numSorted;
      }
   }
   for (int i = 0; i < numSorted - 1; ++i)
   {
      for (int j = 0; j < numSorted - 1 - i; ++j)
      {
         if (sorted[j].y < sorted[j + 1].y)
         {
            ObjSort tmp = sorted[j];
            sorted[j] = sorted[j + 1];
            sorted[j + 1] = tmp;
         }
      }
   }

   for (int si = 0; si < numSorted; ++si)
   {
      const ObjectData& obj = mObjects[sorted[si].index];

      float depthT = (obj.mY + 1.0f) * 0.5f;
      float perspScale = 1.0f - depthT * 0.6f;
      float groundY = groundBottom - depthT * groundH;
      float screenX = cx + obj.mX * mWidth * 0.35f * perspScale;
      float screenY = groundY - obj.mZ * 30.0f * perspScale;

      float baseRadius = ofClamp(obj.mVolume * 20, 8, 40);
      float radius = baseRadius * perspScale;

      ofColor col = GetObjectColor(obj);

      if (obj.mAnim.mMode != kAnim_Static)
      {
         ofSetColor(col.r, col.g, col.b, (int)(60 * perspScale));
         ofNoFill();
         ofSetLineWidth(1);
         ofCircle(screenX, screenY, radius + 8 + obj.mAnim.mDepth * 6);
      }

      ofSetColor(0, 0, 0, (int)(80 * perspScale));
      ofFill();
      ofCircle(screenX * 0.7f + cx * 0.3f, groundY + 2, radius * 0.6f);

      ofSetColor(col.r, col.g, col.b, (int)(60 * perspScale));
      ofSetLineWidth(1);
      ofLine(screenX, groundY, screenX, screenY);

      ofSetColor(col.r, col.g, col.b, (int)(40 * perspScale));
      ofFill();
      ofCircle(screenX, screenY, radius + 6);

      ofSetColor(col.r, col.g, col.b, (int)(200 * perspScale));
      ofFill();
      ofCircle(screenX, screenY, radius);

      ofSetColor(255, 255, 255, (int)(100 * perspScale));
      ofFill();
      ofCircle(screenX - radius * 0.25f, screenY - radius * 0.25f, radius * 0.35f);

      ofSetColor(col.r, col.g, col.b, (int)(220 * perspScale));
      ofSetLineWidth(1.5f * perspScale);
      ofNoFill();
      ofCircle(screenX, screenY, radius);

      char label[32];
      snprintf(label, sizeof(label), "%d", sorted[si].index);
      ofSetColor(255, 255, 255, (int)(200 * perspScale));
      ofFill();
      gFont.DrawString(label, (int)(11 * perspScale), screenX - 4, screenY + 4);

      if (obj.mOcclusion > 0.5f || obj.mZ < -0.3f)
      {
         ofSetColor(255, 0, 0, (int)(150 * perspScale));
         ofSetLineWidth(2);
         ofLine(screenX - radius * 0.6f, screenY - radius * 0.6f,
                screenX + radius * 0.6f, screenY + radius * 0.6f);
         ofLine(screenX + radius * 0.6f, screenY - radius * 0.6f,
                screenX - radius * 0.6f, screenY + radius * 0.6f);
      }
   }

   ofPopStyle();
   mFBO->Unbind();
}

ofColor EclipSpatialRender::GetObjectColor(const ObjectData& obj) const
{
   int hue = obj.mColorHue;
   if (hue < 0)
      return ofColor(180, 180, 180);

   float h = (hue % 360) / 360.0f;
   float r, g, b;
   float f = h * 6.0f - floorf(h * 6.0f);
   int i = (int)floorf(h * 6.0f);
   float p = 1.0f - f;
   switch (i % 6)
   {
   case 0: r = 1; g = f; b = 0; break;
   case 1: r = p; g = 1; b = 0; break;
   case 2: r = 0; g = 1; b = f; break;
   case 3: r = 0; g = p; b = 1; break;
   case 4: r = f; g = 0; b = 1; break;
   default: r = 1; g = 0; b = p; break;
   }
   return ofColor((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

VisualFBO* EclipSpatialRender::GetFBO()
{
   return mFBO;
}

void EclipSpatialRender::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   if (mFBO && mFBO->IsValid())
   {
      mFBO->Draw(0, 0, mWidth, mHeight);

      const char* layoutNames[] = { "Stereo", "5.1", "7.1", "5.1.2", "5.1.4", "7.1.4", "Ambi1", "Ambi3" };
      ofSetColor(180, 180, 200, 180);
      gFont.DrawString(layoutNames[mSpeakerLayout], 11, 6, 14);

      char buf[64];
      snprintf(buf, sizeof(buf), "%d obj", GetNumActiveObjects());
      ofSetColor(180, 180, 200, 180);
      gFont.DrawString(buf, 11, mWidth - 60, 14);
   }

   mMasterVolumeSlider->Draw();
   mSpeakerLayoutDropdown->Draw();

   float roomX = 5;
   float roomY = 50;
   ofSetColor(0, 0, 0, 180);
   ofFill();
   ofRect(roomX - 3, roomY - 12, mRoomControlWidth + 6, mRoomControlHeight + 14);

   ofSetColor(140, 140, 160, 200);
   gFont.DrawString("room", 11, roomX, roomY);
   mDistanceMinSlider->Draw();
   mDistanceMaxSlider->Draw();
   mRolloffSlider->Draw();
   mReverbMixSlider->Draw();
   mReverbDecaySlider->Draw();
   mReverbSizeSlider->Draw();
   mReverbDampingSlider->Draw();

   mHRTFEnabledCheckbox->Draw();
   mHRTFQualityDropdown->Draw();
   mHeadRadiusSlider->Draw();
}

void EclipSpatialRender::Resize(float w, float h)
{
   mWidth = w;
   mHeight = h;
   if (mVisualCable)
      mVisualCable->SetManualPosition(mWidth - 8, mHeight / 2);
}

void EclipSpatialRender::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("width", moduleInfo, 400);
   mModuleSaveData.LoadFloat("height", moduleInfo, 400);

   SetUpFromSaveData();
}

void EclipSpatialRender::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["width"] = mWidth;
   moduleInfo["height"] = mHeight;
}

void EclipSpatialRender::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mWidth = mModuleSaveData.GetFloat("width");
   mHeight = mModuleSaveData.GetFloat("height");
}

void EclipSpatialRender::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mMasterVolume;
   out << mWidth;
   out << mHeight;
   out << kMaxObjects;
   out << mSpeakerLayout;
   out << mDistanceMin;
   out << mDistanceMax;
   out << mRolloff;
   out << mReverbMix;
   out << mReverbDecay;
   out << mReverbSize;
   out << mReverbDamping;
   out << mHRTFEnabled;
   out << mHeadRadius;
   out << mHRTFQuality;
   for (int i = 0; i < kMaxObjects; ++i)
   {
      out << mObjects[i].mX;
      out << mObjects[i].mY;
      out << mObjects[i].mZ;
      out << mObjects[i].mVolume;
      out << mObjects[i].mMuted;
      out << mObjects[i].mActive;
      out << mObjects[i].mColorHue;
      out << mObjects[i].mOcclusion;
      out << (int)mObjects[i].mAnim.mMode;
      out << mObjects[i].mAnim.mRate;
      out << mObjects[i].mAnim.mDepth;
      out << mObjects[i].mAnim.mPhase;
   }
}

void EclipSpatialRender::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev < 1) return;

   in >> mMasterVolume;
   in >> mWidth;
   in >> mHeight;
   int savedMax = 0;
   in >> savedMax;

   if (rev >= 2)
   {
      in >> mSpeakerLayout;
      in >> mDistanceMin;
      in >> mDistanceMax;
      in >> mRolloff;
      in >> mReverbMix;
      in >> mReverbDecay;
      in >> mReverbSize;
      in >> mReverbDamping;
   }

   if (rev >= 3)
   {
      in >> mHRTFEnabled;
      in >> mHeadRadius;
      in >> mHRTFQuality;
   }
   else
   {
      mHRTFEnabled = false;
      mHeadRadius = 8.75f;
      mHRTFQuality = 2;
   }

   mObjects.resize(kMaxObjects);
   int loadCount = std::min(savedMax, kMaxObjects);
   for (int i = 0; i < loadCount; ++i)
   {
      in >> mObjects[i].mX;
      in >> mObjects[i].mY;
      in >> mObjects[i].mZ;
      in >> mObjects[i].mVolume;
      in >> mObjects[i].mMuted;
      in >> mObjects[i].mActive;
      in >> mObjects[i].mColorHue;

      if (rev >= 2)
      {
         in >> mObjects[i].mOcclusion;
         int animMode = 0;
         in >> animMode;
         mObjects[i].mAnim.mMode = (AnimMode)animMode;
         in >> mObjects[i].mAnim.mRate;
         in >> mObjects[i].mAnim.mDepth;
         in >> mObjects[i].mAnim.mPhase;
      }
   }
}

#include "Spatial2DSpace.h"
#include "SpatialObject.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "PatchCableSource.h"
#include <algorithm>
#include <cmath>

static const int kCombL1Default = 1323;
static const int kCombL2Default = 1500;
static const int kCombR1Default = 1411;
static const int kCombR2Default = 1600;
static const int kApLDefault = 347;
static const int kApRDefault = 389;
static const float kApGain = 0.5f;

Spatial2DSpace::Spatial2DSpace()
: IAudioProcessor(gBufferSize)
{
}

void Spatial2DSpace::Init()
{
   IDrawableModule::Init();
   TheTransport->AddAudioPoller(this);
}

Spatial2DSpace::~Spatial2DSpace()
{
   TheTransport->RemoveAudioPoller(this);
   for (auto* obj : mObjects)
   {
      if (obj)
         obj->mRegisteredSpace = nullptr;
   }
}

void Spatial2DSpace::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   mRoomWidthSlider = new FloatSlider(this, "room w (cm)", 5, 2, 110, 15, &mRoomWidth, 100, 2000);
   mRoomDepthSlider = new FloatSlider(this, "room d (cm)", 120, 2, 110, 15, &mRoomDepth, 100, 2000);
   mRoomHeightSlider = new FloatSlider(this, "room h (cm)", 235, 2, 110, 15, &mRoomHeight, 50, 1000);
   mSpeakerCountSelector = new DropdownList(this, "speakers", 5, 20, &mNumSpeakers);
   mOutputModeSelector = new DropdownList(this, "output", 95, 20, &mOutputMode);
   mRoomEffectCheckbox = new Checkbox(this, "room fx", 180, 20, &mRoomEffectEnabled);
   mReverbMixSlider = new FloatSlider(this, "reverb mix", 235, 20, 90, 15, &mReverbMix, 0, 1);
   mShowVirtualSpeakersCheckbox = new Checkbox(this, "virt spk", 330, 20, &mShowVirtualSpeakers);
   mUserXSlider = new FloatSlider(this, "user x (cm)", 5, 38, 110, 15, &mUserX, -2000, 2000);
   mUserYSlider = new FloatSlider(this, "user y (cm)", 120, 38, 110, 15, &mUserY, -2000, 2000);
   mVirtualSpeakerSPLSlider = new FloatSlider(this, "spl (db)", 235, 38, 110, 15, &mVirtualSpeakerSPL, 70, 120);

   mSpeakerCountSelector->AddLabel("2", 2);
   mSpeakerCountSelector->AddLabel("4", 4);
   mSpeakerCountSelector->AddLabel("5", 5);
   mSpeakerCountSelector->AddLabel("6", 6);
   mSpeakerCountSelector->AddLabel("7", 7);
   mSpeakerCountSelector->AddLabel("8", 8);
   mSpeakerCountSelector->AddLabel("10", 10);
   mSpeakerCountSelector->AddLabel("12", 12);
   mSpeakerCountSelector->AddLabel("16", 16);

   mOutputModeSelector->AddLabel("speaker", 0);
   mOutputModeSelector->AddLabel("headphone", 1);

   RebuildSpeakers();
}

void Spatial2DSpace::Process(double time)
{
   GetBuffer()->Reset();

   if (!mRoomEffectEnabled || mOutputMode != 1)
      return;

   SyncBuffers();
   int bufferSize = GetBuffer()->BufferSize();

   float* outL = TheSynth->GetOutputBuffer(0);
   float* outR = TheSynth->GetOutputBuffer(1);
   if (!outL || !outR)
      return;

   float volume = mRoomWidth * mRoomDepth * mRoomHeight;
   float volumeNorm = volume / 90000000.0f;
   float feedback = ofClamp(0.4f + volumeNorm * 0.3f, 0.25f, 0.8f);
   float wetMix = mReverbMix * ofClamp(0.3f + volumeNorm * 0.5f, 0.15f, 1.0f);

   int combDL1 = (int)(kCombL1Default * mRoomWidth / 600.0f);
   int combDL2 = (int)(kCombL2Default * mRoomDepth / 500.0f);
   int combDR1 = (int)(kCombR1Default * mRoomWidth / 600.0f);
   int combDR2 = (int)(kCombR2Default * mRoomDepth / 500.0f);
   int apDL = kApLDefault;
   int apDR = kApRDefault;

   for (int s = 0; s < bufferSize; ++s)
   {
      float inL = GetBuffer()->GetChannel(0)[s];
      float inR = GetBuffer()->GetChannel(1)[s];
      float mono = (inL + inR) * 0.5f;

      int idx = mReverbIdx;

      int cL1 = (idx - combDL1 + kReverbBufSize) % kReverbBufSize;
      int cL2 = (idx - combDL2 + kReverbBufSize) % kReverbBufSize;
      int aL = (idx - apDL + kReverbBufSize) % kReverbBufSize;
      float combL1 = mCombL1[cL1];
      float combL2 = mCombL2[cL2];
      float combSumL = (combL1 + combL2);

      mCombL1[idx] = mono + combL1 * feedback;
      mCombL2[idx] = mono + combL2 * feedback;

      float apLDelayed = mApL[aL];
      mApL[idx] = combSumL + apLDelayed * kApGain;
      float wetL = apLDelayed - combSumL * kApGain;

      int cR1 = (idx - combDR1 + kReverbBufSize) % kReverbBufSize;
      int cR2 = (idx - combDR2 + kReverbBufSize) % kReverbBufSize;
      int aR = (idx - apDR + kReverbBufSize) % kReverbBufSize;
      float combR1 = mCombR1[cR1];
      float combR2 = mCombR2[cR2];
      float combSumR = (combR1 + combR2);

      mCombR1[idx] = mono + combR1 * feedback;
      mCombR2[idx] = mono + combR2 * feedback;

      float apRDelayed = mApR[aR];
      mApR[idx] = combSumR + apRDelayed * kApGain;
      float wetR = apRDelayed - combSumR * kApGain;

      outL[s] += wetL * wetMix;
      outR[s] += wetR * wetMix;

      mReverbIdx = (idx + 1) % kReverbBufSize;
   }
}

void Spatial2DSpace::OnTransportAdvanced(float amount)
{
}

void Spatial2DSpace::UnregisterObject(SpatialObject* obj)
{
   auto it = std::find(mObjects.begin(), mObjects.end(), obj);
   if (it != mObjects.end())
   {
      (*it)->mRegisteredSpace = nullptr;
      mObjects.erase(it);
   }
}

void Spatial2DSpace::RebuildSpeakers()
{
   mSpeakerPositions.clear();
   int n = mNumSpeakers;
   float halfW = mRoomWidth * 0.5f;
   float halfD = mRoomDepth * 0.5f;
   for (int i = 0; i < n; ++i)
   {
      float sx, sy;
      if (n == 2)
      {
         if (i == 0) { sx = -halfW * 0.6f; sy = 0; }
         else        { sx =  halfW * 0.6f; sy = 0; }
      }
      else if (n == 4)
      {
         float angle = (float)i / n * FPI * 2 - FPI * 0.75f;
         float rx = halfW * 0.7f;
         float ry = halfD * 0.7f;
         sx = std::cos(angle) * rx;
         sy = std::sin(angle) * ry;
      }
      else
      {
         float startAngle = -FPI * 0.8f;
         float endAngle = FPI * 0.8f;
         float t = (n > 1) ? (float)i / (n - 1) : 0.5f;
         float angle = startAngle + t * (endAngle - startAngle);
         sx = std::cos(angle) * halfW * 0.7f;
         sy = std::sin(angle) * halfD * 0.7f;
      }
      mSpeakerPositions.push_back(ofVec2f(sx, sy));
   }
}

ofVec2f Spatial2DSpace::PosToCanvas(float x, float y) const
{
   float canvasW = mModuleWidth - 10;
   float canvasH = mModuleHeight - 60;
   float cx = canvasW / 2;
   float cy = canvasH / 2;

   float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
   halfRange = std::max(halfRange, 500.0f);
   float scale = std::min(canvasW, canvasH) / (halfRange * 2);

   return ofVec2f(5 + cx + x * scale, 55 + cy + y * scale);
}

ofVec2f Spatial2DSpace::CanvasToPos(float cx, float cy) const
{
   float canvasW = mModuleWidth - 10;
   float canvasH = mModuleHeight - 60;
   float cxc = canvasW / 2;
   float cyc = canvasH / 2;

   float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
   halfRange = std::max(halfRange, 500.0f);
   float scale = std::min(canvasW, canvasH) / (halfRange * 2);

   return ofVec2f((cx - 5 - cxc) / scale, (cy - 55 - cyc) / scale);
}

void Spatial2DSpace::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mRoomWidthSlider->Draw();
   mRoomDepthSlider->Draw();
   mRoomHeightSlider->Draw();
   mSpeakerCountSelector->Draw();
   mOutputModeSelector->Draw();
   mRoomEffectCheckbox->Draw();
   mReverbMixSlider->Draw();
   mShowVirtualSpeakersCheckbox->Draw();
   mUserXSlider->Draw();
   mUserYSlider->Draw();
   mVirtualSpeakerSPLSlider->Draw();

   float canvasX = 5;
   float canvasY = 55;
   float canvasW = mModuleWidth - 10;
   float canvasH = mModuleHeight - 60;

   float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
   halfRange = std::max(halfRange, 500.0f);
   float scale = std::min(canvasW, canvasH) / (halfRange * 2);
   float cxc = canvasX + canvasW / 2;
   float cyc = canvasY + canvasH / 2;

   bool headphoneMode = (mOutputMode == 1);

   ofSetColor(35, 35, 45);
   ofFill();
   ofRect(canvasX, canvasY, canvasW, canvasH);

   auto toScreen = [&](float px, float py) -> ofVec2f
   {
      return ofVec2f(cxc + px * scale, cyc + py * scale);
   };

   float halfW = mRoomWidth * 0.5f;
   float halfD = mRoomDepth * 0.5f;

   ofSetColor(50, 55, 70);
   ofFill();
   ofVec2f r0 = toScreen(-halfW, -halfD);
   ofVec2f r1 = toScreen( halfW,  halfD);
   ofRect(r0.x, r0.y, r1.x - r0.x, r1.y - r0.y);
   ofSetColor(90, 95, 120);
   ofNoFill();
   ofRect(r0.x, r0.y, r1.x - r0.x, r1.y - r0.y);

   ofSetColor(70, 75, 95);
   ofSetLineWidth(1);
   ofLine(cxc, canvasY, cxc, canvasY + canvasH);
   ofLine(canvasX, cyc, canvasX + canvasW, cyc);

   bool showSpeakers = !headphoneMode || (headphoneMode && mShowVirtualSpeakers);

   if (showSpeakers)
   {
      for (int i = 0; i < mNumSpeakers; ++i)
      {
         ofVec2f sp = toScreen(mSpeakerPositions[i].x, mSpeakerPositions[i].y);
         if (headphoneMode)
         {
            ofSetColor(200, 180, 100);
            ofNoFill();
            ofCircle(sp.x, sp.y, 9);
            ofSetColor(200, 180, 100, 60);
            ofFill();
            ofCircle(sp.x, sp.y, 5);
         }
         else
         {
            ofSetColor(100, 200, 255);
            ofFill();
            ofCircle(sp.x, sp.y, 7);
         }
         ofSetColor(255, 255, 255);
         DrawTextNormal(ofToString(i + 1), (int)sp.x - 3, (int)sp.y + 4);
      }
   }

   ofVec2f up = toScreen(mUserX, mUserY);
   ofSetColor(255, 255, 100);
   ofFill();
   ofCircle(up.x, up.y, 6);
   ofSetColor(0, 0, 0);
   DrawTextNormal("U", (int)up.x - 3, (int)up.y + 4);

   for (auto* obj : mObjects)
   {
      if (obj)
      {
         ofVec2f op = toScreen(obj->GetPositionX(), obj->GetPositionY());
         ofSetColor(255, 150, 50);
         ofFill();
         ofCircle(op.x, op.y, 5);
         ofSetColor(255, 255, 255);
         DrawTextNormal("O", (int)op.x - 3, (int)op.y + 4);
         DrawTextNormal("z=" + ofToString((int)obj->GetPositionZ()) + "cm", (int)op.x - 10, (int)op.y - 10);
      }
   }

   auto drawDimLabel = [&](float x, float y, const char* text)
   {
      ofVec2f p = toScreen(x, y);
      ofSetColor(180, 180, 180);
      DrawTextNormal(text, (int)p.x - 10, (int)p.y);
   };
   drawDimLabel(0, halfD + 30, ("w=" + ofToString((int)mRoomWidth) + "cm").c_str());
   drawDimLabel(halfW + 20, 0, ("d=" + ofToString((int)mRoomDepth) + "cm").c_str());

   ofSetColor(180, 180, 200);
   ofVec2f hp3 = toScreen(0, -halfD - 20);
   DrawTextNormal(("h=" + ofToString((int)mRoomHeight) + "cm").c_str(), (int)hp3.x - 15, (int)hp3.y);

   if (headphoneMode)
   {
      ofSetColor(200, 200, 100, 80);
      ofVec2f hp4 = toScreen(0, 0);
      DrawTextNormal("HEADPHONE MODE", (int)hp4.x - 55, (int)hp4.y + 4);
   }

   if (mRoomEffectEnabled && headphoneMode)
   {
      ofSetColor(100, 200, 100, 60);
      ofVec2f re = toScreen(0, halfD);
      ofRect(re.x - 60, re.y, 120, 3);
   }

   int hoverIdx = mHoverTarget;
   bool canDragSpeakers = !headphoneMode || (headphoneMode && mShowVirtualSpeakers);
   if (canDragSpeakers && hoverIdx >= kDrag_SpeakerStart && hoverIdx < mNumSpeakers)
   {
      ofVec2f sp = toScreen(mSpeakerPositions[hoverIdx].x, mSpeakerPositions[hoverIdx].y);
      ofSetColor(255, 255, 255);
      ofNoFill();
      ofCircle(sp.x, sp.y, 11);
   }
   else if (hoverIdx == kDrag_User)
   {
      ofVec2f hp2 = toScreen(mUserX, mUserY);
      ofSetColor(255, 255, 255);
      ofNoFill();
      ofCircle(hp2.x, hp2.y, 10);
   }
}

void Spatial2DSpace::GetModuleDimensions(float& w, float& h)
{
   w = mModuleWidth;
   h = mModuleHeight;
}

void Spatial2DSpace::Resize(float w, float h)
{
   mModuleWidth = std::max(w, 300.0f);
   mModuleHeight = std::max(h, 380.0f);
}

void Spatial2DSpace::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   float canvasX = 5;
   float canvasY = 55;
   float canvasW = mModuleWidth - 10;
   float canvasH = mModuleHeight - 60;
   bool headphoneMode = (mOutputMode == 1);

   if (x >= canvasX && x < canvasX + canvasW && y >= canvasY && y < canvasY + canvasH)
   {
      ofVec2f up = PosToCanvas(mUserX, mUserY);
      float userDist = std::sqrt(ofVec2f(x - up.x, y - up.y).lengthSquared());
      if (userDist < 10)
      {
         mDragTarget = kDrag_User;
         mDragging = true;
         mDragOffset = ofVec2f(0, 0);
         return;
      }

      bool canDragSpeakers = !headphoneMode || (headphoneMode && mShowVirtualSpeakers);
      if (canDragSpeakers)
      {
         for (int i = 0; i < mNumSpeakers; ++i)
         {
            ofVec2f sp = PosToCanvas(mSpeakerPositions[i].x, mSpeakerPositions[i].y);
            float dist = std::sqrt(ofVec2f(x - sp.x, y - sp.y).lengthSquared());
            if (dist < 12)
            {
               mDragTarget = i;
               mDragging = true;
               mDragOffset = ofVec2f(0, 0);
               return;
            }
         }
      }
   }
}

bool Spatial2DSpace::MouseMoved(float x, float y)
{
   IDrawableModule::MouseMoved(x, y);

   float canvasX = 5;
   float canvasY = 55;
   float canvasW = mModuleWidth - 10;
   float canvasH = mModuleHeight - 60;
   bool headphoneMode = (mOutputMode == 1);

   mHoverTarget = kDrag_None;

   if (x >= canvasX && x < canvasX + canvasW && y >= canvasY && y < canvasY + canvasH)
   {
      ofVec2f up = PosToCanvas(mUserX, mUserY);
      if (std::sqrt(ofVec2f(x - up.x, y - up.y).lengthSquared()) < 10)
      {
         mHoverTarget = kDrag_User;
         return false;
      }

      bool canDragSpeakers = !headphoneMode || (headphoneMode && mShowVirtualSpeakers);
      if (canDragSpeakers)
      {
         for (int i = 0; i < mNumSpeakers; ++i)
         {
            ofVec2f sp = PosToCanvas(mSpeakerPositions[i].x, mSpeakerPositions[i].y);
            if (std::sqrt(ofVec2f(x - sp.x, y - sp.y).lengthSquared()) < 12)
            {
               mHoverTarget = i;
               return false;
            }
         }
      }
   }

   if (mDragging)
   {
      ofVec2f pos = CanvasToPos(x, y);
      float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
      halfRange = std::max(halfRange, 500.0f);
      float nx = ofClamp(pos.x, -halfRange, halfRange);
      float ny = ofClamp(pos.y, -halfRange, halfRange);

      if (mDragTarget == kDrag_User)
      {
         mUserX = nx;
         mUserY = ny;
      }
      else if (mDragTarget >= 0 && mDragTarget < mNumSpeakers)
      {
         mSpeakerPositions[mDragTarget].x = nx;
         mSpeakerPositions[mDragTarget].y = ny;
      }
   }

   return false;
}

void Spatial2DSpace::MouseReleased()
{
   IDrawableModule::MouseReleased();
   mDragging = false;
   mDragTarget = kDrag_None;
}

void Spatial2DSpace::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (slider == mRoomWidthSlider || slider == mRoomDepthSlider || slider == mRoomHeightSlider)
      RebuildSpeakers();
}

void Spatial2DSpace::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mSpeakerCountSelector)
      RebuildSpeakers();
}

void Spatial2DSpace::ButtonClicked(ClickButton* button, double time)
{
}

void Spatial2DSpace::CheckboxUpdated(Checkbox* checkbox, double time)
{
}

void Spatial2DSpace::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadFloat("roomwidth", moduleInfo, 600.0f);
   mModuleSaveData.LoadFloat("roomdepth", moduleInfo, 500.0f);
   mModuleSaveData.LoadFloat("roomheight", moduleInfo, 300.0f);
   mModuleSaveData.LoadInt("numspeakers", moduleInfo, 2, 2, 16);
   SetUpFromSaveData();
}

void Spatial2DSpace::SetUpFromSaveData()
{
   mRoomWidth = mModuleSaveData.GetFloat("roomwidth");
   mRoomDepth = mModuleSaveData.GetFloat("roomdepth");
   mRoomHeight = mModuleSaveData.GetFloat("roomheight");
   mNumSpeakers = mModuleSaveData.GetInt("numspeakers");
   RebuildSpeakers();
}

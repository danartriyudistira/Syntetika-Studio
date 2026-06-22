#include "SpatialRender.h"
#include "SpatialSource.h"
#include "ModularSynth.h"
#include "Profiler.h"
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

SpatialRender::SpatialRender()
: IAudioProcessor(gBufferSize)
{
}

void SpatialRender::Init()
{
   IDrawableModule::Init();
   TheTransport->AddAudioPoller(this);
}

SpatialRender::~SpatialRender()
{
   TheTransport->RemoveAudioPoller(this);
   {
      std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
      for (auto& s : mSources)
      {
         if (s.src)
            s.src->mRegisteredRender = nullptr;
      }
   }
}

void SpatialRender::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   mRoomWidthSlider = new FloatSlider(this, "room w (cm)", 5, kRow1Y, 110, 15, &mRoomWidth, 100, 2000);
   mRoomDepthSlider = new FloatSlider(this, "room d (cm)", 120, kRow1Y, 110, 15, &mRoomDepth, 100, 2000);
   mRoomHeightSlider = new FloatSlider(this, "room h (cm)", 235, kRow1Y, 110, 15, &mRoomHeight, 50, 1000);
   mSpeakerCountSelector = new DropdownList(this, "speakers", 5, kRow2Y, &mNumSpeakers);
   mSPLSlider = new FloatSlider(this, "spl (db)", 100, kRow2Y, 110, 15, &mSPL, 70, 120);
   mRoomEffectCheckbox = new Checkbox(this, "room fx", 220, kRow2Y, &mRoomEffectEnabled);
   mReverbMixSlider = new FloatSlider(this, "reverb mix", 280, kRow2Y, 100, 15, &mReverbMix, 0, 1);
   mUserXSlider = new FloatSlider(this, "user x (cm)", 5, kRow3Y, 110, 15, &mUserX, -2000, 2000);
   mUserYSlider = new FloatSlider(this, "user y (cm)", 120, kRow3Y, 110, 15, &mUserY, -2000, 2000);

   mDirectSourceSelector = new DropdownList(this, "dir src", 235, kRow3Y, &mDirectSource);
   mBinauralSourceSelector = new DropdownList(this, "bin src", 330, kRow3Y, &mBinauralSource);

   mSpeakerCountSelector->AddLabel("2", 2);
   mSpeakerCountSelector->AddLabel("4", 4);
   mSpeakerCountSelector->AddLabel("5", 5);
   mSpeakerCountSelector->AddLabel("6", 6);
   mSpeakerCountSelector->AddLabel("7", 7);
   mSpeakerCountSelector->AddLabel("8", 8);
   mSpeakerCountSelector->AddLabel("10", 10);
   mSpeakerCountSelector->AddLabel("12", 12);
   mSpeakerCountSelector->AddLabel("16", 16);

   // Mono output cables: DirL(0), DirR(1), BinL(2), BinR(3), Spk(4..)
   GetPatchCableSource()->SetManualSide(PatchCableSource::Side::kBottom);

   mDirectRCable = new PatchCableSource(this, kConnectionType_Audio);
   mDirectRCable->SetManualSide(PatchCableSource::Side::kBottom);
   AddPatchCableSource(mDirectRCable);

   mBinauralLCable = new PatchCableSource(this, kConnectionType_Audio);
   mBinauralLCable->SetManualSide(PatchCableSource::Side::kBottom);
   AddPatchCableSource(mBinauralLCable);

   mBinauralRCable = new PatchCableSource(this, kConnectionType_Audio);
   mBinauralRCable->SetManualSide(PatchCableSource::Side::kBottom);
   AddPatchCableSource(mBinauralRCable);

   mSpeakerCables.clear();

   RebuildSpeakers();
   RebuildDropdowns();
   RebuildSpeakerCables();
   UpdateCablePositions();
}

void SpatialRender::Process(double time)
{
   PROFILER(SpatialRender);

   GetBuffer()->Reset();
   int bufferSize = gBufferSize;
   assert(bufferSize <= kMaxProcessBufSize);

   //snapshot UI-controlled data to avoid data race with UI thread
   std::vector<ofVec2f> speakerPositions;
   int numSpk = 0;
   float userX, userY;
   float roomW, roomD, roomH;
   int directSrc, binauralSrc;
   float spl;
   float reverbMixVal;
   bool roomEffect;
   int speakerChan[16];
   {
      std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
      speakerPositions = mSpeakerPositions;
      numSpk = (int)mSpeakerPositions.size();
      userX = mUserX;
      userY = mUserY;
      roomW = mRoomWidth;
      roomD = mRoomDepth;
      roomH = mRoomHeight;
      directSrc = mDirectSource;
      binauralSrc = mBinauralSource;
      spl = mSPL;
      reverbMixVal = mReverbMix;
      roomEffect = mRoomEffectEnabled;
      for (int i = 0; i < 16; ++i)
         speakerChan[i] = mSpeakerChannels[i];
   }

   memset(mDirectL, 0, kMaxProcessBufSize * sizeof(float));
   memset(mDirectR, 0, kMaxProcessBufSize * sizeof(float));
   memset(mBinauralL, 0, kMaxProcessBufSize * sizeof(float));
   memset(mBinauralR, 0, kMaxProcessBufSize * sizeof(float));
   for (int i = 0; i < 16; ++i)
      memset(mSpeakerSignal[i], 0, kMaxProcessBufSize * sizeof(float));

   int numSrc;
   {
      std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
      numSrc = (int)mSources.size();
      for (auto& src : mSources)
         if (src.isInternal)
            src.hasAudio = false;
   }

   ChannelBuffer* input = GetBuffer();
   int numInputCh = input->NumActiveChannels();
   if (numInputCh > 0)
   {
      int copySize = std::min(input->BufferSize(), kMaxProcessBufSize);
      for (int ch = 0; ch < numInputCh && ch < 2; ++ch)
      {
         const float* chData = input->GetChannel(ch);
         if (!chData)
            continue;
         for (int s = 0; s < bufferSize; ++s)
         {
            float sample = chData[s];
            mDirectL[s] += sample;
            mDirectR[s] += sample;
            for (int spk = 0; spk < numSpk; ++spk)
               mSpeakerSignal[spk][s] += sample / std::max(numSpk, 1);
         }
      }
   }

   {
      std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
      for (int si = 0; si < numSrc; ++si)
      {
         auto& src = mSources[si];
         if (!src.hasAudio)
            continue;

          float dx = src.x - userX;
          float dy = src.y - userY;
          float dz = src.z - 170.0f;
          float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
          float distAttn = 1.0f / (1.0f + dist * 0.002f);
          float objAngle = std::atan2(dy, dx);
          float splGainVal = std::pow(10.0f, (spl - 85.0f) / 20.0f);

          for (int s = 0; s < bufferSize; ++s)
          {
             float sample = src.audioBuffer[s];

             bool routeToDirect = (directSrc == -1 || directSrc == si);
             if (routeToDirect)
             {
                mDirectL[s] += sample;
                mDirectR[s] += sample;
             }

             for (int spk = 0; spk < numSpk; ++spk)
             {
                float spkDx = speakerPositions[spk].x - userX;
                float spkDy = speakerPositions[spk].y - userY;
               float spkAngle = std::atan2(spkDy, spkDx);
               float angleDiff = objAngle - spkAngle;
               while (angleDiff > FPI) angleDiff -= 2 * FPI;
               while (angleDiff < -FPI) angleDiff += 2 * FPI;
               float vbapGain = std::max(0.0f, std::cos(angleDiff * 0.5f));
               vbapGain = std::pow(vbapGain, 1.5f);

               float spkDist = std::sqrt(spkDx * spkDx + spkDy * spkDy);
               float spkDistAttn = 1.0f / (1.0f + spkDist * 0.002f);
               float spkSPL = (spk < (int)mSpeakerSPL.size()) ? mSpeakerSPL[spk] : mSPL;
               float spkSplGain = std::pow(10.0f, (spkSPL - 85.0f) / 20.0f);

                mSpeakerSignal[spk][s] += sample * distAttn * vbapGain * splGainVal * spkSplGain * spkDistAttn;
            }
         }
      }
   }

   for (int spk = 0; spk < numSpk; ++spk)
   {
         int outCh = (spk < numSpk) ? speakerChan[spk] : 0;
      if (outCh == 0)
      {
         float speakerPan = ofClamp(speakerPositions[spk].x / (roomW * 0.4f), -1, 1);
         float panAngle = (speakerPan + 1.0f) * FPI / 4.0f;
         float cosPan = std::cos(panAngle);
         float sinPan = std::sin(panAngle);
         for (int s = 0; s < bufferSize; ++s)
         {
            mBinauralL[s] += mSpeakerSignal[spk][s] * cosPan;
            mBinauralR[s] += mSpeakerSignal[spk][s] * sinPan;
         }
      }
      else
      {
         IAudioReceiver* spkTarget = GetTarget(4 + spk);
         if (spkTarget)
         {
            ChannelBuffer* out = spkTarget->GetBuffer();
            out->SetNumActiveChannels(1);
            for (int s = 0; s < bufferSize; ++s)
               out->GetChannel(0)[s] += mSpeakerSignal[spk][s];
         }
      }
   }

   // Index 0: Direct L
   IAudioReceiver* dirLTarget = GetTarget(0);
   if (dirLTarget)
   {
      ChannelBuffer* out = dirLTarget->GetBuffer();
      out->SetNumActiveChannels(1);
      for (int s = 0; s < bufferSize; ++s)
         out->GetChannel(0)[s] += mDirectL[s];
   }

   // Index 1: Direct R
   IAudioReceiver* dirRTarget = GetTarget(1);
   if (dirRTarget)
   {
      ChannelBuffer* out = dirRTarget->GetBuffer();
      out->SetNumActiveChannels(1);
      for (int s = 0; s < bufferSize; ++s)
         out->GetChannel(0)[s] += mDirectR[s];
   }

   // Index 2: Binaural L, Index 3: Binaural R
   {
      float roomVolume = roomW * roomD * roomH;
      float volumeNorm = roomVolume / 90000000.0f;
      float feedback = ofClamp(0.4f + volumeNorm * 0.3f, 0.25f, 0.8f);

      if (roomEffect)
      {
         int combDL1 = (int)(kCombL1Default * roomW / 600.0f);
         int combDL2 = (int)(kCombL2Default * roomD / 500.0f);
         int combDR1 = (int)(kCombR1Default * roomW / 600.0f);
         int combDR2 = (int)(kCombR2Default * roomD / 500.0f);
         int apDL = kApLDefault;
         int apDR = kApRDefault;
         float wetMix = reverbMixVal * ofClamp(0.3f + volumeNorm * 0.5f, 0.15f, 1.0f);

         for (int s = 0; s < bufferSize; ++s)
         {
            float mono = (mBinauralL[s] + mBinauralR[s]) * 0.5f;
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
            mBinauralL[s] += (apLDelayed - combSumL * kApGain) * wetMix;

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
            mBinauralR[s] += (apRDelayed - combSumR * kApGain) * wetMix;

            mReverbIdx = (idx + 1) % kReverbBufSize;
         }
      }

      IAudioReceiver* binLTarget = GetTarget(2);
      if (binLTarget)
      {
         ChannelBuffer* out = binLTarget->GetBuffer();
         out->SetNumActiveChannels(1);
         for (int s = 0; s < bufferSize; ++s)
            out->GetChannel(0)[s] += mBinauralL[s];
      }

      IAudioReceiver* binRTarget = GetTarget(3);
      if (binRTarget)
      {
         ChannelBuffer* out = binRTarget->GetBuffer();
         out->SetNumActiveChannels(1);
         for (int s = 0; s < bufferSize; ++s)
            out->GetChannel(0)[s] += mBinauralR[s];
      }
   }
}

void SpatialRender::OnTransportAdvanced(float amount)
{
}

void SpatialRender::RegisterSource(SpatialSource* src)
{
   {
      std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
      for (auto& s : mSources)
      {
         if (s.src == src)
            return;
      }
      RegisteredSource rs;
      rs.src = src;
      rs.x = src->GetPositionX();
      rs.y = src->GetPositionY();
      rs.z = src->GetPositionZ();
      rs.hasAudio = false;
      rs.bufferSize = 0;
      mSources.push_back(rs);
   }
   RebuildDropdowns();
}

void SpatialRender::UnregisterSource(SpatialSource* src)
{
   bool found = false;
   {
      std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
      for (auto it = mSources.begin(); it != mSources.end(); ++it)
      {
         if (it->src == src)
         {
            int idx = (int)(it - mSources.begin());
            mSources.erase(it);
            auto fix = [&](int& sel)
            {
               if (sel == idx) sel = -1;
               else if (sel > idx) sel--;
            };
            fix(mDirectSource);
            fix(mBinauralSource);
            for (int i = 0; i < 16; ++i)
               fix(mSpeakerSources[i]);
            found = true;
            break;
         }
      }
   }
   if (found)
      RebuildDropdowns();
}

void SpatialRender::NotifySourceMoved(SpatialSource* src)
{
   std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
   for (auto& s : mSources)
   {
      if (s.src == src)
      {
         s.x = src->GetPositionX();
         s.y = src->GetPositionY();
         s.z = src->GetPositionZ();
         return;
      }
   }
}

void SpatialRender::AcceptSourceAudio(SpatialSource* src, float* buffer, int bufferSize)
{
   std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
   for (auto& s : mSources)
   {
      if (s.src == src)
      {
         int copySize = std::min(bufferSize, (int)(sizeof(s.audioBuffer) / sizeof(float)));
         for (int i = 0; i < copySize; ++i)
            s.audioBuffer[i] = buffer[i];
         s.bufferSize = copySize;
         s.x = src->GetPositionX();
         s.y = src->GetPositionY();
         s.z = src->GetPositionZ();
         s.hasAudio = true;
         return;
      }
   }
}

void SpatialRender::RebuildSpeakers()
{
   mSpeakerPositions.clear();
   mSpeakerSPL.clear();
   int n = mNumSpeakers;
   float halfW = mRoomWidth * 0.5f;
   float halfD = mRoomDepth * 0.5f;
   for (int i = 0; i < n && i < 16; ++i)
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
         sx = std::cos(angle) * halfW * 0.7f;
         sy = std::sin(angle) * halfD * 0.7f;
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
      mSpeakerSPL.push_back(mSPL);
      mSpeakerChannels[i] = (i < 2 ? (i + 1) : 0);
      mSpeakerSources[i] = -1;
   }
   for (int i = n; i < 16; ++i)
   {
      mSpeakerChannels[i] = 0;
      mSpeakerSources[i] = -1;
   }
   RebuildDropdowns();
   RebuildSpeakerCables();
   UpdateCablePositions();
}

void SpatialRender::RebuildDropdowns()
{
   std::lock_guard<std::recursive_mutex> lock(mSourceMutex);
   int numSrc = (int)mSources.size();

   mDirectSourceSelector->Clear();
   mDirectSourceSelector->AddLabel("Sum All", -1);
   for (int i = 0; i < numSrc; ++i)
   {
      char label[32];
      if (mSources[i].src)
         snprintf(label, 32, "Src %d: %s", i + 1, mSources[i].src->Name());
      else
         snprintf(label, 32, "Cable ch%d", mSources[i].internalChannel);
      mDirectSourceSelector->AddLabel(label, i);
   }

   mBinauralSourceSelector->Clear();
   mBinauralSourceSelector->AddLabel("Virtual Mix", -1);
   for (int i = 0; i < numSrc; ++i)
   {
      char label[32];
      if (mSources[i].src)
         snprintf(label, 32, "Src %d: %s", i + 1, mSources[i].src->Name());
      else
         snprintf(label, 32, "Cable ch%d", mSources[i].internalChannel);
      mBinauralSourceSelector->AddLabel(label, i);
   }

   while ((int)mSpeakerSourceSelectors.size() < 16)
   {
      int idx = (int)mSpeakerSourceSelectors.size();
      DropdownList* dd = new DropdownList(this, "spk src", 0, 0, &mSpeakerSources[idx]);
      mSpeakerSourceSelectors.push_back(dd);
   }

   while ((int)mSpeakerChannelSelectors.size() < 16)
   {
      int idx = (int)mSpeakerChannelSelectors.size();
      DropdownList* dd = new DropdownList(this, "spk ch", 0, 0, &mSpeakerChannels[idx]);
      mSpeakerChannelSelectors.push_back(dd);
   }

   for (int i = 0; i < 16; ++i)
   {
      DropdownList* chDd = mSpeakerChannelSelectors[i];
      chDd->Clear();
      chDd->AddLabel("bin", 0);
      for (int ch = 1; ch <= 16; ++ch)
      {
         char chLabel[8];
         snprintf(chLabel, 8, "ch%d", ch);
         chDd->AddLabel(chLabel, ch);
      }
   }

   for (int i = 0; i < mNumSpeakers; ++i)
   {
      DropdownList* dd = mSpeakerSourceSelectors[i];
      dd->Clear();
      dd->AddLabel("VBAP Mix", -1);
      for (int si = 0; si < numSrc; ++si)
      {
         char label[16];
         if (mSources[si].src)
            snprintf(label, 16, "Src %d", si + 1);
         else
            snprintf(label, 16, "Cable %d", mSources[si].internalChannel);
         dd->AddLabel(label, si);
      }
   }

   UpdateDropdownPositions();
}

void SpatialRender::UpdateDropdownPositions()
{
   float canvasY = (float)kHeaderH;
   float canvasH = GetCanvasHeight();

   int sourceListY = (int)(canvasY + canvasH + 6);
   int sourcePanelH = ((int)mSources.size() + 1) * kRowH + 8;
   int patchingPanelY = sourceListY + sourcePanelH + 4;
   int spkRows = std::min(mNumSpeakers, 8);
   int spkY2 = patchingPanelY + kRowH;

   for (int i = 0; i < 16; ++i)
   {
      if (i < mNumSpeakers)
      {
         int col = i / 8;
         int row = i % 8;
         int sx = 10 + col * 260;
         int sy2 = spkY2 + row * kRowH;
         mSpeakerSourceSelectors[i]->SetPosition((float)(sx + 72), (float)sy2);
         mSpeakerChannelSelectors[i]->SetPosition((float)(sx + 138), (float)sy2);
      }
      else
      {
         mSpeakerSourceSelectors[i]->SetPosition(-1000, -1000);
         mSpeakerChannelSelectors[i]->SetPosition(-1000, -1000);
      }
   }
}

void SpatialRender::RebuildSpeakerCables()
{
   int needed = mNumSpeakers;
   while ((int)mSpeakerCables.size() > needed)
   {
      auto* cable = mSpeakerCables.back();
      RemovePatchCableSource(cable);
      mSpeakerCables.pop_back();
   }
   while ((int)mSpeakerCables.size() < needed)
   {
      auto* cable = new PatchCableSource(this, kConnectionType_Audio);
      cable->SetManualSide(PatchCableSource::Side::kBottom);
      AddPatchCableSource(cable);
      mSpeakerCables.push_back(cable);
   }
}

void SpatialRender::UpdateCableLabels()
{
   int total = 4 + (int)mSpeakerCables.size();
   float spacing = std::min(28.0f, (mModuleWidth - 10) / std::max(total, 1));
   float startX = (mModuleWidth - spacing * (total - 1)) / 2;
   int labelY = (int)mModuleHeight - 2;

   ofSetColor(100, 180, 255);
   DrawTextNormal("Dir L", (int)(startX) - 5, labelY);
   DrawTextNormal("Dir R", (int)(startX + spacing) - 5, labelY);
   ofSetColor(200, 180, 100);
   DrawTextNormal("Bin L", (int)(startX + spacing * 2) - 6, labelY);
   DrawTextNormal("Bin R", (int)(startX + spacing * 3) - 6, labelY);
   ofSetColor(100, 200, 255);
   for (int i = 0; i < (int)mSpeakerCables.size(); ++i)
   {
      char lbl[8];
      snprintf(lbl, 8, "Spk%d", i + 1);
      DrawTextNormal(lbl, (int)(startX + spacing * (4 + i)) - 8, labelY);
   }
}

void SpatialRender::UpdateCablePositions()
{
   int total = 4 + (int)mSpeakerCables.size();
   float spacing = std::min(28.0f, (mModuleWidth - 10) / std::max(total, 1));
   float startX = (mModuleWidth - spacing * (total - 1)) / 2;

   GetPatchCableSource()->SetManualPosition(startX, mModuleHeight + 3);
   mDirectRCable->SetManualPosition(startX + spacing, mModuleHeight + 3);
   mBinauralLCable->SetManualPosition(startX + spacing * 2, mModuleHeight + 3);
   mBinauralRCable->SetManualPosition(startX + spacing * 3, mModuleHeight + 3);

   for (int i = 0; i < (int)mSpeakerCables.size(); ++i)
      mSpeakerCables[i]->SetManualPosition(startX + spacing * (4 + i), mModuleHeight + 3);
}

ofVec2f SpatialRender::PosToCanvas(float x, float y) const
{
   float canvasW = mModuleWidth - 10;
   float canvasH = GetCanvasHeight();
   float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
   halfRange = std::max(halfRange, 500.0f);
   float scale = std::min(canvasW, canvasH) / (halfRange * 2);
   return ofVec2f(5 + canvasW / 2 + x * scale, (float)kHeaderH + canvasH / 2 + y * scale);
}

ofVec2f SpatialRender::CanvasToPos(float cx, float cy) const
{
   float canvasW = mModuleWidth - 10;
   float canvasH = GetCanvasHeight();
   float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
   halfRange = std::max(halfRange, 500.0f);
   float scale = std::min(canvasW, canvasH) / (halfRange * 2);
   return ofVec2f((cx - 5 - canvasW / 2) / scale, (cy - (float)kHeaderH - canvasH / 2) / scale);
}

void SpatialRender::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   std::lock_guard<std::recursive_mutex> lock(mSourceMutex);

   mRoomWidthSlider->Draw();
   mRoomDepthSlider->Draw();
   mRoomHeightSlider->Draw();
   mSpeakerCountSelector->Draw();
   mSPLSlider->Draw();
   mRoomEffectCheckbox->Draw();
   mReverbMixSlider->Draw();
   mUserXSlider->Draw();
   mUserYSlider->Draw();
   mDirectSourceSelector->Draw();
   mBinauralSourceSelector->Draw();

   float canvasX = 5;
   float canvasY = (float)kHeaderH;
   float canvasW = mModuleWidth - 10;
   float canvasH = GetCanvasHeight();

   float halfRange = std::max(mRoomWidth, mRoomDepth) * 0.5f + 300.0f;
   halfRange = std::max(halfRange, 500.0f);
   float scale = std::min(canvasW, canvasH) / (halfRange * 2);
   float cxc = canvasX + canvasW / 2;
   float cyc = canvasY + canvasH / 2;

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
   ofVec2f r1 = toScreen(halfW, halfD);
   ofRect(r0.x, r0.y, r1.x - r0.x, r1.y - r0.y);
   ofSetColor(90, 95, 120);
   ofNoFill();
   ofRect(r0.x, r0.y, r1.x - r0.x, r1.y - r0.y);

   ofSetColor(70, 75, 95);
   ofSetLineWidth(1);
   ofLine(cxc, canvasY, cxc, canvasY + canvasH);
   ofLine(canvasX, cyc, canvasX + canvasW, cyc);

   for (int i = 0; i < mNumSpeakers; ++i)
   {
      ofVec2f sp = toScreen(mSpeakerPositions[i].x, mSpeakerPositions[i].y);
       int outCh = (i < mNumSpeakers) ? mSpeakerChannels[i] : 0;
       if (outCh == 0)
       {
          ofSetColor(200, 180, 100);
          ofNoFill();
          ofCircle(sp.x, sp.y, 9);
         ofSetColor(200, 180, 100, 60);
         ofFill();
         ofCircle(sp.x, sp.y, 5);
         ofSetColor(200, 180, 100);
      }
      else
      {
         ofSetColor(100, 200, 255);
         ofFill();
         ofCircle(sp.x, sp.y, 7);
         ofSetColor(255, 255, 255);
      }
      DrawTextNormal(ofToString(i + 1), (int)sp.x - 3, (int)sp.y + 4);
      ofSetColor(180, 180, 180);
      if (outCh == 0)
         DrawTextNormal("bin", (int)sp.x - 8, (int)sp.y + 18);
      else
         DrawTextNormal("ch" + ofToString(outCh), (int)sp.x - 10, (int)sp.y + 18);
   }

   ofVec2f up = toScreen(mUserX, mUserY);
   ofSetColor(255, 255, 100);
   ofFill();
   ofCircle(up.x, up.y, 6);
   ofSetColor(0, 0, 0);
   DrawTextNormal("U", (int)up.x - 3, (int)up.y + 4);

   for (auto& src : mSources)
   {
      ofVec2f op = toScreen(src.x, src.y);
      ofSetColor(255, 150, 50);
      ofFill();
      ofCircle(op.x, op.y, 5);
      ofSetColor(255, 255, 255);
      const char* name = src.src ? src.src->Name() : "src";
      DrawTextNormal(name, (int)op.x - 10, (int)op.y - 10);
      DrawTextNormal("S", (int)op.x - 3, (int)op.y + 4);
   }

   auto drawDim = [&](float x, float y, const char* text)
   {
      ofVec2f p = toScreen(x, y);
      ofSetColor(180, 180, 180);
      DrawTextNormal(text, (int)p.x - 10, (int)p.y);
   };
   drawDim(0, halfD + 30, ("w=" + ofToString((int)mRoomWidth) + "cm").c_str());
   drawDim(halfW + 20, 0, ("d=" + ofToString((int)mRoomDepth) + "cm").c_str());

   ofSetColor(180, 180, 200);
   ofVec2f hp3 = toScreen(0, -halfD - 20);
   DrawTextNormal(("h=" + ofToString((int)mRoomHeight) + "cm").c_str(), (int)hp3.x - 15, (int)hp3.y);

   if (mRoomEffectEnabled)
   {
      ofSetColor(100, 200, 100, 60);
      ofVec2f re = toScreen(0, halfD);
      ofRect(re.x - 60, re.y, 120, 3);
   }

   if (mHoverTarget >= 0 && mHoverTarget < mNumSpeakers)
   {
      ofVec2f sp = toScreen(mSpeakerPositions[mHoverTarget].x, mSpeakerPositions[mHoverTarget].y);
      ofSetColor(255, 255, 255);
      ofNoFill();
      ofCircle(sp.x, sp.y, 11);
   }
   else if (mHoverTarget == -2)
   {
      ofVec2f hp2 = toScreen(mUserX, mUserY);
      ofSetColor(255, 255, 255);
      ofNoFill();
      ofCircle(hp2.x, hp2.y, 10);
   }

   int sourceListY = (int)(canvasY + canvasH + 6);
   ofSetColor(40, 40, 50);
   ofFill();
   ofRect(5, (float)sourceListY, mModuleWidth - 10, (float)(((int)mSources.size() + 1) * kRowH + 8));

   ofSetColor(180, 180, 200);
   DrawTextNormal("Sources:", 8, sourceListY + 12);

   int sy = sourceListY + 2 + kRowH;
   for (int i = 0; i < (int)mSources.size(); ++i)
   {
      auto& src = mSources[i];
      ofSetColor(180, 180, 200);
      char label[64];
      if (src.src)
         snprintf(label, 64, "[%d] %s", i + 1, src.src->Name());
      else
         snprintf(label, 64, "[%d] cable ch%d", i + 1, src.internalChannel);
      DrawTextNormal(label, 8, sy + 12);

      ofSetColor(140, 140, 160);
      DrawTextNormal(("x:" + ofToString((int)src.x)).c_str(), 170, sy + 12);
      DrawTextNormal(("y:" + ofToString((int)src.y)).c_str(), 220, sy + 12);

      sy += kRowH;
   }

   int patchingPanelY = sy + 4;
   int spkRows = std::min(mNumSpeakers, 8);
   float patchingH = (float)((2 + spkRows) * kRowH + 8);

   ofSetColor(40, 40, 50);
   ofFill();
   ofRect(5, (float)patchingPanelY, mModuleWidth - 10, patchingH);

   ofSetColor(180, 180, 200);
   DrawTextNormal("Speaker Patching:", 10, patchingPanelY + 12);

   int spkY2 = patchingPanelY + kRowH;
   for (int i = 0; i < mNumSpeakers; ++i)
   {
      int col = i / 8;
      int row = i % 8;
      int sx = 10 + col * 260;
      int sy2 = spkY2 + row * kRowH;

      ofSetColor(140, 140, 160);
      DrawTextNormal(("Spk" + ofToString(i + 1)).c_str(), sx, sy2 + 12);

      if (i < (int)mSpeakerSourceSelectors.size())
         mSpeakerSourceSelectors[i]->Draw();
      if (i < (int)mSpeakerChannelSelectors.size())
         mSpeakerChannelSelectors[i]->Draw();
   }

   // Cable labels at bottom edge — aligned with cable positions
   UpdateCableLabels();
}

float SpatialRender::GetCanvasHeight() const
{
   int sourceListH = ((int)mSources.size() + 1) * kRowH + 8;
   int spkRows = std::min(mNumSpeakers, 8);
   int patchingH = (2 + spkRows) * kRowH + 8;
   float h = mModuleHeight - (float)kHeaderH - 10.0f - (float)sourceListH - (float)patchingH;
   return std::max(h, 100.0f);
}

void SpatialRender::GetModuleDimensions(float& w, float& h)
{
   w = mModuleWidth;
   h = mModuleHeight;
}

void SpatialRender::Resize(float w, float h)
{
   mModuleWidth = std::max(w, 300.0f);
   mModuleHeight = std::max(h, 400.0f);
   UpdateCablePositions();
   UpdateDropdownPositions();
}

void SpatialRender::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   float canvasX = 5;
   float canvasY = (float)kHeaderH;
   float canvasW = mModuleWidth - 10;
   float canvasH = GetCanvasHeight();

   int sourceListY = (int)(canvasY + canvasH + 6);
   int sourcePanelH = ((int)mSources.size() + 1) * kRowH + 8;
   int patchingPanelY = sourceListY + sourcePanelH + 4;
   int spkRows = std::min(mNumSpeakers, 8);
   float patchingH = (float)((2 + spkRows) * kRowH + 8);

   if (x >= canvasX && x < canvasX + canvasW && y >= canvasY && y < canvasY + canvasH)
   {
      for (int i = 0; i < (int)mSources.size(); ++i)
      {
         ofVec2f op = PosToCanvas(mSources[i].x, mSources[i].y);
          if (ofVec2f(x - op.x, y - op.y).lengthSquared() < 64)
         {
            mDragTarget = 1000 + i;
            mDragging = true;
            mDragOffset = ofVec2f(0, 0);
            return;
         }
      }

      ofVec2f up = PosToCanvas(mUserX, mUserY);
      if (ofVec2f(x - up.x, y - up.y).lengthSquared() < 100)
      {
         mDragTarget = -2;
         mDragging = true;
         mDragOffset = ofVec2f(0, 0);
         return;
      }

      for (int i = 0; i < mNumSpeakers; ++i)
      {
         ofVec2f sp = PosToCanvas(mSpeakerPositions[i].x, mSpeakerPositions[i].y);
         if (ofVec2f(x - sp.x, y - sp.y).lengthSquared() < 144)
         {
            mDragTarget = i;
            mDragging = true;
            mDragOffset = ofVec2f(0, 0);
            return;
         }
      }
   }
}

bool SpatialRender::MouseMoved(float x, float y)
{
   IDrawableModule::MouseMoved(x, y);

   float canvasX = 5;
   float canvasY = (float)kHeaderH;
   float canvasW = mModuleWidth - 10;
   float canvasH = GetCanvasHeight();

   mHoverTarget = -1;

   if (x >= canvasX && x < canvasX + canvasW && y >= canvasY && y < canvasY + canvasH)
   {
      ofVec2f up = PosToCanvas(mUserX, mUserY);
      if (ofVec2f(x - up.x, y - up.y).lengthSquared() < 100)
      {
         mHoverTarget = -2;
         return false;
      }

      for (int i = 0; i < mNumSpeakers; ++i)
      {
         ofVec2f sp = PosToCanvas(mSpeakerPositions[i].x, mSpeakerPositions[i].y);
         if (ofVec2f(x - sp.x, y - sp.y).lengthSquared() < 144)
         {
            mHoverTarget = i;
            return false;
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

      if (mDragTarget == -2)
      {
         mUserX = nx;
         mUserY = ny;
      }
      else if (mDragTarget >= 0 && mDragTarget < mNumSpeakers)
      {
         mSpeakerPositions[mDragTarget].x = nx;
         mSpeakerPositions[mDragTarget].y = ny;
      }
      else if (mDragTarget >= 1000)
      {
         int srcIdx = mDragTarget - 1000;
         if (srcIdx >= 0 && srcIdx < (int)mSources.size())
         {
            mSources[srcIdx].x = nx;
            mSources[srcIdx].y = ny;
            if (mSources[srcIdx].src)
               mSources[srcIdx].src->SetPosition(nx, ny, mSources[srcIdx].z);
         }
      }
   }

   return false;
}

void SpatialRender::MouseReleased()
{
   IDrawableModule::MouseReleased();
   mDragging = false;
   mDragTarget = -1;
}

void SpatialRender::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (slider == mRoomWidthSlider || slider == mRoomDepthSlider || slider == mRoomHeightSlider)
      RebuildSpeakers();
}

void SpatialRender::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mSpeakerCountSelector)
      RebuildSpeakers();
}

void SpatialRender::CheckboxUpdated(Checkbox* checkbox, double time)
{
}

void SpatialRender::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadFloat("roomwidth", moduleInfo, 600.0f);
   mModuleSaveData.LoadFloat("roomdepth", moduleInfo, 500.0f);
   mModuleSaveData.LoadFloat("roomheight", moduleInfo, 300.0f);
   mModuleSaveData.LoadInt("numspeakers", moduleInfo, 2, 2, 16);
   mModuleSaveData.LoadFloat("spl", moduleInfo, 85.0f);
   mModuleSaveData.LoadFloat("userx", moduleInfo, 0.0f);
   mModuleSaveData.LoadFloat("usery", moduleInfo, -100.0f);
   mModuleSaveData.LoadBool("roomeffect", moduleInfo, false);
   mModuleSaveData.LoadFloat("reverbmix", moduleInfo, 0.3f);
   mModuleSaveData.LoadInt("directsource", moduleInfo, -1);
   mModuleSaveData.LoadInt("binaurausource", moduleInfo, -1);
   for (int i = 0; i < 16; ++i)
   {
      char key[32];
      snprintf(key, 32, "spksrc%d", i);
      mModuleSaveData.LoadInt(key, moduleInfo, -1);
      snprintf(key, 32, "spkch%d", i);
      mModuleSaveData.LoadInt(key, moduleInfo, i < 2 ? (i + 1) : 0);
   }
   mModuleSaveData.LoadFloat("modulewidth", moduleInfo, 520.0f);
   mModuleSaveData.LoadFloat("moduleheight", moduleInfo, 520.0f);
   SetUpFromSaveData();
}

void SpatialRender::SetUpFromSaveData()
{
   int loadedSpeakerSources[16];
   int loadedSpeakerChannels[16];
   for (int i = 0; i < 16; ++i)
   {
      char key[32];
      snprintf(key, 32, "spksrc%d", i);
      loadedSpeakerSources[i] = mModuleSaveData.GetInt(key);
      snprintf(key, 32, "spkch%d", i);
      loadedSpeakerChannels[i] = mModuleSaveData.GetInt(key);
   }

   mRoomWidth = mModuleSaveData.GetFloat("roomwidth");
   mRoomDepth = mModuleSaveData.GetFloat("roomdepth");
   mRoomHeight = mModuleSaveData.GetFloat("roomheight");
   mNumSpeakers = mModuleSaveData.GetInt("numspeakers");
   mSPL = mModuleSaveData.GetFloat("spl");
   mUserX = mModuleSaveData.GetFloat("userx");
   mUserY = mModuleSaveData.GetFloat("usery");
   mRoomEffectEnabled = mModuleSaveData.GetBool("roomeffect");
   mReverbMix = mModuleSaveData.GetFloat("reverbmix");
   mDirectSource = mModuleSaveData.GetInt("directsource");
   mBinauralSource = mModuleSaveData.GetInt("binaurausource");
   mModuleWidth = mModuleSaveData.GetFloat("modulewidth");
   mModuleHeight = mModuleSaveData.GetFloat("moduleheight");

   mReverbIdx = 0;

   RebuildSpeakers();

   for (int i = 0; i < 16; ++i)
   {
      mSpeakerSources[i] = loadedSpeakerSources[i];
      mSpeakerChannels[i] = loadedSpeakerChannels[i];
   }

   UpdateDropdownPositions();
   UpdateCablePositions();
}

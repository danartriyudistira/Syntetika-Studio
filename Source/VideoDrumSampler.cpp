#include "VideoDrumSampler.h"
#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "IAudioReceiver.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>
#include <cstdlib>

VideoDrumSampler::VideoDrumSampler() : mNoteInputBuffer(this)
{
   mWriteBuffer.SetNumActiveChannels(2);
   mVideoEngine.getFormatManager().registerFormat(std::make_unique<foleys::FFmpegFormat>());
   for (int i = 0; i < kNumPads; ++i) mButtonHeldVelocity[i] = 0;
}

VideoDrumSampler::~VideoDrumSampler()
{
   TheTransport->RemoveListener(this);
   for (auto& pad : mPads)
      if (pad.mLoaded && mFBO) { NVGcontext* n = mFBO->GetNVGContext(); if (n && pad.mNvgHandle >= 0) { nvgDeleteImage(n, pad.mNvgHandle); pad.mNvgHandle = -1; } }
   delete mFBO;
}

void VideoDrumSampler::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   // Row 1 (Y=3): file + quantize
   mSaveButton = new ClickButton(this, "save", 3, 3); AddUIControl(mSaveButton);
   mLoadButton = new ClickButton(this, "load", 45, 3); AddUIControl(mLoadButton);

   mQuantizeDropdown = new DropdownList(this, "quantize", 90, 3, (int*)&mQuantizeInterval, 55);
   mQuantizeDropdown->AddLabel("none", kInterval_None); mQuantizeDropdown->AddLabel("4n", kInterval_4n);
   mQuantizeDropdown->AddLabel("8n", kInterval_8n);    mQuantizeDropdown->AddLabel("16n", kInterval_16n);
   AddUIControl(mQuantizeDropdown);

   mDisplayModeDropdown = new DropdownList(this, "display", 150, 3, (int*)&mDisplayMode, 60);
   mDisplayModeDropdown->AddLabel("fit", kDisplay_Fit);
   mDisplayModeDropdown->AddLabel("fill", kDisplay_Fill);
   mDisplayModeDropdown->AddLabel("16:9", kDisplay_16x9);
   mDisplayModeDropdown->AddLabel("4:3", kDisplay_4x3);
   mDisplayModeDropdown->AddLabel("1:1", kDisplay_1x1);
   AddUIControl(mDisplayModeDropdown);

   // Row 2 (Y=20): performance
   mNoteRepeatCheckbox = new Checkbox(this, "repeat", 3, 20, &mNoteRepeat); AddUIControl(mNoteRepeatCheckbox);
   mFullVelocityCheckbox = new Checkbox(this, "full vel", 55, 20, &mFullVelocity); AddUIControl(mFullVelocityCheckbox);
   mSingleVoiceCheckbox = new Checkbox(this, "mono", 115, 20, &mSingleVoice); AddUIControl(mSingleVoiceCheckbox);
   mPianoModeCheckbox = new Checkbox(this, "piano", 170, 20, &mPianoMode); AddUIControl(mPianoModeCheckbox);
   mOctDownButton = new ClickButton(this, "-8", 225, 20); AddUIControl(mOctDownButton);
   mOctUpButton = new ClickButton(this, "+8", 252, 20); AddUIControl(mOctUpButton);
   mEditCheckbox = new Checkbox(this, "edit", 282, 20, &mEditMode); AddUIControl(mEditCheckbox);

   // Edit panel (below grid, Y = gridBottom + 8)
   float ey = kGridY + 4 * (kPadSize + kPadGap) + 8;
   mEditVolSlider = new FloatSlider(this, "vol", 5, ey, 80, 15, &mEditVol, 0, 2);
   mEditSpeedSlider = new FloatSlider(this, "speed", 90, ey, 80, 15, &mEditSpeed, 0.1f, 4);
   mEditPanSlider = new FloatSlider(this, "pan", 175, ey, 80, 15, &mEditPan, -1, 1);
   mEditFpsSlider = new FloatSlider(this, "fps", 260, ey, 70, 15, &mEditFps, 1, 120, 0);

   float ey2 = ey + 20;
   mEditTrimStartSlider = new FloatSlider(this, "trim A", 5, ey2, 150, 15, &mEditTrimStart, 0, 1, 3);
   mEditTrimEndSlider = new FloatSlider(this, "trim B", 160, ey2, 150, 15, &mEditTrimEnd, 0, 1, 3);
   mEditLoopCheckbox = new Checkbox(this, "loop", 315, ey2, &mEditLoop);

   float ey3 = ey2 + 20;
   mPlayPadButton = new ClickButton(this, "play pad", 5, ey3);
   mStopPadButton = new ClickButton(this, "stop", 70, ey3);
   mLoadVideoButton = new ClickButton(this, "load video", 120, ey3);
   mClearPadButton = new ClickButton(this, "clear", 195, ey3);

   AddUIControl(mEditVolSlider); AddUIControl(mEditSpeedSlider);
   AddUIControl(mEditPanSlider); AddUIControl(mEditFpsSlider);
   AddUIControl(mEditTrimStartSlider); AddUIControl(mEditTrimEndSlider);
   AddUIControl(mEditLoopCheckbox);
   AddUIControl(mPlayPadButton); AddUIControl(mStopPadButton);
   AddUIControl(mLoadVideoButton); AddUIControl(mClearPadButton);

   mOutputCable = new PatchCableSource(this, kConnectionType_Audio);
   mOutputCable->SetManualPosition(mWidth, mHeight / 2); AddPatchCableSource(mOutputCable);
   mVisualCable = new PatchCableSource(this, kConnectionType_Special);
   mVisualCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
   mVisualCable->SetManualPosition(mWidth, mHeight / 2 + 15); AddPatchCableSource(mVisualCable);
}

void VideoDrumSampler::Init() { IDrawableModule::Init(); TheTransport->AddListener(this, mQuantizeInterval, OffsetInfo(0, true), false); }

void VideoDrumSampler::Process(double time)
{
   PROFILER(VideoDrumSampler);
   IAudioReceiver* target = GetTarget();
   if (!mEnabled || target == nullptr) return;
   mNoteInputBuffer.Process(time);
   ComputeSliders(0);

   int bufSize = target->GetBuffer()->BufferSize();
   mWriteBuffer.Clear();
   juce::AudioBuffer<float> tempBuf(2, bufSize);

   for (auto& pad : mPads)
   {
      if (!pad.mActive || !pad.mLoaded || !pad.mClip || !pad.mClip->hasAudio()) continue;
      tempBuf.clear();
      juce::AudioSourceChannelInfo info(tempBuf); info.startSample = 0; info.numSamples = bufSize;
      pad.mClip->getNextAudioBlock(info);
      float vl = pad.mVol * (1.0f - std::max(0.0f, pad.mPan));
      float vr = pad.mVol * (1.0f + std::min(0.0f, pad.mPan));
      for (int ch = 0; ch < 2; ++ch) {
         const float* src = tempBuf.getReadPointer(ch);
         float* dst = mWriteBuffer.GetChannel(ch);
         float v = (ch == 0) ? vl : vr;
         for (int i = 0; i < bufSize; ++i) dst[i] += src[i] * v;
      }
   }
   SyncOutputBuffer(2);
   for (int ch = 0; ch < 2; ++ch) Add(target->GetBuffer()->GetChannel(ch), mWriteBuffer.GetChannel(ch), bufSize);
}

void VideoDrumSampler::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled) return;

   if (mPianoMode) {
      int slot = pitch - mPianoRootNote;
      if (slot < 0 || slot >= kNumPads) return;
      if (mFullVelocity) velocity = 127;
      if (velocity > 0) {
         if (mQuantizeInterval != kInterval_None) mButtonHeldVelocity[slot] = (float)velocity / 127.0f;
         else TriggerPad(slot, time);
      } else mButtonHeldVelocity[slot] = 0;
      return;
   }

   pitch %= 24; if (pitch < 0 || pitch >= kNumPads) return;
   if (mFullVelocity) velocity = 127;
   if (velocity > 0) {
      if (mQuantizeInterval != kInterval_None) mButtonHeldVelocity[pitch] = (float)velocity / 127.0f;
      else TriggerPad(pitch, time);
   } else mButtonHeldVelocity[pitch] = 0;
}

void VideoDrumSampler::OnTimeEvent(double time)
{
   if (!mEnabled) return;
   for (int i = 0; i < kNumPads; ++i)
      if (mButtonHeldVelocity[i] > 0) { TriggerPad(i, time); if (!mNoteRepeat) mButtonHeldVelocity[i] = 0; }
}

void VideoDrumSampler::TriggerPad(int index, double time)
{
   if (index < 0 || index >= kNumPads) return;
   auto& pad = mPads[index];
   if (!pad.mLoaded) return;

   if (mSingleVoice) for (int i = 0; i < kNumPads; ++i) mPads[i].mActive = false;
   else if (pad.mLinkId != -1) for (int i = 0; i < kNumPads; ++i) if (i != index && mPads[i].mLinkId == pad.mLinkId) mPads[i].mActive = false;

   pad.mActive = true;
   pad.mStartTime = gTime * 0.001;
   pad.mLastTimecode = -1;

   if (pad.mClip) {
      double dur = pad.mClip->getLengthInSeconds();
      double fd = pad.mClip->getFrameDurationInSeconds();
      int total = fd > 0 ? (int)(dur / fd) : 0;
      int ts = ofClamp(pad.mTrimStart, 0, total);
      int te = pad.mTrimEnd > 0 ? ofClamp(pad.mTrimEnd, ts + 1, total) : total;
      int range = std::max(1, te - ts);
      double startSec = (ts + pad.mStartOffset * range) * fd;
      pad.mClip->setNextReadPosition((int64_t)(startSec * pad.mClip->getSampleRate()));
   }
}

int VideoDrumSampler::PadFromClick(float x, float y) const
{
   float r = kGridX + 4 * (kPadSize + kPadGap), b = kGridY + 4 * (kPadSize + kPadGap);
   if (x < kGridX || x >= r || y < kGridY || y >= b) return -1;
   return (int)((x - kGridX) / (kPadSize + kPadGap)) + (int)((y - kGridY) / (kPadSize + kPadGap)) * 4;
}

void VideoDrumSampler::DrawModule()
{
   if (Minimized() || !IsVisible()) return;
   ofPushStyle(); ofSetColor(30, 30, 40); ofFill(); ofRect(0, 0, mWidth, mHeight); ofPopStyle();

   mSaveButton->Draw(); mLoadButton->Draw(); mQuantizeDropdown->Draw();
   mDisplayModeDropdown->Draw();
   mNoteRepeatCheckbox->Draw(); mFullVelocityCheckbox->Draw();
   mSingleVoiceCheckbox->Draw(); mPianoModeCheckbox->Draw();
   mOctDownButton->Draw(); mOctUpButton->Draw();
   mEditCheckbox->Draw();

   if (mPianoMode) {
      ofPushStyle(); ofSetColor(200, 200, 140);
      DrawTextNormal(juce::MidiMessage::getMidiNoteName(mPianoRootNote, true, true, 3).toStdString(), 280, 12, 10);
      ofPopStyle();
   }

   // FBO preview (right of grid)
   float gr = kGridX + 4 * (kPadSize + kPadGap), gb = kGridY + 4 * (kPadSize + kPadGap);
   float previewX = gr + 5, previewW = mWidth - previewX - 5, previewH = gb - kGridY;
   if (previewW > 10 && previewH > 10) {
      ofPushStyle(); ofSetColor(20, 20, 30); ofFill(); ofRect(previewX, kGridY, previewW, previewH);
      ofSetColor(60, 60, 75); ofNoFill(); ofRect(previewX, kGridY, previewW, previewH);
      ofPopStyle();

      if (mFBO && mFBO->IsValid()) {
         mFBO->ReleaseDisplayImage();
         mFBO->Draw(previewX, kGridY, previewW, previewH);
      } else {
         ofPushStyle(); ofSetColor(80, 80, 100);
         DrawTextNormal("no preview", previewX + previewW * 0.25f, kGridY + previewH * 0.5f, 12);
         ofPopStyle();
      }
   }

   // pads
   for (int i = 0; i < kNumPads; ++i) {
      int col = i % 4, row = i / 4;
      float px = kGridX + col * (kPadSize + kPadGap), py = kGridY + row * (kPadSize + kPadGap);
      auto& pad = mPads[i];

      float glow = 0;
      if (pad.mActive && pad.mLoaded && pad.mClip) {
         double dur = pad.mClip->getLengthInSeconds();
         double elapsed = (gTime * 0.001 - pad.mStartTime) * pad.mSpeed;
         double progress = dur > 0 ? ofClamp(elapsed / dur, 0.0, 1.0) : 0;
         glow = (float)(1.0 - progress); if (glow < 0) glow = 0;
      }

      ofPushStyle();
      ofSetColor(pad.mLoaded ? ofColor(60 + (int)(40 * glow), 120 + (int)(80 * glow), 160 + (int)(60 * glow)) : ofColor(50, 50, 60));
      ofFill(); ofRect(px, py, kPadSize, kPadSize); ofPopStyle();

      ofPushStyle(); ofSetColor(200, 200, 220);
      DrawTextNormal(juce::String(i + 1).toStdString(), px + 3, py + 12, 10);
      if (pad.mLoaded) DrawTextNormal("V", px + kPadSize - 14, py + 12, 10);
      if (pad.mLoaded && pad.mClip && pad.mClip->hasAudio()) DrawTextNormal("A", px + kPadSize - 14, py + 24, 10);
      ofPopStyle();

      ofPushStyle(); ofSetColor(60, 60, 70); ofNoFill(); ofRect(px, py, kPadSize, kPadSize); ofPopStyle();

      if (mEditMode && i == mEditIndex) { ofPushStyle(); ofSetColor(255, 200, 50); ofNoFill(); ofRect(px - 1, py - 1, kPadSize + 2, kPadSize + 2); ofPopStyle(); }
      else if (!mEditMode && i == mLastClickedPad) { ofPushStyle(); ofSetColor(200, 200, 100); ofNoFill(); ofRect(px - 1, py - 1, kPadSize + 2, kPadSize + 2); ofPopStyle(); }
   }

   if (mEditMode) { DrawEditPanel(); DrawWaveformTimeline(); }
}

void VideoDrumSampler::DrawEditPanel()
{
   float gb = kGridY + 4 * (kPadSize + kPadGap), ey = gb + 8;
   ofPushStyle(); ofSetColor(35, 35, 48); ofFill(); ofRect(3, ey - 2, mWidth - 6, 65); ofPopStyle();
   ofPushStyle(); ofSetColor(200, 200, 220); DrawTextNormal("Pad " + juce::String(mEditIndex + 1).toStdString(), 7, ey - 4, 11); ofPopStyle();
   mEditVolSlider->Draw(); mEditSpeedSlider->Draw(); mEditPanSlider->Draw(); mEditFpsSlider->Draw();
   mEditTrimStartSlider->Draw(); mEditTrimEndSlider->Draw();
   mEditLoopCheckbox->Draw();
   mPlayPadButton->Draw(); mStopPadButton->Draw();
   mLoadVideoButton->Draw(); mClearPadButton->Draw();
}

void VideoDrumSampler::DrawWaveformTimeline()
{
   if (mEditIndex < 0 || mEditIndex >= kNumPads) return;
   auto& pad = mPads[mEditIndex];
   if (!pad.mLoaded || !pad.mClip) return;

   double dur = pad.mClip->getLengthInSeconds();
   double fDur = pad.mClip->getFrameDurationInSeconds();
   int total = fDur > 0 ? (int)(dur / fDur) : 0;
   if (total <= 0) return;

   float gb = kGridY + 4 * (kPadSize + kPadGap);
   float wy = gb + 78, wh = 40, wx = 5, ww = mWidth - 10;

   ofPushStyle(); ofSetColor(20, 20, 30); ofFill(); ofRect(wx, wy, ww, wh);
   ofSetColor(50, 50, 60); ofNoFill(); ofRect(wx, wy, ww, wh);

   float ppf = ww / total; int step = ppf < 1 ? (int)(1.0f / ppf) + 1 : 1;
   ofPushStyle(); ofSetColor(80, 160, 255, 100);
   for (int px = 0; px < (int)ww; px += step) { int fi = (int)(px / ppf); if (fi >= total) break; ofLine(wx + px, wy + 4, wx + px, wy + wh - 8); }
   ofPopStyle();

   int ts = ofClamp(pad.mTrimStart, 0, total), te = pad.mTrimEnd > 0 ? ofClamp(pad.mTrimEnd, ts + 1, total) : total;
   float ax = wx + (float)ts / total * ww, bx = wx + (float)te / total * ww;

   if (pad.mTrimEnd > 0) { ofPushStyle(); ofSetColor(0, 200, 100, 60); ofFill(); ofRect(ax, wy, bx - ax, wh); ofPopStyle(); }
   ofPushStyle(); ofSetColor(0, 220, 100); ofSetLineWidth(2); ofLine(ax, wy, ax, wy + wh); ofPopStyle();
   ofPushStyle(); ofSetColor(220, 60, 60); ofSetLineWidth(2); ofLine(bx, wy, bx, wy + wh); ofPopStyle();

   ofPushStyle(); ofSetColor(160, 160, 200);
   std::string info = juce::String(total).toStdString() + "f  trim[" + juce::String(ts).toStdString() + "-" + juce::String(te).toStdString() + "] " + juce::String(te - ts).toStdString() + "f";
   DrawTextNormal(info, wx + 4, wy + wh - 10, 9);
   ofPopStyle();
}

void VideoDrumSampler::PostRender()
{
   if (!mEnabled) return;

   // find video dimensions from first loaded pad
   int targetW = 512, targetH = 512;
   for (auto& pad : mPads) {
      if (pad.mLoaded && pad.mClip && pad.mClip->hasVideo()) {
         foleys::Size sz = pad.mClip->getVideoSize();
         if (sz.width > targetW) targetW = sz.width;
         if (sz.height > targetH) targetH = sz.height;
      }
   }
   targetW = std::max(64, targetW);
   targetH = std::max(64, targetH);

   if (!mFBO) mFBO = new VisualFBO();
   if (!mFBO->IsValid() || mFBO->GetWidth() != targetW || mFBO->GetHeight() != targetH) {
      delete mFBO;
      mFBO = new VisualFBO();
      mFBO->Create(targetW, targetH);
   }

   mFBO->Bind();
   float fbw = (float)targetW, fbh = (float)targetH;
   bool anyActive = false;

   for (auto& pad : mPads) {
      if (!pad.mLoaded || !pad.mClip || !pad.mActive) continue;

      double dur = pad.mClip->getLengthInSeconds();
      double fd = pad.mClip->getFrameDurationInSeconds();
      if (fd <= 0) fd = 1.0 / 30.0;
      if (dur <= 0 || dur > 36000.0) dur = std::max(10.0, fd * 100);
      int total = dur > 0 ? (int)(dur / fd) : 0;
      int ts = ofClamp(pad.mTrimStart, 0, total);
      int te = pad.mTrimEnd > 0 ? ofClamp(pad.mTrimEnd, ts + 1, total) : total;
      if (te <= ts) te = total;
      int range = std::max(1, te - ts);

      double startSec = (ts + pad.mStartOffset * range) * fd;
      double elapsed = (gTime * 0.001 - pad.mStartTime) * pad.mSpeed;
      double pos = startSec + elapsed;

      if (pos >= te * fd) {
         if (pad.mLooping) {
            pos = ts * fd + fmod(pos - ts * fd, range * fd);
            pad.mStartTime = gTime * 0.001 - (pos - ts * fd) / pad.mSpeed;
         } else pad.mActive = false;
      }

      if (pad.mActive) { RenderPadFrame(pad, pos, fbw, fbh); anyActive = true; }
   }

   if (!anyActive) {
      // idle: leave black (clip ended)
   }

   mFBO->Unbind();
}

void VideoDrumSampler::RenderPadFrame(Pad& pad, double playheadSec, float fbw, float fbh)
{
   if (!pad.mClip || !pad.mClip->hasVideo()) return;

   // try to get frame; if invalid or same timecode, still draw cached texture
   bool refreshTexture = false;
   int iw = pad.mCachedW, ih = pad.mCachedH;

   foleys::VideoFrame& frame = pad.mClip->getFrame(playheadSec);
   if (frame.image.isValid() && frame.timecode != pad.mLastTimecode) {
      pad.mLastTimecode = frame.timecode;
      refreshTexture = true;
      iw = frame.image.getWidth(); ih = frame.image.getHeight();
   }

   if (iw <= 0 || ih <= 0) return;

   if (refreshTexture) {
      juce::Image::BitmapData bmp(frame.image, juce::Image::BitmapData::readOnly);
      const uint8_t* sd = (const uint8_t*)bmp.data;
      size_t sz = (size_t)iw * ih * 4; if (mConvertBuffer.size() < sz) mConvertBuffer.resize(sz);
      for (int y = 0; y < ih; ++y) { const uint8_t* s = sd + (size_t)y * bmp.lineStride; uint8_t* d = mConvertBuffer.data() + (size_t)y * iw * 4; for (int x = 0; x < iw; ++x) { d[x*4+0]=s[x*4+2]; d[x*4+1]=s[x*4+1]; d[x*4+2]=s[x*4+0]; d[x*4+3]=s[x*4+3]; } }

      NVGcontext* nvg = mFBO ? mFBO->GetNVGContext() : gNanoVG;
      if (pad.mNvgHandle >= 0) { nvgDeleteImage(nvg, pad.mNvgHandle); pad.mNvgHandle = -1; }
      pad.mNvgHandle = nvgCreateImageRGBA(nvg, iw, ih, 0, mConvertBuffer.data());
      pad.mCachedW = iw; pad.mCachedH = ih;
   }

   if (pad.mNvgHandle >= 0) {
      float dx, dy, dw, dh;
      float ir = (float)iw / ih;
      if (mDisplayMode == kDisplay_Fill) { dx = 0; dy = 0; dw = fbw; dh = fbh; }
      else {
         float tr = (mDisplayMode == kDisplay_16x9) ? 16.0f/9.0f : (mDisplayMode == kDisplay_4x3) ? 4.0f/3.0f : (mDisplayMode == kDisplay_1x1) ? 1.0f : ir;
         float s = std::min(fbw / tr, fbh); dw = tr * s; dh = s;
         dx = (fbw - dw) / 2; dy = (fbh - dh) / 2;
      }
      NVGpaint p = nvgImagePattern(gNanoVG, dx, dy, dw, dh, 0, pad.mNvgHandle, pad.mVol);
      nvgBeginPath(gNanoVG); nvgRect(gNanoVG, dx, dy, dw, dh); nvgFillPaint(gNanoVG, p); nvgFill(gNanoVG);
   }
}

VisualFBO* VideoDrumSampler::GetFBO() { return mFBO; }

void VideoDrumSampler::ButtonClicked(ClickButton* button, double time)
{
   if (button == mSaveButton) SaveKit();
   else if (button == mLoadButton) LoadKit();
   else if (button == mOctDownButton) { mPianoRootNote = std::max(0, mPianoRootNote - 12); }
   else if (button == mOctUpButton)   { mPianoRootNote = std::min(108, mPianoRootNote + 12); }
   else if (button == mPlayPadButton && mEditIndex >= 0)
      TriggerPad(mEditIndex, gTime * 0.001);
   else if (button == mStopPadButton && mEditIndex >= 0)
      mPads[mEditIndex].mActive = false;
   else if (button == mLoadVideoButton && mEditIndex >= 0 && mEditIndex < kNumPads) {
      juce::FileChooser chooser("Select Video for Pad " + juce::String(mEditIndex + 1), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mp4;*.mov;*.avi;*.mkv", true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen()) { mPads[mEditIndex].mVideoPath = chooser.getResult().getFullPathName().toStdString(); LoadPadVideo(mEditIndex); }
   }
   else if (button == mClearPadButton && mEditIndex >= 0 && mEditIndex < kNumPads) ClearPad(mEditIndex);
}

void VideoDrumSampler::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (mEditIndex < 0 || mEditIndex >= kNumPads) return;
   auto& pad = mPads[mEditIndex];

   if (slider == mEditVolSlider) pad.mVol = mEditVol;
   else if (slider == mEditSpeedSlider) pad.mSpeed = mEditSpeed;
   else if (slider == mEditPanSlider) pad.mPan = mEditPan;
   else if (slider == mEditFpsSlider) pad.mFps = mEditFps;
   else if (slider == mEditStartOffsetSlider) pad.mStartOffset = mEditStartOffset;
   else if (slider == mEditTrimStartSlider || slider == mEditTrimEndSlider) {
      int total = 100;
      if (pad.mClip) {
         double dur = pad.mClip->getLengthInSeconds();
         double fd = pad.mClip->getFrameDurationInSeconds();
         if (dur <= 0 || dur > 36000.0) dur = (fd > 0 ? fd * 100 : 10.0);
         if (fd > 0) total = (int)(dur / fd);
      }
      if (total <= 0) total = 100;

      int ts = (int)(mEditTrimStart * total);
      int te = (int)(mEditTrimEnd * total);
      if (ts < 0) ts = 0;
      if (te > total) te = total;
      if (ts >= te) {
         if (slider == mEditTrimStartSlider) ts = te - 1;
         else te = ts + 1;
      }
      pad.mTrimStart = ts;
      pad.mTrimEnd = te;
   }
}

void VideoDrumSampler::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (checkbox == mEditCheckbox) { if (mEditMode && mEditIndex < 0) { mEditIndex = 0; SyncEditVars(); } if (!mEditMode) mEditIndex = -1; }
   else if (checkbox == mEditLoopCheckbox && mEditIndex >= 0 && mEditIndex < kNumPads) mPads[mEditIndex].mLooping = mEditLoop;
}

void VideoDrumSampler::OnClicked(float x, float y, bool right)
{
   if (right) { IDrawableModule::OnClicked(x, y, right); return; }
   int idx = PadFromClick(x, y); if (idx < 0) return;
   if (mEditMode) { mEditIndex = idx; SyncEditVars(); }
   else { mLastClickedPad = idx; TriggerPad(idx, gTime * 0.001); }
}

void VideoDrumSampler::SyncEditVars()
{
   if (mEditIndex < 0 || mEditIndex >= kNumPads) return;
   auto& pad = mPads[mEditIndex];
   mEditVol = pad.mVol; mEditSpeed = pad.mSpeed; mEditPan = pad.mPan; mEditFps = pad.mFps;
   mEditLoop = pad.mLooping; mEditStartOffset = pad.mStartOffset;

   int total = 100;
   if (pad.mClip) {
      double dur = pad.mClip->getLengthInSeconds();
      double fd = pad.mClip->getFrameDurationInSeconds();
      if (dur <= 0 || dur > 36000.0) dur = (fd > 0 ? fd * 100 : 10.0);
      if (fd > 0) total = (int)(dur / fd);
   }
   if (total <= 0) total = 100;
   mEditTrimStart = total > 0 ? (float)pad.mTrimStart / total : 0;
   mEditTrimEnd   = total > 0 ? (float)pad.mTrimEnd   / total : 1;
}

void VideoDrumSampler::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mQuantizeDropdown) { TheTransport->RemoveListener(this); TheTransport->AddListener(this, mQuantizeInterval, OffsetInfo(0, true), false); }
}

void VideoDrumSampler::GetModuleDimensions(float& w, float& h) { w = mWidth; h = mEditMode ? 400.0f : mHeight; }
void VideoDrumSampler::Resize(float w, float h) { mWidth = ofClamp(w, 300, 9999); mHeight = ofClamp(h, 250, 9999); if (mOutputCable) mOutputCable->SetManualPosition(mWidth, mHeight / 2); if (mVisualCable) mVisualCable->SetManualPosition(mWidth, mHeight / 2 + 15); }

void VideoDrumSampler::LoadPadVideo(int index)
{
   if (index < 0 || index >= kNumPads) return;
   auto& pad = mPads[index]; ClearPad(index);
   if (pad.mVideoPath.empty()) return;
   juce::File f(pad.mVideoPath); if (!f.existsAsFile()) return;

   auto clip = mVideoEngine.createClipFromFile(juce::URL(f));
   pad.mClip = std::dynamic_pointer_cast<foleys::MovieClip>(clip);
   if (!pad.mClip) return;

   pad.mClip->prepareToPlay(1024, gSampleRate);
   foleys::Size sz = pad.mClip->getVideoSize();
   pad.mLoaded = sz.width > 0 && sz.height > 0;
   double dur = pad.mClip->getLengthInSeconds();
   double fd = pad.mClip->getFrameDurationInSeconds();
   // video-only files have sampleRate=0 → getLengthInSeconds() returns inf/NaN
   if (dur <= 0 || dur > 36000.0) {
      // estimate: assume 30fps, max 5 min; actual playback bounded by decoder EOF
      dur = fd > 0 ? 300.0 : 10.0;
   }
   pad.mFps = fd > 0 ? (float)(1.0 / fd) : 30.0f;
   pad.mTrimStart = 0;
   pad.mTrimEnd = fd > 0 ? (int)(dur / fd) : 100;
   if (mEditIndex == index) SyncEditVars();
}

void VideoDrumSampler::ClearPad(int index)
{
   if (index < 0 || index >= kNumPads) return;
   auto& pad = mPads[index];
   if (mFBO) { NVGcontext* n = mFBO->GetNVGContext(); if (n && pad.mNvgHandle >= 0) { nvgDeleteImage(n, pad.mNvgHandle); pad.mNvgHandle = -1; } }
   pad.mClip.reset(); pad.mLoaded = false; pad.mActive = false;
   pad.mTrimStart = 0; pad.mTrimEnd = 0; pad.mCachedW = 0; pad.mCachedH = 0;
}

void VideoDrumSampler::SaveKit()
{
   juce::FileChooser chooser("Save Drum Kit", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.vds", true, false, TheSynth->GetFileChooserParent());
   if (!chooser.browseForFileToSave(true)) return;
   juce::File file = chooser.getResult(); if (file.getFileExtension().isEmpty()) file = file.withFileExtension(".vds");

   juce::DynamicObject::Ptr root = new juce::DynamicObject();
   root->setProperty("quantizeInterval", (int)mQuantizeInterval);
   root->setProperty("noteRepeat", mNoteRepeat); root->setProperty("fullVelocity", mFullVelocity);
   root->setProperty("singleVoice", mSingleVoice); root->setProperty("pianoMode", mPianoMode);
   root->setProperty("pianoRootNote", mPianoRootNote);

   juce::Array<juce::var> pads;
   for (int i = 0; i < kNumPads; ++i) {
      juce::DynamicObject::Ptr po = new juce::DynamicObject();
      po->setProperty("videoPath", juce::String(mPads[i].mVideoPath));
      po->setProperty("vol", mPads[i].mVol); po->setProperty("speed", mPads[i].mSpeed);
      po->setProperty("pan", mPads[i].mPan); po->setProperty("fps", mPads[i].mFps);
      po->setProperty("loop", mPads[i].mLooping); po->setProperty("linkId", mPads[i].mLinkId);
      po->setProperty("trimStart", mPads[i].mTrimStart); po->setProperty("trimEnd", mPads[i].mTrimEnd);
      po->setProperty("startOffset", mPads[i].mStartOffset);
      pads.add(juce::var(po.get()));
   }
   root->setProperty("pads", pads);
   file.replaceWithText(juce::JSON::toString(juce::var(root.get())));
}

void VideoDrumSampler::LoadKit()
{
   juce::FileChooser chooser("Load Drum Kit", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.vds", true, false, TheSynth->GetFileChooserParent());
   if (!chooser.browseForFileToOpen()) return;
   juce::var json = juce::JSON::parse(chooser.getResult().loadFileAsString());
   if (!json.isObject()) return;

   for (int i = 0; i < kNumPads; ++i) ClearPad(i);
   mQuantizeInterval = (NoteInterval)(int)json.getProperty("quantizeInterval", (int)kInterval_None);
   mNoteRepeat = json.getProperty("noteRepeat", false); mFullVelocity = json.getProperty("fullVelocity", false);
   mSingleVoice = json.getProperty("singleVoice", false);
   mPianoMode = json.getProperty("pianoMode", false);
   mPianoRootNote = json.getProperty("pianoRootNote", 36);

   if (json.hasProperty("pads") && json["pads"].isArray()) {
      auto* pads = json["pads"].getArray();
      for (int i = 0; i < juce::jmin((int)pads->size(), kNumPads); ++i) {
         juce::var p = (*pads)[i]; if (!p.isObject()) continue;
         mPads[i].mVideoPath = p.getProperty("videoPath", "").toString().toStdString();
         mPads[i].mVol = p.getProperty("vol", 1.0f); mPads[i].mSpeed = p.getProperty("speed", 1.0f);
         mPads[i].mPan = p.getProperty("pan", 0.0f); mPads[i].mFps = p.getProperty("fps", 30.0f);
         mPads[i].mLooping = p.getProperty("loop", false); mPads[i].mLinkId = p.getProperty("linkId", -1);
         mPads[i].mTrimStart = p.getProperty("trimStart", 0); mPads[i].mTrimEnd = p.getProperty("trimEnd", 0);
         mPads[i].mStartOffset = p.getProperty("startOffset", 0.0f);
         if (!mPads[i].mVideoPath.empty()) LoadPadVideo(i);
      }
   }
   if (mEditIndex >= 0) SyncEditVars();
   TheTransport->RemoveListener(this); TheTransport->AddListener(this, mQuantizeInterval, OffsetInfo(0, true), false);
}

void VideoDrumSampler::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << (int&)mQuantizeInterval; out << mNoteRepeat; out << mFullVelocity; out << mSingleVoice;
   out << mPianoMode; out << mPianoRootNote;
   for (int i = 0; i < kNumPads; ++i) {
      out << mPads[i].mVideoPath; out << mPads[i].mVol; out << mPads[i].mSpeed;
      out << mPads[i].mPan; out << mPads[i].mFps; out << mPads[i].mLooping; out << mPads[i].mLinkId;
      out << mPads[i].mStartOffset; out << mPads[i].mTrimStart; out << mPads[i].mTrimEnd;
   }
}

void VideoDrumSampler::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev >= 2) { in >> (int&)mQuantizeInterval; in >> mNoteRepeat; in >> mFullVelocity; in >> mSingleVoice;
      if (rev >= 3) { in >> mPianoMode; in >> mPianoRootNote; }
   }
   for (int i = 0; i < kNumPads; ++i) {
      std::string vp; in >> vp; in >> mPads[i].mVol; in >> mPads[i].mSpeed;
      in >> mPads[i].mPan; in >> mPads[i].mFps; in >> mPads[i].mLooping; in >> mPads[i].mLinkId;
      if (rev >= 2) { in >> mPads[i].mStartOffset; in >> mPads[i].mTrimStart; in >> mPads[i].mTrimEnd; }
      mPads[i].mVideoPath = vp; if (!vp.empty()) LoadPadVideo(i);
   }
}

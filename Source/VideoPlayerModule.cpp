#include "VideoPlayerModule.h"
#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "IAudioReceiver.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>

#pragma warning(push)
#pragma warning(disable : 4996)

VideoPlayerModule::VideoPlayerModule()
{
   mWriteBuffer.SetNumActiveChannels(2);
   mVideoEngine.getFormatManager().registerFormat(std::make_unique<foleys::FFmpegFormat>());
}

VideoPlayerModule::~VideoPlayerModule()
{
   if (mCurrentFrameHandle >= 0 && mFBO && mFBO->IsValid())
   {
      NVGcontext* fboNVG = mFBO->GetNVGContext();
      if (fboNVG)
         nvgDeleteImage(fboNVG, mCurrentFrameHandle);
      mCurrentFrameHandle = -1;
   }
   delete mFBO;
   mClip.reset();
}

void VideoPlayerModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   // Row 1: transport controls
   float x = 3;
   mOpenButton = new ClickButton(this, "open", x, kControlY); x += 42;
   AddUIControl(mOpenButton);

   mPlayButton = new ClickButton(this, "play", x, kControlY); x += 42;
   AddUIControl(mPlayButton);

   mPauseButton = new ClickButton(this, "pause", x, kControlY); x += 48;
   AddUIControl(mPauseButton);

   mStopButton = new ClickButton(this, "stop", x, kControlY); x += 42;
   AddUIControl(mStopButton);

   // Row 2: cue + loop + speed
   x = 3;
   mCueButton = new ClickButton(this, "cue", x, kRow2Y); x += 38;
   AddUIControl(mCueButton);

   mLoopCheckbox = new Checkbox(this, "loop", x, kRow2Y, &mLoop); x += 46;
   AddUIControl(mLoopCheckbox);

   mLoopInButton = new ClickButton(this, "in", x, kRow2Y); x += 30;
   AddUIControl(mLoopInButton);

   mLoopOutButton = new ClickButton(this, "out", x, kRow2Y); x += 36;
   AddUIControl(mLoopOutButton);

   mLoopClearButton = new ClickButton(this, "clr", x, kRow2Y); x += 32;
   AddUIControl(mLoopClearButton);

   x += 2;
   mSpeedSlider = new FloatSlider(this, "spd", x, kRow2Y, 80, kControlH, &mSpeed, 0.05f, 8.0f, 2);
   AddUIControl(mSpeedSlider);

   // Row 3: dropdowns + hotcues
   mSpeedRangeDropdown = new DropdownList(this, "range", 3, kRow3Y, (int*)&mSpeedRange);
   mSpeedRangeDropdown->AddLabel("1x", kSpeedRange_1x);
   mSpeedRangeDropdown->AddLabel("2x", kSpeedRange_2x);
   mSpeedRangeDropdown->AddLabel("4x", kSpeedRange_4x);
   mSpeedRangeDropdown->AddLabel("8x", kSpeedRange_8x);
   AddUIControl(mSpeedRangeDropdown);

   mCueModeDropdown = new DropdownList(this, "cue mode", 75, kRow3Y, (int*)&mCueMode);
   mCueModeDropdown->AddLabel("jump", kCueMode_Jump);
   mCueModeDropdown->AddLabel("set", kCueMode_Set);
   AddUIControl(mCueModeDropdown);

   float hx = 162;
   for (int i = 0; i < kNumHotcues; ++i)
   {
      mHotcuePosition[i] = -1;
      mHotcueButton[i] = new ClickButton(this, ofToString(i + 1).c_str(), hx, kRow3Y);
      hx += 28;
      AddUIControl(mHotcueButton[i]);
   }

   // Row 4: position slider (full width)
   mPositionSlider = new FloatSlider(this, "pos", 3, kRow4Y, (int)mWidth - 6, kControlH, &mPlayheadFloat, 0, 1);
   AddUIControl(mPositionSlider);

   // Cables
   mOutputCable = new PatchCableSource(this, kConnectionType_Audio);
   mOutputCable->SetManualPosition(mWidth, mHeight - 25);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);

   mVisualCable = new PatchCableSource(this, kConnectionType_Special);
   mVisualCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
   mVisualCable->SetManualPosition(mWidth, mHeight - 10);
   mVisualCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mVisualCable);

   mSpeedSlider->SetExtents(0.05f, mSpeedRangeValues[mSpeedRange]);
}

void VideoPlayerModule::Process(double time)
{
   PROFILER(VideoPlayerModule);

   IAudioReceiver* target = GetTarget();
   if (!mEnabled || target == nullptr)
      return;

   ComputeSliders(0);

   int bufferSize = target->GetBuffer()->BufferSize();
   mWriteBuffer.Clear();

   if (mSeekPending.load(std::memory_order_acquire))
   {
      mSeekPending.store(false, std::memory_order_relaxed);
      if (mClip)
      {
         mClip->setNextReadPosition((int64_t)(mSeekTarget * mClip->getSampleRate()));
         mClip->waitForSamplesReady(bufferSize, 30);
      }
   }

   if (mClip && mClip->hasAudio() && mPlaying)
   {
      juce::AudioBuffer<float> tempBuf(2, bufferSize);
      tempBuf.clear();
      juce::AudioSourceChannelInfo info(tempBuf);
      info.startSample = 0;
      info.numSamples = bufferSize;
      mClip->getNextAudioBlock(info);

      for (int ch = 0; ch < 2; ++ch)
      {
         const float* src = tempBuf.getReadPointer(ch);
         float* dst = mWriteBuffer.GetChannel(ch);
         for (int i = 0; i < bufferSize; ++i)
            dst[i] = src[i];
      }
   }

   SyncOutputBuffer(2);
   for (int ch = 0; ch < 2; ++ch)
      Add(target->GetBuffer()->GetChannel(ch), mWriteBuffer.GetChannel(ch), bufferSize);
}

void VideoPlayerModule::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mHasVideo)
      return;

   int slot = pitch % kNumHotcues;

   if (velocity > 0)
   {
      if (mHotcuePosition[slot] >= 0)
      {
         if (!mPlaying)
            mPlaying = true;
         SetPosition(mHotcuePosition[slot]);
      }
      else
      {
         mHotcuePosition[slot] = mPlayhead;
      }
   }
}

void VideoPlayerModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mOpenButton)
      LoadFile();
    else if (button == mPlayButton)
    {
       if (mHasVideo)
       {
          if (mPlayhead >= mDuration)
             SetPosition(0);
          mPlaying = true;
          mPlayStartTime = gTime * 0.001 - mPlayhead / mSpeed;
       }
    }
   else if (button == mPauseButton)
      mPlaying = false;
   else if (button == mStopButton)
   {
      mPlaying = false;
      SetPosition(0);
   }
   else if (button == mCueButton)
   {
      if (mCueMode == kCueMode_Set)
      {
         mHasCue = true;
         mCuePoint = mPlayhead;
      }
      else
      {
         if (mHasCue)
            SetPosition(mCuePoint);
      }
   }
   else if (button == mLoopInButton)
   {
      if (mLoopOut >= 0 && mPlayhead > mLoopOut)
         mLoopIn = mLoopOut;
      else
         mLoopIn = mPlayhead;
      if (mLoopIn >= 0 && mLoopOut >= 0 && mLoopOut > mLoopIn)
         mLoopSectionActive = true;
   }
   else if (button == mLoopOutButton)
   {
      if (mLoopIn >= 0 && mPlayhead < mLoopIn)
         mLoopOut = mLoopIn;
      else
         mLoopOut = mPlayhead;
      if (mLoopIn >= 0 && mLoopOut >= 0 && mLoopOut > mLoopIn)
         mLoopSectionActive = true;
   }
    else if (button == mLoopClearButton)
    {
       mLoopIn = -1;
       mLoopOut = -1;
       mLoopSectionActive = false;
    }
    else
    {
       for (int i = 0; i < kNumHotcues; ++i)
       {
          if (button == mHotcueButton[i])
          {
          if (mHotcuePosition[i] < 0)
          {
             mHotcuePosition[i] = mPlayhead;
          }
          else
          {
             if (!mPlaying)
                mPlaying = true;
             SetPosition(mHotcuePosition[i]);
          }
             return;
          }
       }
    }
}

void VideoPlayerModule::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (slider == mPositionSlider && !mScrubbing)
   {
      SetPosition(mPlayheadFloat * mDuration);
   }

   if (slider == mSpeedSlider && mPlaying)
   {
      mPlayStartTime = gTime * 0.001 - mPlayhead / mSpeed;
   }
}

void VideoPlayerModule::CheckboxUpdated(Checkbox* checkbox, double time)
{
}

void VideoPlayerModule::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mSpeedRangeDropdown)
   {
      mSpeedSlider->SetExtents(0.05f, mSpeedRangeValues[mSpeedRange]);
      mSpeed = ofClamp(mSpeed, 0.05f, mSpeedRangeValues[mSpeedRange]);
       if (mPlaying)
          mPlayStartTime = gTime * 0.001 - mPlayhead / mSpeed;
   }
}

void VideoPlayerModule::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

void VideoPlayerModule::Resize(float width, float height)
{
   mWidth = ofClamp(width, 380, 9999);
   mHeight = ofClamp(height, 200, 9999);

   if (mPositionSlider)
      mPositionSlider->SetDimensions((int)mWidth - 6, kControlH);

   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, mHeight - 25);
   if (mVisualCable)
      mVisualCable->SetManualPosition(mWidth, mHeight - 10);
}

static const char* TimeStr(double seconds)
{
   int secs = (int)seconds;
   int mins = secs / 60;
   secs %= 60;
   static char buf[32];
   snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
   return buf;
}

void VideoPlayerModule::DrawModule()
{
   if (Minimized() || !IsVisible())
      return;

   // ── dark background behind all controls ──
   ofPushStyle();
   ofSetColor(25, 25, 35);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   mOpenButton->Draw();
   mPlayButton->Draw();
   mPauseButton->Draw();
   mStopButton->Draw();
   mCueButton->Draw();
   mSpeedSlider->Draw();
   mLoopCheckbox->Draw();
   mLoopInButton->Draw();
   mLoopOutButton->Draw();
   mLoopClearButton->Draw();
   mPositionSlider->Draw();
    mSpeedRangeDropdown->Draw();
    mCueModeDropdown->Draw();
    for (int i = 0; i < kNumHotcues; ++i)
       mHotcueButton[i]->Draw();

    if (!mHasVideo)
   {
      ofPushStyle();
      ofSetColor(80, 80, 100);
      DrawTextNormal("open a video file", kMargin, kTimelineY + kTimelineH / 2, 11);
      ofPopStyle();
      return;
   }

   // ── info bar ──
   ofPushStyle();
   ofFill();
   ofSetColor(0, 0, 0, 200);
   ofRect(kMargin, kInfoY, mWidth - kMargin * 2, kInfoH);
   ofSetColor(200, 200, 220);
   int currentFrame = (int)(mPlayhead * mFps);
   std::string info = std::string(TimeStr(mPlayhead)) + " / " + std::string(TimeStr(mDuration))
      + "  " + ofToString((int)(mFps + 0.5f)) + "fps"
      + "  f" + ofToString(currentFrame);
   if (mPlaying)
      info += "  [PLAY]";
   else
      info += "  [STOP]";
   if (mLoop)
      info += "  [LOOP]";
   if (mLoopSectionActive)
      info += "  [A-LOOP " + std::string(TimeStr(mLoopIn)) + "-" + std::string(TimeStr(mLoopOut)) + "]";
   if (mHasCue)
      info += "  [CUE " + std::string(TimeStr(mCuePoint)) + "]";
   if (mClip && mClip->hasAudio())
      info += "  [AUDIO]";
   DrawTextNormal(info, kMargin + 3, kInfoY + 4, 8);
   ofPopStyle();

    // ── single zoomable waveform timeline ──
    float tw = mWidth - kMargin * 2;
    float tY = kTimelineY;
    float tH = kTimelineH;
    ofPushStyle();
    ofSetColor(20, 20, 30);
    ofFill();
    ofRect(kMargin, tY, tw, tH);
    ofSetColor(50, 50, 60);
    ofNoFill();
    ofRect(kMargin, tY, tw, tH);

    double zs = mTimelineViewStart;
    double dur = mScrollZoomSeconds;
    if (zs + dur > mDuration) dur = mDuration - zs;
    if (dur <= 0) dur = mScrollZoomSeconds;

    if (mClip && mClip->hasAudio() && !mWaveformOverview.empty())
    {
       double ratio = (double)mWaveformOverview.size() / mDuration;
       ofPushStyle();
       ofSetColor(80, 160, 255, 150);
       float cy = tY + tH / 2;
       float maxH = (tH - 6) / 2;
       for (int px = 0; px < (int)tw; ++px)
       {
          double t = zs + px * dur / tw;
          int idx = (int)(t * ratio);
          if (idx < 0) idx = 0;
          if (idx >= (int)mWaveformOverview.size()) idx = (int)mWaveformOverview.size() - 1;
          float h = mWaveformOverview[idx] * maxH;
          if (h > 0.5f)
             ofLine(kMargin + px, cy - h, kMargin + px, cy + h);
       }
       ofPopStyle();
    }

    // playhead
    float phPx = SecondsToPixel(mPlayhead);
    ofSetColor(255, 200, 50);
    ofSetLineWidth(2);
    ofLine(phPx, tY, phPx, tY + tH);

    // loop markers
    if (mLoopIn >= 0)
    {
       ofSetColor(0, 200, 100);
       ofLine(SecondsToPixel(mLoopIn), tY, SecondsToPixel(mLoopIn), tY + tH);
    }
    if (mLoopOut >= 0)
    {
       ofSetColor(200, 50, 50);
       ofLine(SecondsToPixel(mLoopOut), tY, SecondsToPixel(mLoopOut), tY + tH);
    }
    if (mLoopSectionActive && mLoopIn >= 0 && mLoopOut >= 0)
    {
       float lx = SecondsToPixel(mLoopIn);
       float rx = SecondsToPixel(mLoopOut);
       ofSetColor(0, 200, 100, 40);
       ofFill();
       ofRect(lx, tY, rx - lx, tH);
    }
    if (mHasCue)
    {
       float cx = SecondsToPixel(mCuePoint);
       ofSetColor(100, 200, 255);
       ofTriangle(cx - 5, tY + tH, cx + 5, tY + tH, cx, tY + tH - 10);
    }

    // hotcue markers
    ofColor hotColors[8] = {
       ofColor(255,100,100), ofColor(255,180,60), ofColor(255,255,60),
       ofColor(100,255,100), ofColor(60,220,255), ofColor(100,120,255),
       ofColor(220,100,255), ofColor(255,255,255)
    };
    for (int i = 0; i < kNumHotcues; ++i)
    {
       if (mHotcuePosition[i] >= 0)
       {
          float hx = SecondsToPixel(mHotcuePosition[i]);
          ofSetColor(hotColors[i]);
          ofTriangle(hx - 4, tY, hx + 4, tY, hx, tY + 8);
          DrawTextNormal(ofToString(i + 1), hx - 2, tY - 1, 8);
       }
    }
    ofPopStyle();

    // ── video preview below timeline ──
    float previewTop = tY + tH + 2;
    float previewH = mHeight - previewTop - kMargin;
    if (mFBO && mFBO->IsValid() && previewH > 10)
    {
       ofPushStyle();
       ofSetColor(50, 50, 60);
       ofNoFill();
       ofRect(kMargin - 1, previewTop - 1, mWidth - kMargin * 2 + 2, previewH + 2);
       ofPopStyle();
       mFBO->ReleaseDisplayImage();
       mFBO->Draw(kMargin, previewTop, mWidth - kMargin * 2, previewH);
    }
}

void VideoPlayerModule::PostRender()
{
   if (!mEnabled || !mClip)
      return;

   double t = gTime * 0.001;

   if (mPlaying)
   {
      double elapsed = t - mPlayStartTime;
      mPlayhead = elapsed * mSpeed;

      if (mLoopSectionActive && mLoopIn >= 0 && mLoopOut >= 0 && mLoopOut > mLoopIn)
      {
         if (mPlayhead >= mLoopOut)
         {
            mPlayhead = mLoopIn;
            mPlayStartTime = t - mPlayhead / mSpeed;
            mSeekTarget = mPlayhead;
            mSeekPending.store(true, std::memory_order_release);
            mLastFrameTimecode = -1;
         }
      }
      if (mLoop && mPlayhead >= mDuration)
      {
         mPlayhead = 0;
         mPlayStartTime = t - mPlayhead / mSpeed;
         mSeekTarget = mPlayhead;
         mSeekPending.store(true, std::memory_order_release);
         mLastFrameTimecode = -1;
      }
      if (mPlayhead >= mDuration)
      {
         mPlayhead = mDuration;
         mPlaying = false;
      }
      if (mPlayhead < 0) mPlayhead = 0;
      mPlayheadFloat = (float)(mDuration > 0 ? mPlayhead / mDuration : 0);
   }

   // sync playhead from slider drag (slider directly writes mPlayheadFloat, may not fire callback)
   if (!mPlaying && mDuration > 0)
   {
      double sliderPosition = (double)mPlayheadFloat * mDuration;
      if (std::abs(sliderPosition - mPlayhead) > 0.0005)
      {
         mPlayhead = ofClamp(sliderPosition, 0.0, mDuration);
         mLastFrameTimecode = -1;
      }
   }

   // ── compute visible timeline window ──
   {
      double zs = mPlayhead - mScrollZoomSeconds / 2.0;
      double ze = mPlayhead + mScrollZoomSeconds / 2.0;
      if (zs < 0) { ze -= zs; zs = 0; }
      if (ze > mDuration) { zs -= (ze - mDuration); ze = mDuration; }
      if (zs < 0) zs = 0;
      mTimelineViewStart = zs;
   }

   if (mClip->hasVideo())
   {
      juce::Image img;
      bool newFrame = false;

      if (mPlaying)
      {
         if (mClip->isFrameAvailable(mPlayhead))
         {
            foleys::VideoFrame& frame = mClip->getFrame(mPlayhead);
            if (frame.image.isValid() && frame.timecode != mLastFrameTimecode)
            {
               mLastFrameTimecode = frame.timecode;
               img = frame.image;
               newFrame = true;
            }
         }
      }
      else
      {
         // slider scrub: seek decoder then pull from FIFO (non-blocking, 1-frame delay)
         if (mLastFrameTimecode < 0)
         {
            if (mClip->hasAudio())
               mClip->setNextReadPosition((int64_t)(mPlayhead * mClip->getSampleRate()));
            else
               mClip->setNextReadPosition(0);
            mLastFrameTimecode = -2; // mark as "seeking, wait for decoder"
         }

         if (mLastFrameTimecode == -2 && mClip->isFrameAvailable(mPlayhead))
         {
            foleys::VideoFrame& frame = mClip->getFrame(mPlayhead);
            if (frame.image.isValid())
            {
               mLastFrameTimecode = frame.timecode;
               img = frame.image;
               newFrame = true;
            }
         }
      }

      if (newFrame)
      {
         int w = img.getWidth();
         int h = img.getHeight();

         if (!mFBO)
            mFBO = new VisualFBO();
         if (!mFBO->IsValid() || mFBO->GetWidth() != w || mFBO->GetHeight() != h)
         {
            delete mFBO;
            mFBO = new VisualFBO();
            mFBO->Create(std::max(64, w), std::max(64, h));
         }

         juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readOnly);
         const uint8_t* srcData = (const uint8_t*)bmp.data;
         std::vector<uint8_t> rgba((size_t)w * h * 4);

         for (int y = 0; y < h; ++y)
         {
            const uint8_t* src = srcData + (size_t)y * bmp.lineStride;
            uint8_t* dst = rgba.data() + (size_t)y * w * 4;
            for (int x = 0; x < w; ++x)
            {
               dst[x * 4 + 0] = src[x * 4 + 2];
               dst[x * 4 + 1] = src[x * 4 + 1];
               dst[x * 4 + 2] = src[x * 4 + 0];
               dst[x * 4 + 3] = src[x * 4 + 3];
            }
         }

         mFBO->Bind();

         if (mCurrentFrameHandle >= 0)
            nvgDeleteImage(gNanoVG, mCurrentFrameHandle);
         mCurrentFrameHandle = nvgCreateImageRGBA(gNanoVG, w, h, 0, rgba.data());

         if (mCurrentFrameHandle >= 0)
         {
            NVGpaint imgPaint = nvgImagePattern(gNanoVG, 0, 0, (float)w, (float)h, 0.0f, mCurrentFrameHandle, 1.0f);
            nvgBeginPath(gNanoVG);
            nvgRect(gNanoVG, 0, 0, (float)w, (float)h);
            nvgFillPaint(gNanoVG, imgPaint);
            nvgFill(gNanoVG);
         }
         mFBO->Unbind();
      }
   }
}

VisualFBO* VideoPlayerModule::GetFBO()
{
   return mFBO;
}

void VideoPlayerModule::LoadFile()
{
   juce::FileChooser chooser("Select Video File", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.mp4;*.mov;*.avi;*.mkv;*.webm", true, false, TheSynth->GetFileChooserParent());
   if (chooser.browseForFileToOpen())
      LoadFromPath(chooser.getResult().getFullPathName().toStdString());
}

void VideoPlayerModule::LoadFromPath(const std::string& path)
{
   UnloadVideo();
   mVideoPath = path;

   auto clip = mVideoEngine.createClipFromFile(juce::URL(juce::File(path)));
   if (!clip)
      return;

   mClip = std::dynamic_pointer_cast<foleys::MovieClip>(clip);
   if (!mClip)
      return;

   // Metadata
   foleys::Size size = mClip->getVideoSize();
   mVideoW = size.width;
   mVideoH = size.height;
   mDuration = mClip->getLengthInSeconds();
   if (mDuration <= 0) mDuration = 1.0;
   mFps = mClip->getFrameDurationInSeconds() > 0
      ? 1.0 / mClip->getFrameDurationInSeconds() : 30.0;

    // Audio setup
    mClip->prepareToPlay(1024, gSampleRate);
    if (mClip->hasAudio())
    {
       mClip->setNextReadPosition(0);
       ComputeWaveform();
    }

   mHasVideo = true;
   mPlaying = false;
   SetPosition(0);
   mLoopIn = -1;
   mLoopOut = -1;
   mLoopSectionActive = false;
   mHasCue = false;
}

void VideoPlayerModule::UnloadVideo()
{
   if (mCurrentFrameHandle >= 0 && mFBO && mFBO->IsValid())
   {
      NVGcontext* fboNVG = mFBO->GetNVGContext();
      if (fboNVG)
         nvgDeleteImage(fboNVG, mCurrentFrameHandle);
      mCurrentFrameHandle = -1;
   }
   mClip.reset();
   mWaveformOverview.clear();
   mHasVideo = false;
   mPlaying = false;
   mPlayhead = 0;
   mDuration = 0;
   mVideoW = 0;
   mVideoH = 0;
   mFps = 30.0;
   mLastFrameTimecode = -1;
}

void VideoPlayerModule::SetPosition(double seconds)
{
   mPlayhead = ofClamp(seconds, 0.0, mDuration);
   mPlayheadFloat = (float)(mDuration > 0 ? mPlayhead / mDuration : 0);
   if (mPlaying)
   {
      mPlayStartTime = gTime * 0.001 - mPlayhead / mSpeed;
      mSeekTarget = mPlayhead;
      mSeekPending.store(true, std::memory_order_release);
   }
   mLastFrameTimecode = -1;
}

void VideoPlayerModule::ComputeWaveform()
{
   mWaveformOverview.clear();
   if (!mClip || !mClip->hasAudio())
      return;

   int numPixels = (int)(mWidth - kMargin * 2);
   if (numPixels <= 0)
      return;

   auto reader = mVideoEngine.createReaderFor(juce::File(mVideoPath), foleys::StreamTypes::audio());
   if (!reader || !reader->isOpenedOk() || !reader->hasAudio())
      return;

   reader->setOutputSampleRate(gSampleRate);
   double totalSamples = (double)reader->getTotalLength();
   if (totalSamples <= 0)
      return;

   mWaveformOverview.assign(numPixels, 0.0f);

   int numChannels = reader->getAudioSettings(0).numChannels;
   if (numChannels <= 0)
      return;

   foleys::VideoFifo tempVidFifo(2);
   foleys::AudioFifo tempAudFifo(65536);
   tempAudFifo.setNumChannels(numChannels);
   tempAudFifo.setSampleRate((double)gSampleRate);
   tempAudFifo.setNumSamples(48000);

   // stride-based: read limited samples per pixel to avoid freeze on long videos
   const int maxSamplesPerPixel = 512;
   double stride = totalSamples / numPixels;
   if (stride > maxSamplesPerPixel)
      stride = maxSamplesPerPixel;
   int64_t readLimit = (int64_t)(numPixels * stride);
   if (readLimit > (int64_t)totalSamples)
      readLimit = (int64_t)totalSamples;

   int64_t totalRead = 0;
   const int chunkSize = 4096;

   while (totalRead < readLimit)
   {
      reader->readNewData(tempVidFifo, tempAudFifo);

      while (tempAudFifo.getAvailableSamples() >= chunkSize)
      {
         juce::AudioBuffer<float> buf(numChannels, chunkSize);
         buf.clear();
         juce::AudioSourceChannelInfo info(buf);
         info.startSample = 0;
         info.numSamples = chunkSize;
         tempAudFifo.pullSamples(info);

         int samplesRead = chunkSize;
         if (totalRead + samplesRead > readLimit)
            samplesRead = (int)(readLimit - totalRead);

         for (int i = 0; i < samplesRead; ++i)
         {
            float sample = 0;
            for (int ch = 0; ch < numChannels; ++ch)
               sample += buf.getSample(ch, i);
            sample /= (float)numChannels;

            int px = (int)((double)(totalRead + i) * numPixels / readLimit);
            if (px >= 0 && px < numPixels)
            {
               float absVal = std::abs(sample);
               if (absVal > mWaveformOverview[px])
                  mWaveformOverview[px] = absVal;
            }
         }
         totalRead += samplesRead;

         if (totalRead >= readLimit)
            break;
      }
   }
}

double VideoPlayerModule::SecondsPerPixel() const
{
   return mScrollZoomSeconds / (mWidth - kMargin * 2);
}

double VideoPlayerModule::PixelToSeconds(float x) const
{
   return mTimelineViewStart + (x - kMargin) * SecondsPerPixel();
}

float VideoPlayerModule::SecondsToPixel(double seconds) const
{
   return kMargin + (float)((seconds - mTimelineViewStart) / SecondsPerPixel());
}

void VideoPlayerModule::OnClicked(float x, float y, bool right)
{
   if (!mHasVideo || right)
   {
      IDrawableModule::OnClicked(x, y, right);
      return;
   }

   double now = gTime * 0.001;
   bool dc = (now - mLastClickTime < 0.3) && std::abs(x - mLastClickX) < 5 && std::abs(y - mLastClickY) < 5;
   mLastClickTime = now;
   mLastClickX = x;
   mLastClickY = y;

   if (y >= kTimelineY && y < kTimelineY + kTimelineH)
   {
      if (dc)
      {
         mHasCue = true;
         mCuePoint = PixelToSeconds(x);
      }
      else
      {
         mScrubbing = true;
         SetPosition(PixelToSeconds(x));
      }
   }
   else
   {
      IDrawableModule::OnClicked(x, y, right);
   }
}

bool VideoPlayerModule::MouseMoved(float x, float y)
{
   IDrawableModule::MouseMoved(x, y);

   if (!mHasVideo)
      return false;

   if (mScrubbing && y >= kTimelineY && y < kTimelineY + kTimelineH)
   {
      SetPosition(PixelToSeconds(x));
      return true;
   }
   mLastMouseX = x;
   mLastMouseY = y;
   return false;
}

bool VideoPlayerModule::MouseScrolled(float x, float y, float scrollX, float scrollY, bool isSmoothScroll, bool isInvertedScroll)
{
   if (!mHasVideo)
      return false;

   if (y >= kTimelineY && y < kTimelineY + kTimelineH)
   {
      float zoomDelta = scrollY != 0 ? scrollY : scrollX;
      if (zoomDelta != 0)
      {
         mScrollZoomSeconds *= (1.0 + zoomDelta * 0.1);
         mScrollZoomSeconds = ofClamp(mScrollZoomSeconds, 0.3, mDuration + 1.0);
      }
      return true;
   }
   return IDrawableModule::MouseScrolled(x, y, scrollX, scrollY, isSmoothScroll, isInvertedScroll);
}

void VideoPlayerModule::MouseReleased()
{
   mScrubbing = false;
   IDrawableModule::MouseReleased();
}

void VideoPlayerModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mVideoPath;
   out << mPlayhead;
   out << mSpeed;
   out << mLoop;
   out << mSpeedRange;
   out << mCueMode;
   out << mHasCue;
   if (mHasCue)
      out << mCuePoint;
   out << mLoopIn;
   out << mLoopOut;
   out << mLoopSectionActive;
   for (int i = 0; i < kNumHotcues; ++i)
      out << mHotcuePosition[i];
}

void VideoPlayerModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mVideoPath;
   in >> mPlayhead;
   in >> mSpeed;
   in >> mLoop;
   if (rev >= 1)
   {
      in >> mSpeedRange;
      in >> mCueMode;
   }
   in >> mHasCue;
   if (mHasCue)
      in >> mCuePoint;
   in >> mLoopIn;
   in >> mLoopOut;
   in >> mLoopSectionActive;

   if (rev >= 2)
   {
      for (int i = 0; i < kNumHotcues; ++i)
         in >> mHotcuePosition[i];
   }

   if (rev >= 1 && mSpeedSlider)
   {
      mSpeedSlider->SetExtents(0.05f, mSpeedRangeValues[mSpeedRange]);
      mSpeed = ofClamp(mSpeed, 0.05f, mSpeedRangeValues[mSpeedRange]);
   }

   if (!mVideoPath.empty())
   {
      double savedPlayhead = mPlayhead;
      LoadFromPath(mVideoPath);
      SetPosition(savedPlayhead);
   }
}

#pragma warning(pop)

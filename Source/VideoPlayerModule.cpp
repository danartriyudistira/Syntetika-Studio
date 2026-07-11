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

   x += 4;
   mConvertButton = new ClickButton(this, "convert", x, kControlY);
   AddUIControl(mConvertButton);

   // Row 2: nudge + cue group + trim/loop
   x = 3;
   mNudgeLeftButton = new ClickButton(this, "<", x, kRow2Y); x += 22;
   AddUIControl(mNudgeLeftButton);

   mNudgeRightButton = new ClickButton(this, ">", x, kRow2Y); x += 22;
   AddUIControl(mNudgeRightButton);

   mCueButton = new ClickButton(this, "cue", x, kRow2Y); x += 38;
   AddUIControl(mCueButton);

   mCueModeButton = new ClickButton(this, "jump", x, kRow2Y); x += 36;
   AddUIControl(mCueModeButton);

   mCueClearButton = new ClickButton(this, "X", x, kRow2Y); x += 20;
   AddUIControl(mCueClearButton);

   x += 2;
   mLoopCheckbox = new Checkbox(this, "loop", x, kRow2Y, &mLoop); x += 46;
   AddUIControl(mLoopCheckbox);

   mTrimInButton = new ClickButton(this, "trim A", x, kRow2Y); x += 46;
   AddUIControl(mTrimInButton);

   mTrimOutButton = new ClickButton(this, "trim B", x, kRow2Y); x += 46;
   AddUIControl(mTrimOutButton);

   mTrimClearButton = new ClickButton(this, "clr", x, kRow2Y); x += 32;
   AddUIControl(mTrimClearButton);

   // Row 3: hotcues
   float hx = 3;
   for (int i = 0; i < kNumHotcues; ++i)
   {
      mHotcuePosition[i] = -1;
      mHotcueButton[i] = new ClickButton(this, ofToString(i + 1).c_str(), hx, kRow3Y);
      hx += 26;
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
}

void VideoPlayerModule::Process(double time)
{
   PROFILER(VideoPlayerModule);

   if (mSeekPending.exchange(false, std::memory_order_acquire))
   {
      if (mClip)
      {
         mClip->setNextReadPosition((int64_t)(mSeekTarget * mClip->getSampleRate()));
      }
   }

   IAudioReceiver* target = GetTarget();
   if (!mEnabled || target == nullptr)
      return;

   ComputeSliders(0);

   int bufferSize = target->GetBuffer()->BufferSize();
   mWriteBuffer.Clear();

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
      if (mCueMode == kCueMode_Set || mHotcuePosition[slot] < 0)
      {
         mHotcuePosition[slot] = mPlayhead;
      }
      else
      {
         if (!mPlaying)
            mPlaying = true;
         SetPosition(mHotcuePosition[slot]);
      }
   }
}

void VideoPlayerModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mOpenButton)
      LoadFile();
    else if (button == mConvertButton)
    {
       if (mHasVideo && !mVideoPath.empty())
       {
          std::string outPath = mVideoPath;
          size_t dot = outPath.rfind('.');
          if (dot != std::string::npos) outPath.insert(dot, "_opt");
          else outPath += "_opt.mp4";

          juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
          juce::File converter = exeDir.getChildFile("syntetikaconvert.exe");
          if (!converter.existsAsFile())
          {
             mStatusMsg = "syntetikaconvert.exe missing";
             ofLog() << "VideoPlayer convert: syntetikaconvert.exe not found";
             return;
          }

          mStatusMsg = "Converting...";
          juce::String cmd = converter.getFullPathName() + " \"" + mVideoPath + "\" \"" + outPath + "\"";
          juce::ChildProcess proc;
          if (proc.start(cmd))
          {
             proc.waitForProcessToFinish(60000);
             int exitCode = proc.getExitCode();
             if (exitCode == 0 && juce::File(outPath).existsAsFile())
             {
                mStatusMsg = "Done!";
                LoadFromPath(outPath);
             }
             else
             {
                mStatusMsg = "Convert failed (exit " + juce::String(exitCode).toStdString() + ")";
                ofLog() << "VideoPlayer convert failed (exit " << exitCode << ")";
             }
          }
          else
          {
             mStatusMsg = "Convert failed to start";
             ofLog() << "VideoPlayer convert: failed to start process";
          }
       }
    }
   else if (button == mPlayButton)
   {
      if (mHasVideo)
      {
         if (mTrimActive)
            SetPosition(mTrimIn);
         else if (mPlayhead >= mDuration)
            SetPosition(0);
          mPlaying = true;
          mPlayStartTime = gTime * 0.001 - mPlayhead;
          mSeekTarget = mPlayhead;
          mSeekPending.store(true, std::memory_order_release);
         mLastFrameTimecode = -1;
      }
   }
   else if (button == mPauseButton)
      mPlaying = false;
   else if (button == mStopButton)
   {
      mPlaying = false;
      SetPosition(mTrimActive ? mTrimIn : 0);
   }
   else if (button == mCueButton)
   {
      if (mHasCue)
      {
         SetPosition(mCuePoint);
      }
      else
      {
         mHasCue = true;
         mCuePoint = mPlayhead;
      }
   }
   else if (button == mCueModeButton)
   {
      mCueMode = (mCueMode == kCueMode_Jump) ? kCueMode_Set : kCueMode_Jump;
      mCueModeButton->SetLabel(mCueMode == kCueMode_Jump ? "jump" : "set");
   }
   else if (button == mCueClearButton)
   {
      mHasCue = false;
      mCuePoint = 0;
   }
   else if (button == mNudgeLeftButton)
   {
      double nudgeSec = mDuration > 0 ? 0.1 : 0.05;
      SetPosition(mPlayhead - nudgeSec);
   }
   else if (button == mNudgeRightButton)
   {
      double nudgeSec = mDuration > 0 ? 0.1 : 0.05;
      SetPosition(mPlayhead + nudgeSec);
   }
   else if (button == mTrimInButton)
   {
      if (mTrimOut >= 0 && mPlayhead > mTrimOut)
         mTrimIn = mTrimOut;
      else
         mTrimIn = mPlayhead;
      if (mTrimIn >= 0 && mTrimOut >= 0 && mTrimOut > mTrimIn)
         mTrimActive = true;
      else if (mTrimOut >= 0 && mPlayhead > mTrimOut)
      {
         mTrimOut = mPlayhead;
         mTrimActive = true;
      }
   }
   else if (button == mTrimOutButton)
   {
      if (mTrimIn >= 0 && mPlayhead < mTrimIn)
         mTrimOut = mTrimIn;
      else
         mTrimOut = mPlayhead;
      if (mTrimIn >= 0 && mTrimOut >= 0 && mTrimOut > mTrimIn)
         mTrimActive = true;
      else if (mTrimIn >= 0 && mPlayhead < mTrimIn)
      {
         mTrimIn = mPlayhead;
         mTrimActive = true;
      }
   }
   else if (button == mTrimClearButton)
   {
      mTrimIn = -1;
      mTrimOut = -1;
      mTrimActive = false;
   }
   else
   {
      for (int i = 0; i < kNumHotcues; ++i)
      {
         if (button == mHotcueButton[i])
         {
            if (!mHasVideo)
               return;
            if (mCueMode == kCueMode_Set)
            {
               mHotcuePosition[i] = mPlayhead;
            }
            else if (mHotcuePosition[i] >= 0)
            {
               if (!mPlaying)
                  mPlaying = true;
               SetPosition(mHotcuePosition[i]);
            }
            else
            {
               mHotcuePosition[i] = mPlayhead;
            }
            return;
         }
      }
   }
}

void VideoPlayerModule::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (slider == mPositionSlider && !mScrubbing && !mPlaying)
   {
      SetPosition(mPlayheadFloat * mDuration);
   }
}

void VideoPlayerModule::CheckboxUpdated(Checkbox* checkbox, double time)
{
}

void VideoPlayerModule::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

void VideoPlayerModule::Resize(float width, float height)
{
   mWidth = ofClamp(width, 380, 9999);
   mHeight = ofClamp(height, 180, 9999);

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

   ofPushStyle();
   ofSetColor(25, 25, 35);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   mOpenButton->Draw();
   mPlayButton->Draw();
   mPauseButton->Draw();
   mStopButton->Draw();
   mConvertButton->Draw();
   mNudgeLeftButton->Draw();
   mNudgeRightButton->Draw();
   mCueButton->Draw();
   mCueModeButton->Draw();
   mCueClearButton->Draw();
   mLoopCheckbox->Draw();
   mTrimInButton->Draw();
   mTrimOutButton->Draw();
   mTrimClearButton->Draw();
   mPositionSlider->Draw();
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

   // info bar
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
   if (mTrimActive)
      info += "  [TRIM " + std::string(TimeStr(mTrimIn)) + "-" + std::string(TimeStr(mTrimOut)) + "]";
   if (mHasCue)
      info += "  [CUE " + std::string(TimeStr(mCuePoint)) + "]";
    if (mClip && mClip->hasAudio())
       info += "  [AUDIO]";
    if (!mStatusMsg.empty())
       info += "  " + mStatusMsg;
    DrawTextNormal(info, kMargin + 3, kInfoY + 4, 8);
   ofPopStyle();

   // waveform timeline
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

   // trim markers
   if (mTrimIn >= 0)
   {
      ofSetColor(0, 200, 100);
      ofLine(SecondsToPixel(mTrimIn), tY, SecondsToPixel(mTrimIn), tY + tH);
   }
   if (mTrimOut >= 0)
   {
      ofSetColor(200, 50, 50);
      ofLine(SecondsToPixel(mTrimOut), tY, SecondsToPixel(mTrimOut), tY + tH);
   }
   if (mTrimActive && mTrimIn >= 0 && mTrimOut >= 0)
   {
      float lx = SecondsToPixel(mTrimIn);
      float rx = SecondsToPixel(mTrimOut);
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

   // video preview below timeline
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

   if (!mStatusMsg.empty())
   {
      if (mStatusTime == 0) mStatusTime = t;
      else if (t - mStatusTime > 3.0)
         mStatusMsg.clear();
   }
   else
   {
      mStatusTime = 0;
   }

   if (mPlaying)
   {
      double elapsed = gTime * 0.001 - mPlayStartTime;
      mPlayhead = elapsed;

      if (mTrimActive && mTrimIn >= 0 && mTrimOut >= 0 && mTrimOut > mTrimIn)
      {
         if (mPlayhead >= mTrimOut)
         {
            if (mLoop)
            {
               mPlayhead = mTrimIn;
               mPlayStartTime = gTime * 0.001 - mPlayhead;
               mSeekTarget = mPlayhead;
               mSeekPending.store(true, std::memory_order_release);
               mLastFrameTimecode = -1;
            }
            else
            {
               mPlayhead = mTrimOut;
               mPlaying = false;
            }
         }
      }
      else if (mLoop && mPlayhead >= mDuration)
      {
         mPlayhead = 0;
         mPlayStartTime = gTime * 0.001 - mPlayhead;
         mSeekTarget = mPlayhead;
         mSeekPending.store(true, std::memory_order_release);
         mLastFrameTimecode = -1;
      }
      else if (!mLoop && mPlayhead >= mDuration)
      {
         mPlayhead = mDuration;
         mPlaying = false;
      }
      if (mPlayhead < 0) mPlayhead = 0;
      mPlayheadFloat = (float)(mDuration > 0 ? mPlayhead / mDuration : 0);
   }

   // sync playhead from slider drag
   if (!mPlaying && mDuration > 0)
   {
      double sliderPosition = (double)mPlayheadFloat * mDuration;
      if (std::abs(sliderPosition - mPlayhead) > 0.0001)
      {
         mPlayhead = ofClamp(sliderPosition, 0.0, mDuration);
         mLastFrameTimecode = -1;
      }
   }

   // compute visible timeline window
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
         if (mLastFrameTimecode < 0)
         {
            foleys::Size sz = mClip->getVideoSize();
            img = mClip->getStillImage(mPlayhead, sz);
            if (img.isValid())
            {
               mLastFrameTimecode = (juce::int64)(mPlayhead * 1000.0);
               newFrame = true;
            }
         }
      }

      if (newFrame)
      {
         int w = img.getWidth();
         int h = img.getHeight();

         if (!mFBO || !mFBO->IsValid() || mFBO->GetWidth() != w || mFBO->GetHeight() != h)
         {
            if (mCurrentFrameHandle >= 0 && mFBO && mFBO->IsValid())
            {
               NVGcontext* oldNVG = mFBO->GetNVGContext();
               if (oldNVG)
                  nvgDeleteImage(oldNVG, mCurrentFrameHandle);
               mCurrentFrameHandle = -1;
            }
            delete mFBO;
            mFBO = new VisualFBO();
            mFBO->Create(std::max(64, w), std::max(64, h));
         }

         juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readOnly);
         const uint8_t* srcData = (const uint8_t*)bmp.data;
         size_t bufSize = (size_t)w * h * 4;
         if (mConvertBuffer.size() < bufSize)
            mConvertBuffer.resize(bufSize);

         for (int y = 0; y < h; ++y)
         {
            const uint8_t* src = srcData + (size_t)y * bmp.lineStride;
            uint8_t* dst = mConvertBuffer.data() + (size_t)y * w * 4;
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
         mCurrentFrameHandle = nvgCreateImageRGBA(gNanoVG, w, h, 0, mConvertBuffer.data());

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
   {
      mVideoPath.clear();
      return;
   }

   mClip = std::dynamic_pointer_cast<foleys::MovieClip>(clip);
   if (!mClip)
   {
      mVideoPath.clear();
      return;
   }

   foleys::Size size = mClip->getVideoSize();
   mVideoW = size.width;
   mVideoH = size.height;
   mDuration = mClip->getLengthInSeconds();
   if (mDuration <= 0) mDuration = 1.0;
   mFps = mClip->getFrameDurationInSeconds() > 0
      ? 1.0 / mClip->getFrameDurationInSeconds() : 30.0;

   double sr = gSampleRate > 0 ? gSampleRate : 44100.0;
   mClip->prepareToPlay(gBufferSize, sr);
   if (mClip->hasAudio())
   {
      mClip->setNextReadPosition(0);
      ComputeWaveform();
   }

   mHasVideo = true;
   mPlaying = false;
   mStatusMsg.clear();
   mStatusTime = 0;
   SetPosition(0);
   mTrimIn = -1;
   mTrimOut = -1;
   mTrimActive = false;
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
   delete mFBO;
   mFBO = nullptr;
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
   mSeekPending.store(false, std::memory_order_relaxed);
   for (int i = 0; i < kNumHotcues; ++i)
      mHotcuePosition[i] = -1;
}

void VideoPlayerModule::SetPosition(double seconds)
{
   mPlayhead = ofClamp(seconds, 0.0, mDuration);
   mPlayheadFloat = (float)(mDuration > 0 ? mPlayhead / mDuration : 0);
   if (mPlaying)
   {
      mPlayStartTime = gTime * 0.001 - mPlayhead;
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

   double sr = gSampleRate > 0 ? gSampleRate : 44100.0;
   reader->setOutputSampleRate(sr);
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
   tempAudFifo.setSampleRate((double)sr);
   tempAudFifo.setNumSamples(48000);

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
      if (mHasCue && std::abs(x - SecondsToPixel(mCuePoint)) < 8)
      {
         mCueDragging = true;
         return;
      }

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

   if (mCueDragging)
   {
      mCuePoint = ofClamp(PixelToSeconds(x), 0.0, mDuration);
      return true;
   }

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
   mCueDragging = false;
   IDrawableModule::MouseReleased();
}

void VideoPlayerModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mVideoPath;
   out << mPlayhead;
   out << mLoop;
   out << mHasCue;
   if (mHasCue)
      out << mCuePoint;
   out << mTrimIn;
   out << mTrimOut;
   out << mTrimActive;
   for (int i = 0; i < kNumHotcues; ++i)
      out << mHotcuePosition[i];
   out << mCueMode;
}

void VideoPlayerModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mVideoPath;
   in >> mPlayhead;

   if (rev < 5)
   {
      if (rev <= 3)
      {
         float dummySpeed; in >> dummySpeed;
         in >> mLoop;
         int dummyRange = 0, dummyCue = 0;
         if (rev >= 1) { in >> dummyRange; in >> dummyCue; }
         float dummyTempo = 0, dummyBase = 0; bool dummySync = false;
         if (rev >= 3) { in >> dummyTempo; in >> dummyBase; in >> dummySync; }
      }
      else // rev == 4
      {
         in >> mLoop;
         bool dummySync; in >> dummySync;
      }

      in >> mHasCue;
      if (mHasCue) in >> mCuePoint;

      if (rev >= 2)
      {
         double oldLoopIn; in >> oldLoopIn; mTrimIn = oldLoopIn;
         double oldLoopOut; in >> oldLoopOut; mTrimOut = oldLoopOut;
         bool oldActive; in >> oldActive; mTrimActive = oldActive;
         for (int i = 0; i < kNumHotcues; ++i)
            in >> mHotcuePosition[i];
      }
      else
      {
         double dummyTrimIn, dummyTrimOut; bool dummyTrimActive;
         in >> dummyTrimIn; in >> dummyTrimOut; in >> dummyTrimActive;
      }

      if (rev >= 4)
      {
         float dummyBPM; in >> dummyBPM;
         in >> mCueMode;
      }
      else
      {
         mCueMode = kCueMode_Jump;
      }
   }
   else
   {
      in >> mLoop;
      in >> mHasCue;
      if (mHasCue) in >> mCuePoint;
      in >> mTrimIn;
      in >> mTrimOut;
      in >> mTrimActive;
      for (int i = 0; i < kNumHotcues; ++i)
         in >> mHotcuePosition[i];
      in >> mCueMode;
   }

   if (!mVideoPath.empty())
   {
      double savedPlayhead = mPlayhead;
      LoadFromPath(mVideoPath);
      SetPosition(savedPlayhead);
   }
}

#pragma warning(pop)

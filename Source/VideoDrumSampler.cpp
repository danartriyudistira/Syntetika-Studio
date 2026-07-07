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
#include <sstream>

VideoDrumSampler::VideoDrumSampler()
: mNoteInputBuffer(this)
{
   mWriteBuffer.SetNumActiveChannels(2);
}

VideoDrumSampler::~VideoDrumSampler()
{
   delete mFBO;
   for (auto& pad : mPads)
   {
      if (pad.mLoaded && mFBO)
      {
         NVGcontext* nvg = mFBO->GetNVGContext();
         for (auto handle : pad.mImageHandles)
         {
            if (handle >= 0)
               nvgDeleteImage(nvg, handle);
         }
      }
      if (!pad.mTempDir.empty())
         RemoveTempDir(pad.mTempDir);
   }
}

void VideoDrumSampler::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mBrowseButton = new ClickButton(this, "browse", 3, 3);
   AddUIControl(mBrowseButton);

   mModeDropdown = new DropdownList(this, "mode", 60, 3, (int*)&mMode);
   mModeDropdown->AddLabel("FPS", kMode_FPS);
   mModeDropdown->AddLabel("FPB", kMode_FPB);
   AddUIControl(mModeDropdown);

   mFpsSlider = new FloatSlider(this, "fps", 120, 3, 60, 15, &mGlobalFps, 1, 120, 0);
   AddUIControl(mFpsSlider);

   mFramesPerBeatSlider = new FloatSlider(this, "fpb", 190, 3, 60, 15, &mFramesPerBeat, 0.25f, 32, 2);
   AddUIControl(mFramesPerBeatSlider);

   mEditCheckbox = new Checkbox(this, "edit", 260, 3, &mEditMode);
   AddUIControl(mEditCheckbox);

   float editX = mWidth - kEditPanelWidth + 5;
   mEditVolSlider = new FloatSlider(this, "vol", editX, 25, 90, 15, &mEditVol, 0, 2);
   mEditSpeedSlider = new FloatSlider(this, "speed", editX, 43, 90, 15, &mEditSpeed, 0.1f, 4);
   mEditPanSlider = new FloatSlider(this, "pan", editX, 61, 90, 15, &mEditPan, -1, 1);
   mEditFpsSlider = new FloatSlider(this, "p f p s", editX, 79, 90, 15, &mEditFps, 1, 120, 0);
   mLoadVideoButton = new ClickButton(this, "load video", editX, 97);
   mLoadAudioButton = new ClickButton(this, "load audio", editX, 115);
   mClearPadButton = new ClickButton(this, "clear", editX, 133);
   mEditLoopCheckbox = new Checkbox(this, "loop", editX, 151, &mEditLoop);
   AddUIControl(mEditVolSlider);
   AddUIControl(mEditSpeedSlider);
   AddUIControl(mEditPanSlider);
   AddUIControl(mEditFpsSlider);
   AddUIControl(mLoadVideoButton);
   AddUIControl(mLoadAudioButton);
   AddUIControl(mClearPadButton);
   AddUIControl(mEditLoopCheckbox);

   mOutputCable = new PatchCableSource(this, kConnectionType_Audio);
   mOutputCable->SetManualPosition(mWidth, mHeight / 2);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);

   mVisualCable = new PatchCableSource(this, kConnectionType_Special);
   mVisualCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
   mVisualCable->SetManualPosition(mWidth, mHeight / 2 + 15);
   mVisualCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mVisualCable);
}

void VideoDrumSampler::Process(double time)
{
   PROFILER(VideoDrumSampler);

   IAudioReceiver* target = GetTarget();
   if (!mEnabled || target == nullptr)
      return;

   mNoteInputBuffer.Process(time);

   ComputeSliders(0);

   int bufferSize = target->GetBuffer()->BufferSize();
   mWriteBuffer.Clear();

   for (auto& pad : mPads)
   {
      if (!pad.mActive && pad.mAudioSample.LengthInSamples() == 0)
         continue;

      if (pad.mAudioSample.LengthInSamples() > 0)
      {
         if (pad.mAudioSample.IsPlaying())
         {
            ChannelBuffer tempBuf(bufferSize);
            tempBuf.SetNumActiveChannels(2);
            pad.mAudioSample.ConsumeData(time, &tempBuf, bufferSize, true);

            float volL = pad.mVol * (1.0f - std::max(0.0f, pad.mPan));
            float volR = pad.mVol * (1.0f + std::min(0.0f, pad.mPan));

            for (int ch = 0; ch < 2; ++ch)
            {
               float* src = tempBuf.GetChannel(ch);
               float* dst = mWriteBuffer.GetChannel(ch);
               float vol = (ch == 0) ? volL : volR;
               for (int i = 0; i < bufferSize; ++i)
                  dst[i] += src[i] * vol;
            }
         }

         if (!pad.mAudioSample.IsPlaying())
            pad.mActive = false;
      }
   }

   SyncOutputBuffer(2);
   for (int ch = 0; ch < 2; ++ch)
      Add(target->GetBuffer()->GetChannel(ch), mWriteBuffer.GetChannel(ch), bufferSize);
}

void VideoDrumSampler::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled)
      return;

   pitch %= 24;
   if (pitch >= 0 && pitch < kNumPads)
   {
      if (velocity > 0)
         TriggerPad(pitch, time);
   }
}

void VideoDrumSampler::TriggerPad(int index, double time)
{
   if (index < 0 || index >= kNumPads)
      return;

   auto& pad = mPads[index];
   if (!pad.mLoaded && pad.mAudioSample.LengthInSamples() == 0)
      return;

   int linkId = pad.mLinkId;
   if (linkId != -1)
   {
      for (int i = 0; i < kNumPads; ++i)
      {
         if (i != index && mPads[i].mLinkId == linkId)
            mPads[i].mActive = false;
      }
   }

   pad.mActive = true;
   pad.mLastAdvanceTime = gTime;
   pad.mCurrentFrame = 0;

   if (pad.mAudioSample.LengthInSamples() > 0)
      pad.mAudioSample.Play(time, pad.mSpeed, 0);
}

int VideoDrumSampler::PadFromClick(float x, float y) const
{
   float gridRight = kGridX + 4 * (kPadSize + kPadGap);
   float gridBottom = kGridY + 4 * (kPadSize + kPadGap);
   if (x < kGridX || x >= gridRight || y < kGridY || y >= gridBottom)
      return -1;

   int col = (int)((x - kGridX) / (kPadSize + kPadGap));
   int row = (int)((y - kGridY) / (kPadSize + kPadGap));
   return col + row * 4;
}

void VideoDrumSampler::DrawModule()
{
   if (Minimized() || !IsVisible())
      return;

   ofPushStyle();
   ofSetColor(30, 30, 40);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   mBrowseButton->Draw();
   mModeDropdown->Draw();
   mFpsSlider->Draw();
   mFramesPerBeatSlider->Draw();
   mEditCheckbox->Draw();

   if (mFBO && mFBO->IsValid())
   {
      float gridRight = kGridX + 4 * (kPadSize + kPadGap);
      float gridBottom = kGridY + 4 * (kPadSize + kPadGap);
      float previewX = gridRight + 5;
      float previewW = mWidth - previewX - kEditPanelWidth - 10;
      float previewH = gridBottom - kGridY;

      if (previewW > 10 && previewH > 10)
      {
         ofPushStyle();
         ofSetColor(20, 20, 30);
         ofFill();
         ofRect(previewX, kGridY, previewW, previewH);
         ofPopStyle();
         mFBO->Draw(previewX, kGridY, previewW, previewH);
      }
   }

   for (int i = 0; i < kNumPads; ++i)
   {
      int col = i % 4;
      int row = i / 4;
      float px = kGridX + col * (kPadSize + kPadGap);
      float py = kGridY + row * (kPadSize + kPadGap);

      bool active = mPads[i].mActive;

      ofPushStyle();
      if (mPads[i].mLoaded || mPads[i].mAudioSample.LengthInSamples() > 0)
         ofSetColor(active ? 100 : 60, active ? 180 : 120, active ? 200 : 160);
      else
         ofSetColor(50, 50, 60);
      ofFill();
      ofRect(px, py, kPadSize, kPadSize);
      ofPopStyle();

      ofPushStyle();
      ofSetColor(200, 200, 220);
      DrawTextNormal(juce::String(i + 1).toStdString(), px + 3, py + 12, 10);
      if (mPads[i].mLoaded)
         DrawTextNormal("V", px + kPadSize - 14, py + 12, 10);
      if (mPads[i].mAudioSample.LengthInSamples() > 0)
         DrawTextNormal("A", px + kPadSize - 14, py + 24, 10);
      ofPopStyle();

      ofPushStyle();
      ofSetColor(60, 60, 70);
      ofNoFill();
      ofRect(px, py, kPadSize, kPadSize);
      ofPopStyle();

      if (mEditMode && i == mEditIndex)
      {
         ofPushStyle();
         ofSetColor(255, 200, 50);
         ofNoFill();
         ofRect(px - 1, py - 1, kPadSize + 2, kPadSize + 2);
         ofPopStyle();
      }
   }

   if (mEditMode)
   {
      float editX = mWidth - kEditPanelWidth + 3;
      ofPushStyle();
      ofSetColor(40, 40, 50);
      ofFill();
      ofRect(editX - 3, 20, kEditPanelWidth, 175);
      ofPopStyle();

      ofPushStyle();
      ofSetColor(200, 200, 220);
      DrawTextNormal("Pad " + juce::String(mEditIndex + 1).toStdString(), editX, 15, 12);
      ofPopStyle();

      mEditVolSlider->Draw();
      mEditSpeedSlider->Draw();
      mEditPanSlider->Draw();
      mEditFpsSlider->Draw();
      mLoadVideoButton->Draw();
      mLoadAudioButton->Draw();
      mClearPadButton->Draw();
      mEditLoopCheckbox->Draw();
   }
}

void VideoDrumSampler::PostRender()
{
   if (!mEnabled)
      return;

   if (!mFBO)
      mFBO = new VisualFBO();
   if (!mFBO->IsValid())
      mFBO->Create(512, 512);

   bool anyActive = false;
   for (auto& pad : mPads)
   {
      if (!pad.mActive)
         continue;

      anyActive = true;
      double elapsed = gTime - pad.mLastAdvanceTime;
      double msPerFrame = (mMode == kMode_FPB)
         ? (TheTransport->GetDuration(kInterval_4n) / mFramesPerBeat)
         : (1000.0 / (pad.mFps * pad.mSpeed));

      if (msPerFrame > 0)
      {
         int steps = (int)(elapsed / msPerFrame);
         if (steps > 0)
         {
            pad.mLastAdvanceTime += steps * msPerFrame;
            pad.mCurrentFrame += steps;

            if (pad.mCurrentFrame >= pad.mNumFrames)
            {
               if (pad.mLooping)
                  pad.mCurrentFrame %= pad.mNumFrames;
               else
               {
                  pad.mActive = false;
                  continue;
               }
            }
         }
      }
   }

   if (anyActive)
   {
      mFBO->Bind();

      for (auto& pad : mPads)
      {
         if (!pad.mActive || !pad.mLoaded || pad.mImageHandles.empty())
            continue;
         if (pad.mCurrentFrame < 0 || pad.mCurrentFrame >= (int)pad.mImageHandles.size())
            continue;

         int handle = pad.mImageHandles[pad.mCurrentFrame];
         if (handle < 0)
            continue;

         NVGpaint imgPaint = nvgImagePattern(gNanoVG, 0, 0, pad.mImageW, pad.mImageH, 0.0f, handle, pad.mVol);
         nvgBeginPath(gNanoVG);
         nvgRect(gNanoVG, 0, 0, (float)pad.mImageW, (float)pad.mImageH);
         nvgFillPaint(gNanoVG, imgPaint);
         nvgFill(gNanoVG);
      }

      mFBO->Unbind();
   }
}

VisualFBO* VideoDrumSampler::GetFBO()
{
   return mFBO;
}

void VideoDrumSampler::ButtonClicked(ClickButton* button, double time)
{
   if (button == mBrowseButton)
   {
      juce::FileChooser chooser("Select MP4 Video File", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mp4;*.mov;*.avi;*.mkv", true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen())
      {
         juce::File videoFile = chooser.getResult();
         for (int i = 0; i < kNumPads; ++i)
         {
            auto& pad = mPads[i];
            if (!pad.mLoaded && pad.mAudioSample.LengthInSamples() == 0)
            {
               pad.mVideoPath = videoFile.getFullPathName().toStdString();
               LoadPadVideo(i);
               break;
            }
         }
      }
   }
   else if (button == mLoadVideoButton && mEditIndex >= 0 && mEditIndex < kNumPads)
   {
      juce::FileChooser chooser("Select MP4 Video for Pad " + juce::String(mEditIndex + 1), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mp4;*.mov;*.avi;*.mkv", true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen())
      {
         auto& pad = mPads[mEditIndex];
         pad.mVideoPath = chooser.getResult().getFullPathName().toStdString();
         LoadPadVideo(mEditIndex);
      }
   }
   else if (button == mLoadAudioButton && mEditIndex >= 0 && mEditIndex < kNumPads)
   {
      juce::FileChooser chooser("Select Audio Sample for Pad " + juce::String(mEditIndex + 1), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.wav;*.mp3;*.aiff;*.flac;*.ogg", true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen())
      {
         auto& pad = mPads[mEditIndex];
         pad.mAudioPath = chooser.getResult().getFullPathName().toStdString();
         pad.mAudioSample.Read(pad.mAudioPath.c_str());
      }
   }
   else if (button == mClearPadButton && mEditIndex >= 0 && mEditIndex < kNumPads)
   {
      ClearPad(mEditIndex);
   }
}

void VideoDrumSampler::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (mEditIndex < 0 || mEditIndex >= kNumPads)
      return;

   if (slider == mEditVolSlider)      mPads[mEditIndex].mVol = mEditVol;
   else if (slider == mEditSpeedSlider) mPads[mEditIndex].mSpeed = mEditSpeed;
   else if (slider == mEditPanSlider)  mPads[mEditIndex].mPan = mEditPan;
   else if (slider == mEditFpsSlider)  mPads[mEditIndex].mFps = mEditFps;
}

void VideoDrumSampler::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (checkbox == mEditCheckbox)
   {
      if (mEditMode && mEditIndex < 0)
      {
         mEditIndex = 0;
         SyncEditVars();
      }
   }
   else if (checkbox == mEditLoopCheckbox && mEditIndex >= 0 && mEditIndex < kNumPads)
   {
      mPads[mEditIndex].mLooping = mEditLoop;
   }
}

void VideoDrumSampler::OnClicked(float x, float y, bool right)
{
   if (right)
      return;

   if (mEditMode)
   {
      int idx = PadFromClick(x, y);
      if (idx >= 0)
      {
         mEditIndex = idx;
         SyncEditVars();
      }
   }
}

void VideoDrumSampler::SyncEditVars()
{
   if (mEditIndex < 0 || mEditIndex >= kNumPads)
      return;

   auto& pad = mPads[mEditIndex];
   mEditVol = pad.mVol;
   mEditSpeed = pad.mSpeed;
   mEditPan = pad.mPan;
   mEditFps = pad.mFps;
   mEditLoop = pad.mLooping;
}

void VideoDrumSampler::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
}

void VideoDrumSampler::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

#pragma warning(push)
#pragma warning(disable : 4996) //_popen

std::string VideoDrumSampler::FindFFmpeg()
{
   juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
   juce::File localPath = exeDir.getChildFile("ffmpeg.exe");
   if (localPath.existsAsFile())
      return localPath.getFullPathName().toStdString();

   const char* pathEnv = std::getenv("PATH");
   if (pathEnv)
   {
      std::string paths(pathEnv);
      size_t start = 0;
      size_t end;
      while ((end = paths.find(';', start)) != std::string::npos)
      {
         std::string dir = paths.substr(start, end - start);
         juce::File f(dir + "\\ffmpeg.exe");
         if (f.existsAsFile())
            return f.getFullPathName().toStdString();
         start = end + 1;
      }
      juce::File f(paths.substr(start) + "\\ffmpeg.exe");
      if (f.existsAsFile())
         return f.getFullPathName().toStdString();
   }
   return "ffmpeg.exe";
}

std::string VideoDrumSampler::CreateTempDir()
{
   juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
   juce::File dir = tmp.getNonexistentChildFile("vds_", "", 0);
   dir.createDirectory();
   return dir.getFullPathName().toStdString();
}

bool VideoDrumSampler::ExtractFramesFFmpeg(const std::string& videoPath, const std::string& tempDir)
{
   std::string ffmpeg = FindFFmpeg();
   std::string cmd = "\"" + ffmpeg + "\" -i \"" + videoPath + "\" -q:v 1 \"" + tempDir + "\\frame_%04d.png\" 2>nul";

   FILE* pipe = _popen(cmd.c_str(), "r");
   if (!pipe)
      return false;
   _pclose(pipe);

   juce::File dir(tempDir);
   juce::Array<juce::File> files;
   dir.findChildFiles(files, juce::File::findFiles, false, "*.png");
   return !files.isEmpty();
}

void VideoDrumSampler::RemoveTempDir(const std::string& tempDir)
{
   juce::File dir(tempDir);
   if (dir.isDirectory())
      dir.deleteRecursively();
}

#pragma warning(pop)

void VideoDrumSampler::LoadPadVideo(int index)
{
   if (index < 0 || index >= kNumPads)
      return;

   auto& pad = mPads[index];
   ClearPad(index);

   if (pad.mVideoPath.empty())
      return;

   juce::File videoFile(pad.mVideoPath);
   if (!videoFile.existsAsFile())
      return;

   std::string tempDir = CreateTempDir();
   if (tempDir.empty())
      return;

   if (!ExtractFramesFFmpeg(pad.mVideoPath, tempDir))
   {
      RemoveTempDir(tempDir);
      return;
   }

   pad.mTempDir = tempDir;

   juce::Array<juce::File> results;
   juce::File dir(tempDir);
   dir.findChildFiles(results, juce::File::findFiles, false, "*.png");
   results.sort();

   if (results.isEmpty())
   {
      RemoveTempDir(tempDir);
      pad.mTempDir.clear();
      return;
   }

   if (!mFBO)
      mFBO = new VisualFBO();
   NVGcontext* nvg = mFBO->GetNVGContext();

   for (auto& f : results)
   {
      auto juceImage = juce::ImageFileFormat::loadFrom(f);
      if (!juceImage.isValid())
         continue;

      int w = juceImage.getWidth();
      int h = juceImage.getHeight();
      pad.mImageW = w;
      pad.mImageH = h;

      juce::Image::BitmapData bmp(juceImage, juce::Image::BitmapData::readOnly);
      std::vector<unsigned char> buf(w * h * 4);
      for (int y = 0; y < h; ++y)
         for (int x = 0; x < w; ++x)
         {
            int si = y * w + x;
            auto c = bmp.getPixelColour(x, y);
            buf[si * 4 + 0] = c.getRed();
            buf[si * 4 + 1] = c.getGreen();
            buf[si * 4 + 2] = c.getBlue();
            buf[si * 4 + 3] = c.getAlpha();
         }

      int handle = nvgCreateImageRGBA(nvg, w, h, 0, buf.data());
      if (handle >= 0)
      {
         pad.mImageHandles.push_back(handle);
         pad.mNumFrames = (int)pad.mImageHandles.size();
      }
   }

   pad.mLoaded = !pad.mImageHandles.empty();
}

void VideoDrumSampler::ClearPad(int index)
{
   if (index < 0 || index >= kNumPads)
      return;

   auto& pad = mPads[index];
   if (pad.mLoaded && mFBO)
   {
      NVGcontext* nvg = mFBO->GetNVGContext();
      for (auto handle : pad.mImageHandles)
      {
         if (handle >= 0)
            nvgDeleteImage(nvg, handle);
      }
   }
   pad.mImageHandles.clear();
   pad.mNumFrames = 0;
   pad.mLoaded = false;
   pad.mImageW = 0;
   pad.mImageH = 0;
   pad.mActive = false;
   pad.mCurrentFrame = 0;

   if (!pad.mTempDir.empty())
   {
      RemoveTempDir(pad.mTempDir);
      pad.mTempDir.clear();
   }
}

void VideoDrumSampler::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mMode;
   out << mGlobalFps;
   out << mFramesPerBeat;
   for (int i = 0; i < kNumPads; ++i)
   {
      out << mPads[i].mVideoPath;
      out << mPads[i].mAudioPath;
      out << mPads[i].mVol;
      out << mPads[i].mSpeed;
      out << mPads[i].mPan;
      out << mPads[i].mFps;
      out << mPads[i].mLooping;
      out << mPads[i].mLinkId;
   }
}

void VideoDrumSampler::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mMode;
   in >> mGlobalFps;
   in >> mFramesPerBeat;
   if (mFpsSlider) mFpsSlider->SetValue(mGlobalFps, gTime);
   if (mFramesPerBeatSlider) mFramesPerBeatSlider->SetValue(mFramesPerBeat, gTime);
   for (int i = 0; i < kNumPads; ++i)
   {
      std::string videoPath, audioPath;
      in >> videoPath;
      in >> audioPath;
      in >> mPads[i].mVol;
      in >> mPads[i].mSpeed;
      in >> mPads[i].mPan;
      in >> mPads[i].mFps;
      in >> mPads[i].mLooping;
      in >> mPads[i].mLinkId;

      mPads[i].mVideoPath = videoPath;
      mPads[i].mAudioPath = audioPath;

      if (!videoPath.empty())
         LoadPadVideo(i);
      if (!audioPath.empty())
         mPads[i].mAudioSample.Read(audioPath.c_str());
   }
}

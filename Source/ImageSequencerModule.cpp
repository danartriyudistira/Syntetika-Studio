#include "ImageSequencerModule.h"
#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>

ImageSequencerModule::ImageSequencerModule()
{
}

ImageSequencerModule::~ImageSequencerModule()
{
   delete mFBO;
}

void ImageSequencerModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mBrowseButton = new ClickButton(this, "browse folder", 3, 3);
   AddUIControl(mBrowseButton);

   mPlayPauseButton = new ClickButton(this, "play", 85, 3);
   AddUIControl(mPlayPauseButton);

   mPrevButton = new ClickButton(this, "<<", 145, 3);
   AddUIControl(mPrevButton);

   mNextButton = new ClickButton(this, ">>", 175, 3);
   AddUIControl(mNextButton);

   mFpsSlider = new FloatSlider(this, "fps", 3, 20, 90, 15, &mFps, 1, 60);
   AddUIControl(mFpsSlider);

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mWidth, 10);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);
}

void ImageSequencerModule::DrawModule()
{
   if (Minimized() || !mEnabled)
      return;

   ofPushStyle();
   ofSetColor(40, 40, 60);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   mBrowseButton->Draw();
   mPlayPauseButton->Draw();
   mPrevButton->Draw();
   mNextButton->Draw();
   mFpsSlider->Draw();

   float contentTop = 40;
   float contentW = mWidth - 6;
   float contentH = mHeight - contentTop - 18;

   if (mImages.empty())
   {
      ofPushStyle();
      ofSetColor(100, 100, 120);
      DrawTextNormal("No folder selected", 5, mHeight / 2 - 6, 13);
      ofPopStyle();
   }
   else if (mFBO && mFBO->IsValid())
   {
      float previewX = 3;
      float previewY = contentTop;
      float previewW = contentW;
      float previewH = contentH;
      float imgAspect = (float)mImageWidth / (float)mImageHeight;
      float boxAspect = previewW / previewH;
      if (imgAspect > boxAspect)
      {
         previewH = previewW / imgAspect;
         previewY = contentTop + (contentH - previewH) / 2;
      }
      else
      {
         previewW = previewH * imgAspect;
         previewX = 3 + (contentW - previewW) / 2;
      }

      mFBO->Draw(previewX, previewY, previewW, previewH);
   }

   if (!mImages.empty())
   {
      ofPushStyle();
      ofSetColor(180, 180, 200);
      std::string status = juce::String(mCurrentIndex + 1).toStdString() + "/" + juce::String((int)mImages.size()).toStdString() + "  " + (mPlaying ? ">" : "||");
      DrawTextNormal(status, 3, mHeight - 14, 11);
      ofPopStyle();
   }
}

void ImageSequencerModule::PostRender()
{
   if (!mEnabled)
      return;

   if (mPendingScan)
   {
      mPendingScan = false;
      DoScanFolder();
   }

   if (mPendingLoad && mPendingLoadIndex >= 0 && mPendingLoadIndex < (int)mImages.size())
   {
      mPendingLoad = false;
      LoadImageAtIndex(mPendingLoadIndex);
   }

   if (!mPlaying || mImages.empty())
      return;

   if (mFps <= 0)
      return;

   float frameDuration = 1.0f / mFps;
   double elapsed = gTime - mLastFrameTime;

   if (elapsed >= frameDuration)
   {
      mLastFrameTime = gTime;
      AdvanceFrame();
   }
}

void ImageSequencerModule::AdvanceFrame()
{
   if (mImages.empty())
      return;

   mCurrentIndex = (mCurrentIndex + 1) % (int)mImages.size();
   mPendingLoadIndex = mCurrentIndex;
   mPendingLoad = true;
}

void ImageSequencerModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mBrowseButton)
   {
      juce::FileChooser chooser("Select Image Folder", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*", true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForDirectory())
      {
         mFolderPath = chooser.getResult().getFullPathName().toStdString();
         mPendingScan = true;
      }
   }
   else if (button == mPlayPauseButton)
   {
      mPlaying = !mPlaying;
      mPlayPauseButton->SetLabel(mPlaying ? "play" : "pause");
   }
   else if (button == mPrevButton)
   {
      if (!mImages.empty())
      {
         mCurrentIndex = (mCurrentIndex - 1 + (int)mImages.size()) % (int)mImages.size();
         mPendingLoadIndex = mCurrentIndex;
         mPendingLoad = true;
      }
   }
   else if (button == mNextButton)
   {
      if (!mImages.empty())
      {
         mCurrentIndex = (mCurrentIndex + 1) % (int)mImages.size();
         mPendingLoadIndex = mCurrentIndex;
         mPendingLoad = true;
      }
   }
}

void ImageSequencerModule::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
}

void ImageSequencerModule::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

void ImageSequencerModule::Resize(float w, float h)
{
   mWidth = std::max(kMinWidth, w);
   mHeight = std::max(kMinHeight, h);
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, 10);
}

void ImageSequencerModule::ClearImages()
{
   mImages.clear();
   mCurrentIndex = 0;
   mImageWidth = 0;
   mImageHeight = 0;
   if (mImageHandle >= 0 && mFBO)
   {
      NVGcontext* oldNvg = mFBO->GetNVGContext();
      if (oldNvg)
         nvgDeleteImage(oldNvg, mImageHandle);
      mImageHandle = -1;
   }
}

void ImageSequencerModule::DoScanFolder()
{
   ClearImages();

   juce::File folder(mFolderPath);
   if (!folder.isDirectory())
      return;

   juce::Array<juce::File> results;
   folder.findChildFiles(results, juce::File::findFiles, false, "*.png;*.jpg;*.jpeg");
   results.sort();

   for (auto& f : results)
   {
      ImageEntry entry;
      entry.filePath = f.getFullPathName().toStdString();

      auto juceImage = juce::ImageFileFormat::loadFrom(f);
      if (!juceImage.isValid())
         continue;

      entry.width = juceImage.getWidth();
      entry.height = juceImage.getHeight();

      juce::Image::BitmapData bitmapData(juceImage, juce::Image::BitmapData::readOnly);
      entry.decodedData.resize(entry.width * entry.height * 4);
      for (int y = 0; y < entry.height; ++y)
      {
         for (int x = 0; x < entry.width; ++x)
         {
            int si = y * entry.width + x;
            auto c = bitmapData.getPixelColour(x, y);
            entry.decodedData[si * 4 + 0] = c.getRed();
            entry.decodedData[si * 4 + 1] = c.getGreen();
            entry.decodedData[si * 4 + 2] = c.getBlue();
            entry.decodedData[si * 4 + 3] = c.getAlpha();
         }
      }

      mImages.push_back(entry);
   }

   if (!mImages.empty())
   {
      mCurrentIndex = 0;
      mPendingLoadIndex = 0;
      mPendingLoad = true;
      mLastFrameTime = gTime;
   }
}

void ImageSequencerModule::LoadImageAtIndex(int index)
{
   if (index < 0 || index >= (int)mImages.size())
      return;

   auto& entry = mImages[index];
   if (entry.decodedData.empty() || entry.width <= 0 || entry.height <= 0)
      return;

   mImageWidth = entry.width;
   mImageHeight = entry.height;

   UploadRGBAToFBO(entry.decodedData.data(), mImageWidth, mImageHeight);
}

void ImageSequencerModule::UploadRGBAToFBO(unsigned char* data, int w, int h)
{
   if (data == nullptr || w <= 0 || h <= 0)
      return;

   if (mImageHandle >= 0 && mFBO)
   {
      NVGcontext* oldNvg = mFBO->GetNVGContext();
      if (oldNvg)
         nvgDeleteImage(oldNvg, mImageHandle);
      mImageHandle = -1;
   }

   if (!mFBO)
      mFBO = new VisualFBO();
   mFBO->Create(w, h);

   {
      NVGcontext* savedNvg = gNanoVG;
      gNanoVG = mFBO->GetNVGContext();
      mImageHandle = nvgCreateImageRGBA(gNanoVG, w, h, 0, data);
      gNanoVG = savedNvg;
   }

   if (mImageHandle < 0)
      return;

   mFBO->Bind();
   NVGpaint imgPaint = nvgImagePattern(gNanoVG, 0, 0, w, h, 0.0f, mImageHandle, 1.0f);
   nvgBeginPath(gNanoVG);
   nvgRect(gNanoVG, 0, 0, w, h);
   nvgFillPaint(gNanoVG, imgPaint);
   nvgFill(gNanoVG);
   mFBO->Unbind();
}

void ImageSequencerModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mFolderPath;
   out << mWidth;
   out << mHeight;
   out << mPlaying;
   out << mFps;
}

void ImageSequencerModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   std::string path;
   in >> path;
   in >> mWidth;
   in >> mHeight;
   in >> mPlaying;
   in >> mFps;
   if (mFpsSlider)
      mFpsSlider->SetValue(mFps, gTime);
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, 10);
   if (!path.empty())
   {
      mFolderPath = path;
      mPendingScan = true;
   }
}

VisualFBO* ImageSequencerModule::GetFBO()
{
   return mFBO;
}

#include "ImageLoaderModule.h"
#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>

ImageLoaderModule::ImageLoaderModule()
{
}

ImageLoaderModule::~ImageLoaderModule()
{
   delete mFBO; // destroys FBO's nanovg context, which frees all image handles in it
}

void ImageLoaderModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mBrowseButton = new ClickButton(this, "browse", 3, 3);
   AddUIControl(mBrowseButton);

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mWidth, 10);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);
}

void ImageLoaderModule::DrawModule()
{
   if (Minimized() || !mEnabled)
      return;

   ofPushStyle();
   ofSetColor(40, 40, 60);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   mBrowseButton->Draw();

   if (mFBO && mFBO->IsValid())
   {
      float contentTop = 22;
      float contentW = mWidth - 6;
      float contentH = mHeight - contentTop - 3;

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
   else
   {
      ofPushStyle();
      ofSetColor(100, 100, 120);
      DrawTextNormal("No image loaded", 5, mHeight / 2 - 6, 13);
      ofSetColor(80, 80, 100);
      DrawTextNormal("Click Browse to open a file", 5, mHeight / 2 + 10, 11);
      ofPopStyle();
   }
}

void ImageLoaderModule::PostRender()
{
   if (mPendingLoad)
   {
      mPendingLoad = false;
      DoLoadImage();
   }

   UpdateGIFAnimation();
}

void ImageLoaderModule::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

void ImageLoaderModule::Resize(float w, float h)
{
   mWidth = std::max(kMinWidth, w);
   mHeight = std::max(kMinHeight, h);
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, 10);
}

void ImageLoaderModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mBrowseButton)
   {
      auto pattern = "*.png;*.jpg;*.jpeg;*.gif";
      juce::FileChooser chooser("Load Image", juce::File(), pattern, true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen())
      {
         juce::File file = chooser.getResult();
         mPendingPath = file.getFullPathName().toStdString();
         mPendingLoad = true;
      }
   }
}

void ImageLoaderModule::ResetGIFState()
{
   mGIFAnimator = GIFAnimator{};
   mGIFCurrentFrame = 0;
   mGIFLastFrameTime = 0;
}

void ImageLoaderModule::UploadRGBAToFBO(unsigned char* data, int w, int h)
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
   {
      mLoadedPath.clear();
      return;
   }

   mFBO->Bind();
   NVGpaint imgPaint = nvgImagePattern(gNanoVG, 0, 0, w, h, 0.0f, mImageHandle, 1.0f);
   nvgBeginPath(gNanoVG);
   nvgRect(gNanoVG, 0, 0, w, h);
   nvgFillPaint(gNanoVG, imgPaint);
   nvgFill(gNanoVG);
   mFBO->Unbind();
}

namespace
{
   bool IsFileReady(const juce::File& file)
   {
      if (!file.existsAsFile())
         return false;
      juce::FileInputStream stream(file);
      if (!stream.openedOk())
         return false;
      int64_t size = stream.getTotalLength();
      if (size <= 0)
         return false;
      // Check that the file isn't being written to by checking
      // if the size is stable after a brief wait
      juce::Time::waitForMillisecondCounter(juce::Time::getMillisecondCounter() + 20);
      if (stream.getTotalLength() != size)
         return false;
      return true;
   }
}

void ImageLoaderModule::DoLoadImage()
{
   if (mPendingPath.empty())
      return;

   juce::File file(mPendingPath);
   if (!IsFileReady(file))
   {
      // File not ready yet (being written), retry next frame
      mPendingLoad = true;
      return;
   }

   mLoadedPath = mPendingPath;
   mPendingPath.clear();

   std::string ext = file.getFileExtension().toLowerCase().toStdString();
   bool isGif = (ext == ".gif");

   if (isGif)
   {
      ResetGIFState();
      std::string path = file.getFullPathName().toStdString();
      if (!mGIFAnimator.Load(path))
      {
         mLoadedPath.clear();
         return;
      }

      mImageWidth = mGIFAnimator.GetCanvasWidth();
      mImageHeight = mGIFAnimator.GetCanvasHeight();

      // Upload first frame
      int numFrames = mGIFAnimator.GetNumFrames();
      if (numFrames > 0)
      {
         const auto* frameData = mGIFAnimator.GetFrameRGBA(0);
         if (!frameData)
         {
            mLoadedPath.clear();
            return;
         }

         UploadRGBAToFBO(const_cast<unsigned char*>(frameData), mImageWidth, mImageHeight);
         mGIFCurrentFrame = 0;
         mGIFLastFrameTime = 0;

         // If only 1 frame, treat as static image
         if (numFrames <= 1)
            mGIFAnimator = GIFAnimator{};
      }
      return;
   }

   auto juceImage = juce::ImageFileFormat::loadFrom(file);
   if (!juceImage.isValid())
   {
      mLoadedPath.clear();
      return;
   }

   int newW = juceImage.getWidth();
   int newH = juceImage.getHeight();

   mImageWidth = newW;
   mImageHeight = newH;

   juce::Image::BitmapData bitmapData(juceImage, juce::Image::BitmapData::readOnly);
   std::vector<unsigned char> rgbaData(mImageWidth * mImageHeight * 4);
   for (int y = 0; y < mImageHeight; ++y)
   {
      for (int x = 0; x < mImageWidth; ++x)
      {
         int si = y * mImageWidth + x;
         auto c = bitmapData.getPixelColour(x, y);
         rgbaData[si * 4 + 0] = c.getRed();
         rgbaData[si * 4 + 1] = c.getGreen();
         rgbaData[si * 4 + 2] = c.getBlue();
         rgbaData[si * 4 + 3] = c.getAlpha();
      }
   }

   UploadRGBAToFBO(rgbaData.data(), mImageWidth, mImageHeight);
}

void ImageLoaderModule::UpdateGIFAnimation()
{
   if (mGIFAnimator.GetNumFrames() <= 0)
      return;

   int numFrames = mGIFAnimator.GetNumFrames();
   int currentDelay = mGIFAnimator.GetFrameDelay(mGIFCurrentFrame);
   if (currentDelay <= 0) currentDelay = 10; // default 100ms

   double frameDuration = currentDelay / 1000.0; // delay is in milliseconds

   double elapsed = gTime - mGIFLastFrameTime;

   if (elapsed >= frameDuration)
   {
      mGIFCurrentFrame = (mGIFCurrentFrame + 1) % numFrames;
      mGIFLastFrameTime = gTime;

      const auto* frameData = mGIFAnimator.GetFrameRGBA(mGIFCurrentFrame);
      if (frameData)
         UploadRGBAToFBO(const_cast<unsigned char*>(frameData), mImageWidth, mImageHeight);
   }
}

void ImageLoaderModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mLoadedPath;
   out << mWidth;
   out << mHeight;
}

void ImageLoaderModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   std::string path;
   in >> path;
   in >> mWidth;
   in >> mHeight;
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, 10);

   if (!path.empty())
   {
      mPendingPath = path;
      mPendingLoad = true;
   }
}

VisualFBO* ImageLoaderModule::GetFBO()
{
   return mFBO;
}

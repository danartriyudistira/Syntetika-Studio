#include "ImageLoaderModule.h"
#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"

#include "juce_gui_basics/juce_gui_basics.h"

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
   mOutputCable->SetManualPosition(mWidth - 15, 10);
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
      mOutputCable->SetManualPosition(mWidth - 15, 10);
}

void ImageLoaderModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mBrowseButton)
   {
      auto pattern = "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.tga;*.psd";
      juce::FileChooser chooser("Load Image", juce::File(), pattern, true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen())
      {
         juce::File file = chooser.getResult();
         mPendingPath = file.getFullPathName().toStdString();
         mPendingLoad = true;
      }
   }
}

void ImageLoaderModule::DoLoadImage()
{
   if (mPendingPath.empty())
      return;

   juce::File file(mPendingPath);
   if (!file.existsAsFile())
      return;

   auto juceImage = juce::ImageFileFormat::loadFrom(file);
   if (!juceImage.isValid())
      return;

   int newW = juceImage.getWidth();
   int newH = juceImage.getHeight();

   // Delete old image handle from existing FBO context BEFORE destroying it
   if (mImageHandle >= 0 && mFBO)
   {
      NVGcontext* oldNvg = mFBO->GetNVGContext();
      if (oldNvg)
         nvgDeleteImage(oldNvg, mImageHandle);
      mImageHandle = -1;
   }

   // Recreate FBO (destroys old context internally if needed)
   if (!mFBO)
      mFBO = new VisualFBO();
   mFBO->Create(newW, newH);

   mImageWidth = newW;
   mImageHeight = newH;
   mLoadedPath = mPendingPath;

   // Get pixel data
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

   // Create nanovg image handle in FBO's context
   {
      NVGcontext* savedNvg = gNanoVG;
      gNanoVG = mFBO->GetNVGContext();
      mImageHandle = nvgCreateImageRGBA(gNanoVG, mImageWidth, mImageHeight, 0, rgbaData.data());
      gNanoVG = savedNvg;
   }

   // Render image into FBO using Bind/Unbind (handles context switch + frame lifecycle)
   mFBO->Bind();

   NVGpaint imgPaint = nvgImagePattern(gNanoVG, 0, 0, mImageWidth, mImageHeight, 0.0f, mImageHandle, 1.0f);
   nvgBeginPath(gNanoVG);
   nvgRect(gNanoVG, 0, 0, mImageWidth, mImageHeight);
   nvgFillPaint(gNanoVG, imgPaint);
   nvgFill(gNanoVG);

   mFBO->Unbind();
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

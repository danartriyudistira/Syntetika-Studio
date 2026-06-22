#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"
#include "ClickButton.h"
#include "GIFAnimator.h"
#include "Slider.h"

#include <vector>
#include <string>

class VisualFBO;

class ImageSequencerModule : public IDrawableModule, public IButtonListener, public IFloatSliderListener, public IVisualSource
{
public:
   ImageSequencerModule();
   ~ImageSequencerModule();

   static IDrawableModule* Create() { return new ImageSequencerModule(); }
   static bool CanCreate() { return true; }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void DrawModule() override;
   void PostRender() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& width, float& height) override;
   void ButtonClicked(ClickButton* button, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   //IVisualSource
   VisualFBO* GetFBO() override;

private:
   struct ImageEntry
   {
      std::string filePath;
      bool isGif{ false };
      GIFAnimator gifAnimator;
      int gifCurrentFrame{ 0 };
      double gifLastFrameTime{ 0 };
   };

   void DoScanFolder();
   void LoadImageAtIndex(int index);
   void UploadRGBAToFBO(unsigned char* data, int w, int h);
   void ClearImages();
   void AdvanceFrame();

   std::string mFolderPath;
   std::vector<ImageEntry> mImages;
   int mCurrentIndex{ 0 };

   VisualFBO* mFBO{ nullptr };
   int mImageHandle{ -1 };
   int mImageWidth{ 0 };
   int mImageHeight{ 0 };

   PatchCableSource* mOutputCable{ nullptr };
   ClickButton* mBrowseButton{ nullptr };
   ClickButton* mPlayPauseButton{ nullptr };
   ClickButton* mPrevButton{ nullptr };
   ClickButton* mNextButton{ nullptr };
   float mFps{ 24 };
   FloatSlider* mFpsSlider{ nullptr };

   bool mPlaying{ true };
   bool mPendingScan{ false };
   bool mPendingLoad{ false };
   int mPendingLoadIndex{ -1 };
   double mLastFrameTime{ 0 };

   float mWidth{ 320 };
   float mHeight{ 280 };
   static constexpr float kMinWidth = 200;
   static constexpr float kMinHeight = 200;
};

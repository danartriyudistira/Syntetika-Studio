#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"
#include "ClickButton.h"

class VisualFBO;

class ImageLoaderModule : public IDrawableModule, public IButtonListener, public IVisualSource
{
public:
   ImageLoaderModule();
   ~ImageLoaderModule();

   static IDrawableModule* Create() { return new ImageLoaderModule(); }
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

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   //IVisualSource
   VisualFBO* GetFBO() override;

private:
   void DoLoadImage();

   std::string mPendingPath;
   std::string mLoadedPath;
   VisualFBO* mFBO{ nullptr };
   PatchCableSource* mOutputCable{ nullptr };

   ClickButton* mBrowseButton{ nullptr };

   int mImageHandle{ -1 };
   int mImageWidth{ 0 };
   int mImageHeight{ 0 };
   bool mPendingLoad{ false };

   float mWidth{ 280 };
   float mHeight{ 240 };
   static constexpr float kMinWidth = 150;
   static constexpr float kMinHeight = 120;
};

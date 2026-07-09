#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "DropdownList.h"
#include "Checkbox.h"

class SpatialRender;

class SpatialSource : public IAudioProcessor,
                      public IDrawableModule,
                      public IFloatSliderListener,
                      public IDropdownListener
{
public:
   SpatialSource();
   virtual ~SpatialSource();
   static IDrawableModule* Create() { return new SpatialSource(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsEnabled() const override { return mEnabled; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override {}
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 2; }

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   float GetPositionX() const { return mX; }
   float GetPositionY() const { return mY; }
   float GetPositionZ() const { return mZ; }

   void SetPosition(float x, float y, float z);
   void GetAudioBuffer(float* dst, int bufferSize);

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = 130; h = 200; }

   friend class SpatialRender;

   float mX{ 0.0f };
   float mY{ -200.0f };
   float mZ{ 100.0f };
   float mVolume{ 1.0f };
   float mOcclusion{ 0.0f };
   int   mColorHue{ 0 };
   int   mAnimMode{ 0 };
   float mAnimRate{ 0.5f };
   float mAnimDepth{ 0.5f };
   bool  mOrbitInvert{ false };
   bool  mEnabled{ true };

   SpatialRender* mRegisteredRender{ nullptr };

   FloatSlider* mXSlider{ nullptr };
   FloatSlider* mYSlider{ nullptr };
   FloatSlider* mZSlider{ nullptr };
   FloatSlider* mVolumeSlider{ nullptr };
   FloatSlider* mOcclusionSlider{ nullptr };
   DropdownList* mAnimModeDropdown{ nullptr };
   FloatSlider* mAnimRateSlider{ nullptr };
   FloatSlider* mAnimDepthSlider{ nullptr };
   Checkbox*    mOrbitInvertCheckbox{ nullptr };
   DropdownList* mColorDropdown{ nullptr };
};

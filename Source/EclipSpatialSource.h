#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "DropdownList.h"
#include "PatchCableSource.h"

class EclipSpatialRender;

class EclipSpatialSource : public IAudioProcessor,
                           public IDrawableModule,
                           public IFloatSliderListener,
                           public IDropdownListener,
                           public IIntSliderListener
{
public:
   EclipSpatialSource();
   virtual ~EclipSpatialSource();
   static IDrawableModule* Create() { return new EclipSpatialSource(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override {}
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override {}

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 1; }

   bool IsEnabled() const override { return mEnabled; }

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = 130; h = 175; }

   void ResolveRender();

   EclipSpatialRender* mRender{ nullptr };
   IAudioReceiver* mRenderTarget{ nullptr };
   int    mObjectIndex{ 0 };
   float  mX{ 0 };
   float  mY{ 0 };
   float  mZ{ 0 };
   float  mVolume{ 1 };
   float  mOcclusion{ 0 };
   int    mAnimMode{ 0 };
   float  mAnimRate{ 0.5f };
   float  mAnimDepth{ 0.5f };
   int    mColorHue{ 0 };

   IntSlider*    mObjectIndexSlider{ nullptr };
   FloatSlider*  mXSlider{ nullptr };
   FloatSlider*  mYSlider{ nullptr };
   FloatSlider*  mZSlider{ nullptr };
   FloatSlider*  mVolumeSlider{ nullptr };
   FloatSlider*  mOcclusionSlider{ nullptr };
   DropdownList* mAnimModeDropdown{ nullptr };
   FloatSlider*  mAnimRateSlider{ nullptr };
   FloatSlider*  mAnimDepthSlider{ nullptr };
   DropdownList* mColorDropdown{ nullptr };
};

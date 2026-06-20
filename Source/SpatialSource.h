#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"

class SpatialRender;

class SpatialSource : public IAudioProcessor, public IDrawableModule, public IFloatSliderListener
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
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   float GetPositionX() const { return mX; }
   float GetPositionY() const { return mY; }
   float GetPositionZ() const { return mZ; }

   void SetPosition(float x, float y, float z);
   void GetAudioBuffer(float* dst, int bufferSize);

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = 130; h = 75; }

   friend class SpatialRender;

   float mX{ 0.0f };
   float mY{ -200.0f };
   float mZ{ 100.0f };
   bool mEnabled{ true };

   SpatialRender* mRegisteredRender{ nullptr };

   FloatSlider* mXSlider{ nullptr };
   FloatSlider* mYSlider{ nullptr };
   FloatSlider* mZSlider{ nullptr };
};

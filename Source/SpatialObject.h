#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "BiquadFilter.h"

class Spatial2DSpace;

class SpatialObject : public IAudioProcessor, public IDrawableModule, public IFloatSliderListener
{
public:
   SpatialObject();
   virtual ~SpatialObject();
   static IDrawableModule* Create() { return new SpatialObject(); }
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

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = 130; h = 95; }

   enum OutputMode
   {
      kOutputMode_System,
      kOutputMode_Module,
      kOutputMode_Both
   };

   friend class Spatial2DSpace;

   float mX{ 0.0f };
   float mY{ -200.0f };
   float mZ{ 100.0f };
   bool mEnabled{ true };
   int mOutputMode{ kOutputMode_System };
   Spatial2DSpace* mRegisteredSpace{ nullptr };

   FloatSlider* mXSlider{ nullptr };
   FloatSlider* mYSlider{ nullptr };
   FloatSlider* mZSlider{ nullptr };

   BiquadFilter mDistanceFilterL;
   BiquadFilter mDistanceFilterR;
   BiquadFilter mFrontBackFilterL;
   BiquadFilter mFrontBackFilterR;
};

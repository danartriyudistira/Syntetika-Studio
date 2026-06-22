#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "FFT.h"
#include "RollingBuffer.h"

class VisualFBO;

class AudioVisualizerModule : public IAudioProcessor, public IDrawableModule, public IVisualSource, public IFloatSliderListener, public IDropdownListener
{
public:
   AudioVisualizerModule();
   ~AudioVisualizerModule();

   static IDrawableModule* Create() { return new AudioVisualizerModule(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override;

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override {}

   bool IsEnabled() const override { return mEnabled; }

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   //IVisualSource
   VisualFBO* GetFBO() override;

private:
   enum VisualMode
   {
      kMode_Waveform,
      kMode_Spectrum,
      kMode_Both,
      kMode_NumModes
   };

   void DrawModeSelector();
   void DrawWaveform(float x, float y, float w, float h);
   void DrawSpectrum(float x, float y, float w, float h);
   void RenderFBO();
   void CreateFBO();

   //IDrawableModule
   void DrawModule() override;
   void PostRender() override;

   static constexpr int kNumFFTBins = 1024;
   static constexpr int kBinIgnore = 2;
   static constexpr int kMaxWaveformSamples = 4096;
   static constexpr int kNumSpectrumBins = kNumFFTBins / 2 + 1 - kBinIgnore;

   ::FFT mFFT;
   FFTData mFFTData;
   RollingBuffer mRollingInputBuffer;

   float* mWindower{ nullptr };
   float* mSmoother{ nullptr };
   float mWaveformData[kMaxWaveformSamples]{};

   VisualFBO* mFBO{ nullptr };
   PatchCableSource* mOutputCable{ nullptr };

   int mDisplayMode{ kMode_Both };
   float mGain{ 2.0f };
   float mColorHue{ 0.65f };

   DropdownList* mModeSelector{ nullptr };
   FloatSlider* mGainSlider{ nullptr };
   FloatSlider* mColorSlider{ nullptr };

   float mWidth{ 400 };
   float mHeight{ 200 };
   static constexpr float kMinWidth = 200;
   static constexpr float kMinHeight = 120;
};

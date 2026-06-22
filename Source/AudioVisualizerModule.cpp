#include "AudioVisualizerModule.h"
#include "VisualFBO.h"
#include "Profiler.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "OpenFrameworksPort.h"

#include <cmath>

AudioVisualizerModule::AudioVisualizerModule()
: IAudioProcessor(gBufferSize)
, mFFT(kNumFFTBins)
, mFFTData(kNumFFTBins, kNumFFTBins / 2 + 1)
, mRollingInputBuffer(kNumFFTBins * 4)
{
   mWindower = new float[kNumFFTBins];
   for (int i = 0; i < kNumFFTBins; ++i)
      mWindower[i] = -.5f * cos(FTWO_PI * i / kNumFFTBins) + .5f;

   mSmoother = new float[kNumSpectrumBins]{};
}

AudioVisualizerModule::~AudioVisualizerModule()
{
   delete[] mWindower;
   delete[] mSmoother;
   if (mFBO)
      delete mFBO;
}

void AudioVisualizerModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mWidth, 10);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);

   mModeSelector = new DropdownList(this, "mode", 3, 2, &mDisplayMode, 100);
   mModeSelector->AddLabel("Waveform", kMode_Waveform);
   mModeSelector->AddLabel("Spectrum", kMode_Spectrum);
   mModeSelector->AddLabel("Both", kMode_Both);
   AddUIControl(mModeSelector);

   mGainSlider = new FloatSlider(this, "gain", 3, 20, 100, 15, &mGain, 0.1f, 10.0f);
   AddUIControl(mGainSlider);

   mColorSlider = new FloatSlider(this, "color", 110, 2, 100, 15, &mColorHue, 0.0f, 1.0f);
   AddUIControl(mColorSlider);
}

void AudioVisualizerModule::Process(double time)
{
   PROFILER(AudioVisualizerModule);
   SyncBuffers();

   if (mEnabled)
   {
      ComputeSliders(0);

      for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
      {
         if (ch == 0)
            BufferCopy(gWorkBuffer, GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize());
         else
            Add(gWorkBuffer, GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize());
      }

      mRollingInputBuffer.WriteChunk(gWorkBuffer, GetBuffer()->BufferSize(), 0);

      mRollingInputBuffer.ReadChunk(mFFTData.mTimeDomain, kNumFFTBins, 0, 0);
      Mult(mFFTData.mTimeDomain, mWindower, kNumFFTBins);
      mFFT.Forward(mFFTData.mTimeDomain, mFFTData.mRealValues, mFFTData.mImaginaryValues);

      int samplesToRead = std::min(kMaxWaveformSamples, mRollingInputBuffer.Size());
      mRollingInputBuffer.ReadChunk(mWaveformData, samplesToRead, 0, 0);
   }

   IAudioReceiver* target = GetTarget();
   if (target)
   {
      ChannelBuffer* out = target->GetBuffer();
      for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
      {
         Add(out->GetChannel(ch), GetBuffer()->GetChannel(ch), out->BufferSize());
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize(), ch);
      }
   }
   GetBuffer()->Reset();
}

void AudioVisualizerModule::DrawModule()
{
   if (Minimized() || !mEnabled)
      return;

   ofSetColor(15, 15, 25);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   float controlsH = 38;
   float contentY = controlsH;
   float contentH = mHeight - contentY;

   if (mFBO && mFBO->IsValid() && contentH > 0)
   {
      mFBO->Draw(0, contentY, mWidth, contentH);
   }
   else if (contentH > 0)
   {
      ofSetColor(30, 30, 45);
      ofFill();
      ofRect(0, contentY, mWidth, contentH);
   }

   ofSetColor(40, 40, 55);
   ofFill();
   ofRect(0, 0, mWidth, controlsH);

   mModeSelector->Draw();
   mGainSlider->Draw();
   mColorSlider->Draw();
}

void AudioVisualizerModule::PostRender()
{
   if (!mEnabled)
      return;

   float contentH = mHeight - 38;
   if (contentH <= 0)
      return;

   if (!mFBO || !mFBO->IsValid() ||
       mFBO->GetWidth() != (int)mWidth ||
       mFBO->GetHeight() != (int)contentH)
   {
      if (mFBO)
      {
         delete mFBO;
         mFBO = nullptr;
      }
   }

   if (!mFBO)
   {
      mFBO = new VisualFBO();
      mFBO->Create(std::max(64, (int)mWidth), std::max(64, (int)contentH));
   }

   if (!mFBO || !mFBO->IsValid())
      return;

   RenderFBO();
}

void AudioVisualizerModule::RenderFBO()
{
   mFBO->Bind();

   float w = (float)mFBO->GetWidth();
   float h = (float)mFBO->GetHeight();

   float smearAlpha = 0.85f;
   nvgBeginPath(gNanoVG);
   nvgRect(gNanoVG, 0, 0, w, h);
   nvgFillColor(gNanoVG, nvgRGBAf(0, 0, 0, smearAlpha));
   nvgFill(gNanoVG);

   if (mDisplayMode == kMode_Waveform)
      DrawWaveform(0, 0, w, h);
   else if (mDisplayMode == kMode_Spectrum)
      DrawSpectrum(0, 0, w, h);
   else
   {
      float halfH = h / 2.0f;
      float sep = 2;
      DrawWaveform(0, 0, w, halfH - sep);
      DrawSpectrum(0, halfH + sep, w, halfH - sep);
   }

   mFBO->Unbind();
}

void AudioVisualizerModule::DrawWaveform(float x, float y, float w, float h)
{
   ofSetColor(255, 255, 255);
   ofSetLineWidth(2);
   ofNoFill();

   ofBeginShape();
   int numSamples = std::min(kMaxWaveformSamples, mRollingInputBuffer.Size());
   if (numSamples > 0)
   {
      float centerY = y + h / 2;
      float ampScale = h / 2 * mGain * 0.5f;

      for (int i = 0; i < numSamples; ++i)
      {
         float px = x + (float)i / numSamples * w;
         float val = mWaveformData[i] * ampScale;
         val = ofClamp(val, -h / 2, h / 2);
         ofVertex(px, centerY + val);
      }
   }
   ofEndShape(false);
}

void AudioVisualizerModule::DrawSpectrum(float x, float y, float w, float h)
{
   float hue = mColorHue;
   ofColor bright, dim;
   bright.setHsb((int)(hue * 255), 255, 255);
   dim.setHsb((int)(hue * 255), 200, 120);

   int end = kNumFFTBins / 2 + 1;
   ofSetLineWidth(1);
   ofBeginShape();
   for (int i = kBinIgnore; i < end; ++i)
   {
      float fx = x + sqrtf((float)(i - kBinIgnore) / (end - kBinIgnore - 1)) * w;
      float raw = sqrtf(fabsf(mFFTData.mRealValues[i]) / end) * 4;
      float samp = ofClamp(raw, 0, 1);
      mSmoother[i - kBinIgnore] = ofLerp(mSmoother[i - kBinIgnore], samp, 0.15f);
      float fy = y + h - mSmoother[i - kBinIgnore] * h;
      ofVertex(fx, fy);
   }
   ofEndShape(false);

   ofSetLineWidth(3);
   ofBeginShape();
   for (int i = kBinIgnore; i < end; ++i)
   {
      float fx = x + sqrtf((float)(i - kBinIgnore) / (end - kBinIgnore - 1)) * w;
      float fy = y + h - mSmoother[i - kBinIgnore] * h;
      ofVertex(fx, fy);
   }
   ofEndShape(false);

   ofSetLineWidth(1);
}

void AudioVisualizerModule::GetModuleDimensions(float& w, float& h)
{
   w = mWidth;
   h = mHeight;
}

void AudioVisualizerModule::Resize(float w, float h)
{
   bool sizeChanged = (mWidth != w || mHeight != h);
   mWidth = std::max(kMinWidth, w);
   mHeight = std::max(kMinHeight, h);

   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, 10);

   if (sizeChanged && mFBO)
   {
      delete mFBO;
      mFBO = nullptr;
   }
}

VisualFBO* AudioVisualizerModule::GetFBO()
{
   return mFBO;
}

void AudioVisualizerModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mDisplayMode;
   out << mGain;
   out << mColorHue;
   out << mWidth;
   out << mHeight;
}

void AudioVisualizerModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mDisplayMode;
   in >> mGain;
   in >> mColorHue;
   in >> mWidth;
   in >> mHeight;
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth, 10);
}

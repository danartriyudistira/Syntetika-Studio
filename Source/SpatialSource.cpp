#include "SpatialSource.h"
#include "SpatialRender.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "SynthGlobals.h"
#include "PatchCableSource.h"

SpatialSource::SpatialSource()
: IAudioProcessor(gBufferSize)
{
}

void SpatialSource::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   mXSlider = new FloatSlider(this, "x (cm)", 5, 2, 120, 15, &mX, -2000, 2000);
   mYSlider = new FloatSlider(this, "y (cm)", 5, 20, 120, 15, &mY, -2000, 2000);
   mZSlider = new FloatSlider(this, "z (cm)", 5, 38, 120, 15, &mZ, 0, 1000);
}

SpatialSource::~SpatialSource()
{
   if (mRegisteredRender)
      mRegisteredRender->UnregisterSource(this);
}

void SpatialSource::Process(double time)
{
   PROFILER(SpatialSource);

   SyncBuffers();
   int bufferSize = GetBuffer()->BufferSize();

   if (mEnabled && mRegisteredRender)
   {
      float* audioIn = GetBuffer()->GetChannel(0);
      mRegisteredRender->AcceptSourceAudio(this, audioIn, bufferSize);
   }

   GetBuffer()->Reset();
}

void SpatialSource::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   IAudioSource::PostRepatch(cableSource, fromUserClick);
   IAudioReceiver* target = GetTarget();
   SpatialRender* render = dynamic_cast<SpatialRender*>(target);
   if (render)
   {
      if (mRegisteredRender && mRegisteredRender != render)
         mRegisteredRender->UnregisterSource(this);
      render->RegisterSource(this);
      mRegisteredRender = render;
   }
   else if (mRegisteredRender)
   {
      mRegisteredRender->UnregisterSource(this);
      mRegisteredRender = nullptr;
   }
}

void SpatialSource::SetPosition(float x, float y, float z)
{
   mX = x;
   mY = y;
   mZ = z;
}

void SpatialSource::GetAudioBuffer(float* dst, int bufferSize)
{
   const float* src = GetBuffer()->GetChannel(0);
   if (src)
   {
      for (int i = 0; i < bufferSize; ++i)
         dst[i] = src[i];
   }
}

void SpatialSource::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mXSlider->Draw();
   mYSlider->Draw();
   mZSlider->Draw();
}

void SpatialSource::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (mRegisteredRender)
      mRegisteredRender->NotifySourceMoved(this);
}

void SpatialSource::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("x", moduleInfo, 0.0f, mXSlider);
   mModuleSaveData.LoadFloat("y", moduleInfo, -200.0f, mYSlider);
   mModuleSaveData.LoadFloat("z", moduleInfo, 100.0f, mZSlider);
   SetUpFromSaveData();
}

void SpatialSource::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mX = mModuleSaveData.GetFloat("x");
   mY = mModuleSaveData.GetFloat("y");
   mZ = mModuleSaveData.GetFloat("z");
}

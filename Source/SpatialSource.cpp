#include "SpatialSource.h"
#include "SpatialRender.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "SynthGlobals.h"
#include "PatchCableSource.h"
#include "UIControlMacros.h"

SpatialSource::SpatialSource()
: IAudioProcessor(gBufferSize)
{
}

void SpatialSource::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   GetPatchCableSource()->AddTypeFilter("spatialrender");

   UIBLOCK0();
   FLOATSLIDER(mXSlider, "x (cm)", &mX, -2000, 2000);
   FLOATSLIDER(mYSlider, "y (cm)", &mY, -2000, 2000);
   FLOATSLIDER(mZSlider, "z (cm)", &mZ, 0, 1000);
   FLOATSLIDER(mVolumeSlider, "vol", &mVolume, 0, 2);
   FLOATSLIDER(mOcclusionSlider, "occlusion", &mOcclusion, 0, 1);
   DROPDOWN(mAnimModeDropdown, "anim", &mAnimMode, 60);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mAnimRateSlider, "rate", &mAnimRate, 0, 5);
   FLOATSLIDER(mAnimDepthSlider, "depth", &mAnimDepth, 0, 1);
   UIBLOCK_NEWLINE();
   DROPDOWN(mColorDropdown, "color", &mColorHue, 60);
   ENDUIBLOCK0();

   mAnimModeDropdown->AddLabel("static", 0);
   mAnimModeDropdown->AddLabel("orbit", 1);
   mAnimModeDropdown->AddLabel("lfo x", 2);
   mAnimModeDropdown->AddLabel("lfo xy", 3);
   mAnimModeDropdown->AddLabel("lfo xyz", 4);

   mColorDropdown->AddLabel("red", 0);
   mColorDropdown->AddLabel("orange", 30);
   mColorDropdown->AddLabel("yellow", 60);
   mColorDropdown->AddLabel("green", 120);
   mColorDropdown->AddLabel("cyan", 180);
   mColorDropdown->AddLabel("blue", 240);
   mColorDropdown->AddLabel("magenta", 300);
   mColorDropdown->AddLabel("white", -1);
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
      mRegisteredRender->SetSourceProperties(this, mVolume, mOcclusion, mColorHue,
                                              mAnimMode, mAnimRate, mAnimDepth);
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
   mVolumeSlider->Draw();
   mOcclusionSlider->Draw();
   mAnimModeDropdown->Draw();
   mAnimRateSlider->Draw();
   mAnimDepthSlider->Draw();
   mColorDropdown->Draw();
}

void SpatialSource::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (mRegisteredRender)
      mRegisteredRender->NotifySourceMoved(this);
}

void SpatialSource::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mX;
   out << mY;
   out << mZ;
   out << mVolume;
   out << mOcclusion;
   out << mColorHue;
   out << mAnimMode;
   out << mAnimRate;
   out << mAnimDepth;
}

void SpatialSource::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev < 1) return;
   in >> mX;
   in >> mY;
   in >> mZ;
   in >> mVolume;
   in >> mOcclusion;
   in >> mColorHue;
   in >> mAnimMode;
   in >> mAnimRate;
   in >> mAnimDepth;
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

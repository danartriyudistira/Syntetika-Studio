#include "EclipSpatialSource.h"
#include "EclipSpatialRender.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include "OpenFrameworksPort.h"
#include "PatchCableSource.h"

EclipSpatialSource::EclipSpatialSource()
: IAudioProcessor(gBufferSize)
{
}

EclipSpatialSource::~EclipSpatialSource()
{
}

void EclipSpatialSource::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   GetPatchCableSource()->AddTypeFilter("eclipspatialrender");

   UIBLOCK0();
   INTSLIDER(mObjectIndexSlider, "obj", &mObjectIndex, 0, 7);
   FLOATSLIDER(mXSlider, "x", &mX, -1, 1);
   FLOATSLIDER(mYSlider, "y", &mY, -1, 1);
   FLOATSLIDER(mZSlider, "z", &mZ, -1, 1);
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

void EclipSpatialSource::Process(double time)
{
   PROFILER(EclipSpatialSource);

   if (GetTarget() == nullptr)
      return;

   if (mRender == nullptr || GetTarget() != mRenderTarget)
   {
      mRender = dynamic_cast<EclipSpatialRender*>(GetTarget());
      mRenderTarget = GetTarget();
   }

   if (mRender == nullptr)
      return;

   SyncBuffers();

   int bufferSize = GetBuffer()->BufferSize();
   float* left = GetBuffer()->GetChannel(0);
   float* right = nullptr;
   if (GetBuffer()->NumActiveChannels() > 1)
      right = GetBuffer()->GetChannel(1);

   mRender->SetObjectAudio(mObjectIndex, left, right, bufferSize, mX, mY, mZ, mVolume);
   mRender->SetObjectProperties(mObjectIndex, mColorHue, mOcclusion,
                                 (EclipSpatialRender::AnimMode)mAnimMode, mAnimRate, mAnimDepth);

   GetVizBuffer()->WriteChunk(left, bufferSize, 0);
   if (right)
      GetVizBuffer()->WriteChunk(right, bufferSize, 1);

   GetBuffer()->Reset();
}

void EclipSpatialSource::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mObjectIndexSlider->Draw();
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

void EclipSpatialSource::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   SetUpFromSaveData();
}

void EclipSpatialSource::SaveLayout(ofxJSONElement& moduleInfo)
{
}

void EclipSpatialSource::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   ResolveRender();
}

void EclipSpatialSource::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mObjectIndex;
   out << mX;
   out << mY;
   out << mZ;
   out << mVolume;
   out << mOcclusion;
   out << mAnimMode;
   out << mAnimRate;
   out << mAnimDepth;
   out << mColorHue;
}

void EclipSpatialSource::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev < 1) return;
   in >> mObjectIndex;
   in >> mX;
   in >> mY;
   in >> mZ;
   in >> mVolume;
   in >> mOcclusion;
   in >> mAnimMode;
   in >> mAnimRate;
   in >> mAnimDepth;
   in >> mColorHue;
}

void EclipSpatialSource::ResolveRender()
{
   mRender = dynamic_cast<EclipSpatialRender*>(GetTarget());
   mRenderTarget = GetTarget();
}

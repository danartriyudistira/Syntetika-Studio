#include "ModelDSynth.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "IAudioReceiver.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"

static const int kColumnW = 115;
static const int kGap = 3;
static const int kSectionGap = 18;

ModelDSynth::ModelDSynth()
: mPolyMgr(this)
, mNoteInputBuffer(this)
, mWriteBuffer(gBufferSize)
{
   mPolyMgr.Init(kVoiceType_ModelD, &mVoiceParams);
}

void ModelDSynth::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   float w, h;

   UIBLOCK(3, 15, kColumnW);
   DROPDOWN(mOsc1Type, "type", (int*)(&mVoiceParams.mOsc1Type), 50);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(30);
   INTSLIDER(mOsc1Octave, "oct", &mVoiceParams.mOsc1Octave, -3, 3);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mOsc1Semitone, "semi", &mVoiceParams.mOsc1Semitone, -12, 12);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(40);
   FLOATSLIDER_DIGITS(mOsc1Fine, "fine", &mVoiceParams.mOsc1Fine, -100, 100, 0);
   UIBLOCK_NEWLINE();
   UIBLOCK_POPSLIDERWIDTH();
   FLOATSLIDER_DIGITS(mOsc1PW, "pw", &mVoiceParams.mOsc1PW, 0, 1, 2);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mOsc1Vol, "vol", &mVoiceParams.mOsc1Vol, 0, 2);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mOsc1Enabled, "on", &mVoiceParams.mOsc1Enabled);
   ENDUIBLOCK(w, h);

   UIBLOCK(3, h + kSectionGap, kColumnW);
   DROPDOWN(mOsc2Type, "type", (int*)(&mVoiceParams.mOsc2Type), 50);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(30);
   INTSLIDER(mOsc2Octave, "oct", &mVoiceParams.mOsc2Octave, -3, 3);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mOsc2Semitone, "semi", &mVoiceParams.mOsc2Semitone, -12, 12);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(40);
   FLOATSLIDER_DIGITS(mOsc2Fine, "fine", &mVoiceParams.mOsc2Fine, -100, 100, 0);
   UIBLOCK_NEWLINE();
   UIBLOCK_POPSLIDERWIDTH();
   FLOATSLIDER_DIGITS(mOsc2PW, "pw", &mVoiceParams.mOsc2PW, 0, 1, 2);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mOsc2Vol, "vol", &mVoiceParams.mOsc2Vol, 0, 2);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mOsc2Enabled, "on", &mVoiceParams.mOsc2Enabled);
   ENDUIBLOCK(w, h);

   UIBLOCK(3, h + kSectionGap, kColumnW);
   DROPDOWN(mOsc3Type, "type", (int*)(&mVoiceParams.mOsc3Type), 50);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(30);
   INTSLIDER(mOsc3Octave, "oct", &mVoiceParams.mOsc3Octave, -3, 3);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mOsc3Semitone, "semi", &mVoiceParams.mOsc3Semitone, -12, 12);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(40);
   FLOATSLIDER_DIGITS(mOsc3Fine, "fine", &mVoiceParams.mOsc3Fine, -100, 100, 0);
   UIBLOCK_NEWLINE();
   UIBLOCK_POPSLIDERWIDTH();
   FLOATSLIDER_DIGITS(mOsc3PW, "pw", &mVoiceParams.mOsc3PW, 0, 1, 2);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mOsc3Vol, "vol", &mVoiceParams.mOsc3Vol, 0, 2);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mOsc3Enabled, "on", &mVoiceParams.mOsc3Enabled);
   ENDUIBLOCK(w, h);

   UIBLOCK(3, h + kSectionGap, 200);
   FLOATSLIDER(mNoiseAmount, "noise", &mVoiceParams.mNoiseAmount, 0, 1);
   ENDUIBLOCK(w, h);

   UIBLOCK(3, h + kSectionGap, kColumnW);
   FLOATSLIDER(mFilterCutoff, "cutoff", &mVoiceParams.mFilterCutoff, 20, 20000);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mFilterResonance, "res", &mVoiceParams.mFilterResonance, 0, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mFilterDrive, "drive", &mVoiceParams.mFilterDrive, 0, 1);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mFilterKbdTrack, "kbd", &mVoiceParams.mFilterKbdTrack, 0, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mFilterEnvAmount, "env amt", &mVoiceParams.mFilterEnvAmount, 0, 1);
   ENDUIBLOCK(w, h);

   UIBLOCK(3, h + kSectionGap, kColumnW);
   FLOATSLIDER(mLFORate, "lfo rate", &mVoiceParams.mLFORate, 0, 20);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mLFODepth, "lfo depth", &mVoiceParams.mLFODepth, 0, 1);
   UIBLOCK_NEWLINE();
   DROPDOWN(mLFOType, "lfo type", (int*)(&mVoiceParams.mLFOType), 80);
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mLFOTarget, "target", &mVoiceParams.mLFOTarget, 80);
   ENDUIBLOCK(w, h);

   UIBLOCK(3, h + kSectionGap, kColumnW);
   UICONTROL_CUSTOM(mADSRDisplay, new ADSRDisplay(UICONTROL_BASICS("amp env"), 80, 40, &mVoiceParams.mAdsr));
   UIBLOCK_SHIFTRIGHT();
   UICONTROL_CUSTOM(mFilterADSRDisplay, new ADSRDisplay(UICONTROL_BASICS("filter env"), 80, 40, &mVoiceParams.mFilterAdsr));
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mVolSlider, "vol", &mVoiceParams.mVol, 0, 2);
   ENDUIBLOCK(w, h);

   mHeight = h + 6;

   for (auto* dd : { mOsc1Type, mOsc2Type, mOsc3Type })
   {
      dd->AddLabel("sin", kOsc_Sin);
      dd->AddLabel("squ", kOsc_Square);
      dd->AddLabel("tri", kOsc_Tri);
      dd->AddLabel("saw", kOsc_Saw);
      dd->AddLabel("-saw", kOsc_NegSaw);
      dd->AddLabel("noise", kOsc_Random);
   }

   mLFOType->AddLabel("sin", kOsc_Sin);
   mLFOType->AddLabel("tri", kOsc_Tri);
   mLFOType->AddLabel("saw", kOsc_Saw);
   mLFOType->AddLabel("squ", kOsc_Square);
   mLFOType->AddLabel("rnd", kOsc_Random);

   mLFOTarget->AddLabel("off", 0);
   mLFOTarget->AddLabel("filter", 1);
   mLFOTarget->AddLabel("amp", 2);

   mFilterCutoff->SetMode(FloatSlider::kSquare);
   mFilterResonance->SetMode(FloatSlider::kSquare);
   mFilterDrive->SetMode(FloatSlider::kSquare);
   mNoiseAmount->SetMode(FloatSlider::kSquare);
   mLFORate->SetMode(FloatSlider::kSquare);
}

ModelDSynth::~ModelDSynth()
{
}

void ModelDSynth::Process(double time)
{
   PROFILER(ModelDSynth);

   IAudioReceiver* target = GetTarget();

   if (!mEnabled || target == nullptr)
      return;

   mNoteInputBuffer.Process(time);

   ComputeSliders(0);

   int bufferSize = target->GetBuffer()->BufferSize();

   mWriteBuffer.Clear();
   mPolyMgr.Process(time, &mWriteBuffer, bufferSize);

   SyncOutputBuffer(mWriteBuffer.NumActiveChannels());
   for (int ch = 0; ch < mWriteBuffer.NumActiveChannels(); ++ch)
   {
      GetVizBuffer()->WriteChunk(mWriteBuffer.GetChannel(ch), mWriteBuffer.BufferSize(), ch);
      Add(target->GetBuffer()->GetChannel(ch), mWriteBuffer.GetChannel(ch), gBufferSize);
   }
}

void ModelDSynth::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled)
      return;

   if (!NoteInputBuffer::IsTimeWithinFrame(time) && GetTarget())
   {
      mNoteInputBuffer.QueueNote(time, pitch, velocity, voiceIdx, modulation);
      return;
   }

   if (velocity > 0)
   {
      mPolyMgr.Start(time, pitch, velocity / 127.0f, voiceIdx, modulation);
   }
   else
   {
      mPolyMgr.Stop(time, pitch, voiceIdx);
   }
}

void ModelDSynth::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
}

void ModelDSynth::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
}

void ModelDSynth::IntSliderUpdated(IntSlider* slider, int oldVal, double time)
{
}

void ModelDSynth::CheckboxUpdated(Checkbox* checkbox, double time)
{
}

void ModelDSynth::RadioButtonUpdated(RadioButton* list, int oldVal, double time)
{
}

void ModelDSynth::SetEnabled(bool enabled)
{
   mEnabled = enabled;
}

void ModelDSynth::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   SetUpFromSaveData();
}

void ModelDSynth::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
}

void ModelDSynth::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   ofSetColor(255, 255, 255, gModuleDrawAlpha);
   DrawTextNormal("VCO 1", 3, mOsc1Type->GetPosition(true).y - 12);
   mOsc1Type->Draw();
   mOsc1Octave->Draw();
   mOsc1Semitone->Draw();
   mOsc1Fine->Draw();
   mOsc1PW->Draw();
   mOsc1Vol->Draw();
   mOsc1Enabled->Draw();

   ofSetColor(80, 80, 80, gModuleDrawAlpha);
   ofLine(3, mOsc2Type->GetPosition(true).y - 4, mWidth - 3, mOsc2Type->GetPosition(true).y - 4);

   ofSetColor(100, 200, 255, gModuleDrawAlpha);
   DrawTextNormal("VCO 2", 3, mOsc2Type->GetPosition(true).y - 12);
   mOsc2Type->Draw();
   mOsc2Octave->Draw();
   mOsc2Semitone->Draw();
   mOsc2Fine->Draw();
   mOsc2PW->Draw();
   mOsc2Vol->Draw();
   mOsc2Enabled->Draw();

   ofSetColor(80, 80, 80, gModuleDrawAlpha);
   ofLine(3, mOsc3Type->GetPosition(true).y - 4, mWidth - 3, mOsc3Type->GetPosition(true).y - 4);

   ofSetColor(255, 255, 255, gModuleDrawAlpha);
   DrawTextNormal("VCO 3", 3, mOsc3Type->GetPosition(true).y - 12);
   mOsc3Type->Draw();
   mOsc3Octave->Draw();
   mOsc3Semitone->Draw();
   mOsc3Fine->Draw();
   mOsc3PW->Draw();
   mOsc3Vol->Draw();
   mOsc3Enabled->Draw();

   ofSetColor(80, 80, 80, gModuleDrawAlpha);
   ofLine(3, mNoiseAmount->GetPosition(true).y - 4, mWidth - 3, mNoiseAmount->GetPosition(true).y - 4);

   ofSetColor(180, 180, 180, gModuleDrawAlpha);
   DrawTextNormal("NOISE", 3, mNoiseAmount->GetPosition(true).y - 12);
   mNoiseAmount->Draw();

   ofSetColor(80, 80, 80, gModuleDrawAlpha);
   ofLine(3, mFilterCutoff->GetPosition(true).y - 4, mWidth - 3, mFilterCutoff->GetPosition(true).y - 4);

   ofSetColor(255, 200, 100, gModuleDrawAlpha);
   DrawTextNormal("LADDER FILTER", 3, mFilterCutoff->GetPosition(true).y - 12);
   mFilterCutoff->Draw();
   mFilterResonance->Draw();
   mFilterDrive->Draw();
   mFilterKbdTrack->Draw();
   mFilterEnvAmount->Draw();

   ofSetColor(80, 80, 80, gModuleDrawAlpha);
   ofLine(3, mLFORate->GetPosition(true).y - 4, mWidth - 3, mLFORate->GetPosition(true).y - 4);

   ofSetColor(180, 220, 150, gModuleDrawAlpha);
   DrawTextNormal("LFO", 3, mLFORate->GetPosition(true).y - 12);
   mLFORate->Draw();
   mLFODepth->Draw();
   mLFOType->Draw();
   mLFOTarget->Draw();

   ofSetColor(80, 80, 80, gModuleDrawAlpha);
   ofLine(3, mADSRDisplay->GetPosition(true).y - 4, mWidth - 3, mADSRDisplay->GetPosition(true).y - 4);

   mADSRDisplay->Draw();
   mFilterADSRDisplay->Draw();
   mVolSlider->Draw();
}

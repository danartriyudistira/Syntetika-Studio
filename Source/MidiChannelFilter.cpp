#include "MidiChannelFilter.h"
#include "SynthGlobals.h"
#include "UIControlMacros.h"

MidiChannelFilter::MidiChannelFilter()
{
}

MidiChannelFilter::~MidiChannelFilter()
{
}

void MidiChannelFilter::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   DROPDOWN(mInputDropdown, "input", &mInputChannel, 80);
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mOutputDropdown, "output", &mOutputChannel, 80);
   ENDUIBLOCK0();

   mInputDropdown->AddLabel("Omni", 0);
   mOutputDropdown->AddLabel("Same", 0);
   for (int ch = 1; ch <= 16; ++ch)
   {
      char lbl[8];
      snprintf(lbl, sizeof(lbl), "Ch %d", ch);
      mInputDropdown->AddLabel(lbl, ch);
      mOutputDropdown->AddLabel(lbl, ch);
   }
}

void MidiChannelFilter::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mInputDropdown->Draw();
   mOutputDropdown->Draw();

   ofSetColor(100, 140, 200);
   gFont.DrawString("in", 5, 12, 2);
   gFont.DrawString("out", 5, 100, 2);

   ofSetColor(80, 80, 100);
   gFont.DrawString("channel filter", 5, 36, 42);
}

void MidiChannelFilter::GetModuleDimensions(float& width, float& height)
{
   width = 200;
   height = 56;
}

void MidiChannelFilter::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled)
   {
      PlayNoteOutput(time, pitch, velocity, voiceIdx, modulation);
      return;
   }

   // voiceIdx=-1 means no channel info — always pass through (treat as omni)
   if (voiceIdx >= 0 && voiceIdx < 16)
   {
      int noteChannel = voiceIdx + 1;
      if (mInputChannel != 0 && noteChannel != mInputChannel)
         return;
   }

   int outVoiceIdx = voiceIdx;
   if (mOutputChannel != 0)
      outVoiceIdx = mOutputChannel - 1;

   PlayNoteOutput(time, pitch, velocity, outVoiceIdx, modulation);
}

void MidiChannelFilter::SendCC(int control, int value, int voiceIdx)
{
   if (!mEnabled)
   {
      SendCCOutput(control, value, voiceIdx);
      return;
   }

   if (voiceIdx >= 0 && voiceIdx < 16)
   {
      int ccChannel = voiceIdx + 1;
      if (mInputChannel != 0 && ccChannel != mInputChannel)
         return;
   }

   int outVoiceIdx = voiceIdx;
   if (mOutputChannel != 0)
      outVoiceIdx = mOutputChannel - 1;

   SendCCOutput(control, value, outVoiceIdx);
}

void MidiChannelFilter::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
}

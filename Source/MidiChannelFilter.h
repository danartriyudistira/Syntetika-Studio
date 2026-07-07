#pragma once

#include "NoteEffectBase.h"
#include "IDrawableModule.h"
#include "DropdownList.h"

class MidiChannelFilter : public NoteEffectBase, public IDrawableModule, public IDropdownListener
{
public:
   MidiChannelFilter();
   virtual ~MidiChannelFilter();
   static IDrawableModule* Create() { return new MidiChannelFilter(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   //INoteReceiver
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override;

   //IDropdownListener
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   bool IsEnabled() const override { return mEnabled; }

private:
   //IDrawableModule
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;

   int mInputChannel{ 0 }; // 0=omni, 1-16
   int mOutputChannel{ 0 }; // 0=same, 1-16
   DropdownList* mInputDropdown{ nullptr };
   DropdownList* mOutputDropdown{ nullptr };
};

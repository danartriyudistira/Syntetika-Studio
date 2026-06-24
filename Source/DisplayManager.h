#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "INoteReceiver.h"
#include "IPulseReceiver.h"
#include "PatchCableSource.h"
#include "DropdownList.h"
#include "ClickButton.h"
#include "Checkbox.h"
#include <vector>
#include <mutex>

class VisualFBO;

class DisplayManager : public IDrawableModule, public IDropdownListener, public IButtonListener, public IVisualSource, public INoteReceiver, public IPulseReceiver
{
public:
   DisplayManager();
   ~DisplayManager();

   static IDrawableModule* Create() { return new DisplayManager(); }
   static bool CanCreate() { return true; }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return true; }

   void CreateUIControls() override;
   void DrawModule() override;
   void PostRender() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& width, float& height) override;

   void PostRepatch(PatchCableSource* source, bool fromUserClick) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void ButtonClicked(ClickButton* button, double time) override;
   void OnClicked(float x, float y, bool right) override;
   void KeyPressed(int key, bool isRepeat) override;

   //INoteReceiver
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void SendMidi(const juce::MidiMessage& message) override {}

   //IPulseReceiver
   void OnPulse(double time, float velocity, int flags) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 3; }

   //IVisualSource
   VisualFBO* GetFBO() override;

private:
   void ResolveSources();
   void ApplyGridSize();
   void AutoSelect();

   std::vector<IVisualSource*> mSources;
   std::vector<PatchCableSource*> mInputCables;

   int mGridRows{ 1 };
   int mGridCols{ 1 };
   int mActiveCell{ -1 };

   int mRootNote{ 36 };
   int mMidiChannel{ -1 };
   float mNoteVelocity{ 0 };

   bool mAutoSwitch{ false };
   std::vector<int> mCellPriority;
   std::vector<bool> mCellHeld;
   int mPulseAdvanceDir{ 1 };

   DropdownList* mRowsDropdown{ nullptr };
   DropdownList* mColsDropdown{ nullptr };
   DropdownList* mRootNoteDropdown{ nullptr };
   DropdownList* mChannelDropdown{ nullptr };
   int mEditingPriority{ 0 };
   DropdownList* mPriorityDropdown{ nullptr };
   ClickButton* mApplyButton{ nullptr };
   Checkbox* mAutoSwitchCheckbox{ nullptr };

   PatchCableSource* mOutputCable{ nullptr };
   VisualFBO* mOutputFBO{ nullptr };

   mutable std::recursive_mutex mDataMutex;

   float mModuleWidth{ 280 };
   float mModuleHeight{ 240 };

   static constexpr float kMinWidth = 280;
   static constexpr float kMinHeight = 180;
};

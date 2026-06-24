/**
    syntetika (experimental fork of bespoke synth), a software modular synthesizer
    Copyright (C) 2021 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/
//
//  PatternMatrix.h
//  modularSynth
//
//

#pragma once

#include <vector>
#include <string>
#include "IDrawableModule.h"
#include "IUIControl.h"
#include "ClickButton.h"
#include "DropdownList.h"
#include "Slider.h"
#include "Checkbox.h"
#include "MidiDevice.h"

class PatternMatrix : public IDrawableModule, public IButtonListener, public IDropdownListener, public MidiDeviceListener
{
public:
   PatternMatrix();
   ~PatternMatrix();
   static IDrawableModule* Create() { return new PatternMatrix(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void Init() override;
   void Poll() override;
   bool IsResizable() const override { return false; }
   bool HasTitleBar() const override { return !mDock; }
   bool AlwaysOnTop() override { return mDock; }

   void ButtonClicked(ClickButton* button, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void MouseReleased() override;
   bool MouseMoved(float x, float y) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
    int GetModuleSaveStateRev() const override { return 7; }

   bool IsEnabled() const override { return true; }

private:
    static const int kMaxSlots = 32;
   static const int kMaxTracks = 16;
   static constexpr float kDragSensitivity = 60.0f;

   enum class CellType { kRotaryKnob, kSlider, kButton };

   struct Cell
   {
      CellType mType{ CellType::kRotaryKnob };
      std::string mTargetPath;
      IUIControl* mTarget{ nullptr };
      float mValue{ 0 };
      float mMin{ 0 };
      float mMax{ 1 };
   };

   struct TrackInfo
   {
      DropdownList* mModuleSelector{ nullptr };
      IDrawableModule* mModule{ nullptr };
      std::string mModuleName;
      int mCurrentPattern{ -1 };
   };

   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;
   void OnClicked(float x, float y, bool right) override;

   void StorePattern(int trackIdx, int slot);
   void LoadPattern(int trackIdx, int slot);
   void TriggerScene(int slot);
   void RebuildModuleList();
   void ResolveModulePtr(int trackIdx);
   bool PatternSave(int trackIdx, int slot);
   bool PatternLoad(int trackIdx, int slot);
   bool PatternHasData(int trackIdx, int slot);
   bool PatternGetData(int trackIdx, int slot, std::vector<char>& out);
   bool PatternSetData(int trackIdx, int slot, const std::vector<char>& in);
   int GetDropdownTrackIndex(DropdownList* list) const;

   void RebuildElementGrids();
   Cell MakeCell(CellType type);

   bool HitTestElementGrid(float px, float py, Cell*& outCell, int* outRow = nullptr, int* outCol = nullptr);
   void DrawRotaryKnob(float cx, float cy, float radius, float val, float min, float max);
   void DrawSlider(float x, float y, float w, float h, float val, float min, float max);
   void DrawButton(float x, float y, float w, float h);
   void RebuildParamList();
   void ResolveElementBindings();
   void Resize(float w, float h) override;

   std::vector<TrackInfo> mTracks;
   int mNumTracks{ 4 };
   int mNumSlots{ 8 };

   // standalone element grids [row][col]
   std::vector<std::vector<Cell>> mRotaryGrid;
   std::vector<std::vector<Cell>> mSliderGrid;
   std::vector<std::vector<Cell>> mButtonGrid;

   int mRotaryCols{ 0 };
   int mRotaryRows{ 0 };
   int mSliderCols{ 0 };
   int mSliderRows{ 0 };
   int mButtonCols{ 0 };
   int mButtonRows{ 0 };

   std::vector<std::string> mModuleNames;
   int mTrackDropdownSelection[kMaxTracks]{};

   // element drag state
   Cell* mDragCell{ nullptr };
   float mDragStartY{ 0 };
   float mDragStartVal{ 0 };

   // context menu
   DropdownList* mContextMenu{ nullptr };
   int mContextTrack{ -1 };
   int mContextSlot{ -1 };
   std::vector<char> mClipboardData;
   Cell* mContextCell{ nullptr };  // element cell being bound

    enum class ContextAction
    {
       kNone = -1,
       kStore,
       kCopy,
       kPaste,
       kClear,
       kRemoveElement,
       kBindElement
    };
   int mContextAction{ (int)ContextAction::kNone };

   // dock
   Checkbox* mDockCheckbox{ nullptr };
   bool mDock{ false };

   // parameter browser
   DropdownList* mParamBrowser{ nullptr };
   int mParamBrowserSelection{ -1 };
   std::vector<std::string> mParamBrowserPaths;
    bool mPendingBind{ false };
    float mPendingBindX{ 0 };
    float mPendingBindY{ 0 };

     // MIDI mapping
     struct MidiBinding
     {
         int mControl{ 0 };
         int mChannel{ 0 };
         int mTrack{ -1 };     // -1 = scene trigger, >= 0 = track
         CellType mCellType{ CellType::kRotaryKnob };
         int mRow{ 0 };
         int mCol{ 0 };
         int mSlot{ -1 };      // -1 = element grid cell, >= 0 = pattern slot/scene
     };

     // -- pages --
     static const int kMaxPages = 8;

     struct PageData
     {
         std::vector<char> mPatternData[kMaxTracks][kMaxSlots];
         int mCurrentPattern[kMaxTracks]{};
         ofColor mColor;
     };

     void SwitchPage(int pageIdx);
     void SnapshotPage(int pageIdx);
     void RestorePage(int pageIdx);
     void InitPages();

    void SetMidiController(std::string name);
    void OnMidiNote(MidiNote& note) override;
    void OnMidiControl(MidiControl& control) override;
    void DrawMidiOverlay(int trackIdx, CellType type, int row, int col, float x, float y, float w, float h);
    void DrawSlotMidiOverlay(int trackIdx, int slot, float x, float y, float w, float h);
    void ApplyBinding(const MidiBinding& binding, float value);
    void CancelCellLearn();
    void RefreshMidiControllerDropdown();

    ClickButton* mEditMapButton{ nullptr };
    DropdownList* mMidiControllerDropdown{ nullptr };
    int mMidiControllerDropdownSelection{ -1 };
    bool mEditMapMode{ false };
    MidiController* mMidiController{ nullptr };
    bool mPendingCellLearn{ false };
    int mLearnTrack{ -1 };
    CellType mLearnCellType{ CellType::kRotaryKnob };
    int mLearnCellRow{ -1 };
    int mLearnCellCol{ -1 };
    int mLearnCellSlot{ -1 };   // >= 0 = learning a slot/scene
    std::vector<MidiBinding> mMidiBindings;
    std::string mMidiControllerName;

    std::vector<PageData> mPages;
    int mNumPages{ 1 };
    int mCurrentPage{ 0 };
};

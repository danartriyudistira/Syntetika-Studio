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
//  PatternMatrix.cpp
//  modularSynth
//
//

#include "PatternMatrix.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "UIControlMacros.h"
#include "NoteStepSequencer.h"
#include "StepSequencer.h"
#include "CastParameter.h"
#include "MidiController.h"
#include "FillSaveDropdown.h"
#include <algorithm>

PatternMatrix::PatternMatrix()
{
}

PatternMatrix::~PatternMatrix()
{
   if (mMidiController)
      mMidiController->RemoveListener(this);
}

void PatternMatrix::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   for (int i = 0; i < kMaxTracks; ++i)
   {
      TrackInfo track;
      track.mModuleSelector = new DropdownList(this, ("seq" + ofToString(i)).c_str(), -1, -1, &mTrackDropdownSelection[i], 120);
      track.mModuleSelector->AddLabel("(none)", -1);
      track.mModuleSelector->SetDrawTriangle(true);
      mTracks.push_back(track);
   }

    mContextMenu = new DropdownList(this, "slotcontext", -1, -1, &mContextAction, 160);
    mContextMenu->AddLabel("store", (int)ContextAction::kStore);
    mContextMenu->AddLabel("copy", (int)ContextAction::kCopy);
   mContextMenu->AddLabel("paste", (int)ContextAction::kPaste);
   mContextMenu->AddLabel("clear", (int)ContextAction::kClear);
   mContextMenu->AddLabel("remove", (int)ContextAction::kRemoveElement);
   mContextMenu->AddLabel("bind...", (int)ContextAction::kBindElement);
   mContextMenu->SetCableTargetable(false);
    mContextMenu->SetDisplayStyle(DropdownDisplayStyle::kHamburger);
    mContextMenu->SetShowing(false);

    mDockCheckbox = new Checkbox(this, "dock", -1, -1, &mDock);

    mParamBrowser = new DropdownList(this, "parambrowser", -1, -1, &mParamBrowserSelection, 300);
    mParamBrowser->SetShowing(false);

    mEditMapButton = new ClickButton(this, "edit map", -1, -1);
    mMidiControllerDropdown = new DropdownList(this, "midicontroller", -1, -1, &mMidiControllerDropdownSelection, 180);
    mMidiControllerDropdown->SetShowing(false);

    RebuildElementGrids();
}

void PatternMatrix::Init()
{
   IDrawableModule::Init();
}

void PatternMatrix::Poll()
{
   RebuildModuleList();

   for (int i = 0; i < kMaxTracks; ++i)
   {
      if (mTrackDropdownSelection[i] >= 0 && mTrackDropdownSelection[i] < (int)mModuleNames.size())
      {
         std::string newName = mModuleNames[mTrackDropdownSelection[i]];
         if (newName != mTracks[i].mModuleName)
         {
            mTracks[i].mModuleName = newName;
            ResolveModulePtr(i);
         }
      }
       else
       {
          if (!mTracks[i].mModuleName.empty())
          {
             mTracks[i].mModuleName.clear();
             mTracks[i].mModule = nullptr;
          }
       }
    }

    // deferred bind: open param browser after context menu modal is fully popped
    if (mPendingBind)
    {
       mPendingBind = false;
       RebuildParamList();
       mParamBrowser->SetPosition(mPendingBindX, mPendingBindY);
       mParamBrowser->SetShowing(true);
       mParamBrowser->TestClick(mPendingBindX, mPendingBindY, false);
    }
 }

 void PatternMatrix::RebuildModuleList()
{
   std::vector<std::string> freshList;
   const auto& mods = TheSynth->GetRootContainer()->GetModules();
   for (auto* mod : mods)
   {
      if (dynamic_cast<NoteStepSequencer*>(mod) != nullptr ||
          dynamic_cast<StepSequencer*>(mod) != nullptr)
         freshList.push_back(mod->Name());
   }

   if (freshList == mModuleNames)
      return;

   mModuleNames = freshList;

   for (int i = 0; i < kMaxTracks; ++i)
   {
      mTracks[i].mModuleSelector->Clear();
      mTracks[i].mModuleSelector->AddLabel("(none)", -1);

      int newSelection = -1;
      for (int j = 0; j < (int)mModuleNames.size(); ++j)
      {
         mTracks[i].mModuleSelector->AddLabel(mModuleNames[j].c_str(), j);
         if (mModuleNames[j] == mTracks[i].mModuleName)
            newSelection = j;
      }

      mTrackDropdownSelection[i] = newSelection;
      ResolveModulePtr(i);
   }
}

void PatternMatrix::ResolveModulePtr(int trackIdx)
{
   if (trackIdx < 0 || trackIdx >= kMaxTracks)
      return;

   IDrawableModule* mod = nullptr;
   if (!mTracks[trackIdx].mModuleName.empty())
      mod = TheSynth->FindModule(mTracks[trackIdx].mModuleName, false);
   mTracks[trackIdx].mModule = mod;
}

// --- element grid management ---

PatternMatrix::Cell PatternMatrix::MakeCell(CellType type)
{
   Cell c;
   c.mType = type;
   return c;
}

void PatternMatrix::ClearElementGrids()
{
   for (auto& track : mTracks)
   {
      track.mRotaryGrid.clear();
      track.mSliderGrid.clear();
      track.mButtonGrid.clear();
   }
}

void PatternMatrix::RebuildElementGrids()
{
   for (auto& track : mTracks)
   {
      track.mRotaryGrid.assign(mRotaryRows, std::vector<Cell>(mRotaryCols, MakeCell(CellType::kRotaryKnob)));
      track.mSliderGrid.assign(mSliderRows, std::vector<Cell>(mSliderCols, MakeCell(CellType::kSlider)));
      track.mButtonGrid.assign(mButtonRows, std::vector<Cell>(mButtonCols, MakeCell(CellType::kButton)));
   }
}

void PatternMatrix::RebuildParamList()
{
   mParamBrowser->Clear();
   mParamBrowserPaths.clear();

   mParamBrowser->AddLabel("(none)", -1);
   mParamBrowserPaths.push_back("");

   const auto& mods = TheSynth->GetRootContainer()->GetModules();
   for (auto* mod : mods)
   {
      CastParameter* cp = dynamic_cast<CastParameter*>(mod);
      if (cp == nullptr)
         continue;
      if (cp->GetTarget() == nullptr)
         continue;

      std::string path = cp->GetTargetPath();
      std::string label = std::string(mod->Name());
      mParamBrowser->AddLabel(label.c_str(), (int)mParamBrowserPaths.size());
      mParamBrowserPaths.push_back(path);
   }
}

void PatternMatrix::ResolveElementBindings()
{
   for (auto& track : mTracks)
   {
      for (auto& row : track.mRotaryGrid)
         for (auto& cell : row)
            cell.mTarget = TheSynth->FindUIControl(cell.mTargetPath);
      for (auto& row : track.mSliderGrid)
         for (auto& cell : row)
            cell.mTarget = TheSynth->FindUIControl(cell.mTargetPath);
      for (auto& row : track.mButtonGrid)
         for (auto& cell : row)
            cell.mTarget = TheSynth->FindUIControl(cell.mTargetPath);
   }
}

// --- layout helpers ---

namespace
{
   const float kCellW = 36;
   const float kCellH = 24;
   const float kSelW = 130;
   const float kElemW = 30;
   const float kElemH = 24;
   const float kRowPad = 3;
   const float kHeaderH = 16;
   const float kMargin = 5;
   const float kBottomH = 20;  // bottom bar for dock checkbox

   float GetGridX() { return kMargin + kSelW; }
   float GetHeaderStartY() { return kMargin + 2; }

   int GetElementGridRows(int rotaryRows, int sliderRows, int buttonRows)
   {
      return MAX(MAX(rotaryRows, sliderRows), buttonRows);
   }

   int GetElementGridCols(int rotaryCols, int sliderCols, int buttonCols)
   {
      return rotaryCols + sliderCols + buttonCols;
   }

   float GetElementGridStartX(int numSlots)
   {
      return GetGridX() + numSlots * kCellW + kMargin;
   }

   // Track row: pattern slots on left, element grid on right.
   // First element row shares Y with pattern slots; if grid has more rows,
   // track height grows to accommodate them.
   float GetTrackY(int trackIdx, int extraRows)
   {
      float headerArea = GetHeaderStartY() + kHeaderH + kRowPad;
      float trackH = kCellH + kRowPad;
      if (extraRows > 1)                   // first row shares Y with slots
         trackH += (extraRows - 1) * (kElemH + kRowPad);
      return headerArea + trackIdx * trackH;
   }

   float GetTrackHeight(int extraRows)
   {
      float h = kCellH + kRowPad;
      if (extraRows > 1)
         h += (extraRows - 1) * (kElemH + kRowPad);
      return h;
   }
}

// --- drawing ---

void PatternMatrix::DrawModule()
{
   if (mDock)
   {
      float w, h;
      GetModuleDimensions(w, h);
      SetPosition(0, ofGetHeight() / GetOwningContainer()->GetDrawScale() - h);
      Resize(ofGetWidth() / GetOwningContainer()->GetDrawScale(), h);
   }

   if (Minimized() || IsVisible() == false)
      return;

   float gridX = GetGridX();
   float headerY = GetHeaderStartY();
   int extraRows = GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows);

   ofPushStyle();

   // column headers P1-Px
   for (int c = 0; c < mNumSlots; ++c)
   {
      float x = gridX + c * kCellW;
      ofColor headerColor(50, 50, 50);
      headerColor.a = gModuleDrawAlpha;
      ofSetColor(headerColor);
      ofFill();
      ofRect(x, headerY, kCellW, kHeaderH);
      ofSetColor(60, 60, 60, gModuleDrawAlpha);
      ofNoFill();
      ofRect(x, headerY, kCellW, kHeaderH);
      ofSetColor(200, 200, 200, gModuleDrawAlpha);
      DrawTextNormal(("P" + ofToString(c + 1)).c_str(), x + kCellW / 2 - 7, headerY + 11, 10);
   }

    // dock checkbox (bottom bar)
    float checkY = GetTrackY(mNumTracks - 1, extraRows) + GetTrackHeight(extraRows);
    ofSetColor(40, 40, 40, gModuleDrawAlpha);
    ofFill();
    ofRect(kMargin, checkY, ofGetWidth() / GetOwningContainer()->GetDrawScale() - kMargin * 2, kBottomH);
    mDockCheckbox->SetShowing(GetOwningContainer() == TheSynth->GetRootContainer() || GetOwningContainer() == TheSynth->GetUIContainer());
    mDockCheckbox->SetPosition(kMargin + 2, checkY + 2);
    mDockCheckbox->Draw();

    mEditMapButton->SetPosition(kMargin + 60, checkY + 2);
    mEditMapButton->Draw();

    if (mEditMapMode)
    {
       mMidiControllerDropdown->SetPosition(kMargin + 150, checkY + 2);
       mMidiControllerDropdown->Draw();
    }

   // tracks
   for (int i = 0; i < mNumTracks; ++i)
   {
      float y = GetTrackY(i, extraRows);

      mTracks[i].mModuleSelector->SetPosition(kMargin, y);
      mTracks[i].mModuleSelector->SetWidth(kSelW);
      mTracks[i].mModuleSelector->Draw();

      // pattern slots
      for (int c = 0; c < mNumSlots; ++c)
      {
         float x = gridX + c * kCellW;
         bool hasData = PatternHasData(i, c);

          ofColor color;
          if (mTracks[i].mCurrentPattern == c)
             color.set(0, 200, 80);
          else if (hasData)
            color.set(60, 100, 180);
         else
            color.set(40, 40, 40);
         color.a = gModuleDrawAlpha;
         ofSetColor(color);
         ofFill();
         ofRect(x, y, kCellW, kCellH);
         ofSetColor(60, 60, 60, gModuleDrawAlpha);
         ofNoFill();
         ofRect(x, y, kCellW, kCellH);
         if (hasData)
         {
            ofSetColor(200, 200, 200, gModuleDrawAlpha);
            DrawTextNormal(("P" + ofToString(c + 1)).c_str(), x + kCellW / 2 - 7, y + kCellH / 2 + 3, 10);
         }
      }

      // element grid (first row shares Y with pattern slots)
      if (extraRows > 0)
      {
         float egX = GetElementGridStartX(mNumSlots);
         float egY = y;  // same Y as pattern slots

         // rotary grid
         for (int r = 0; r < (int)mTracks[i].mRotaryGrid.size() && r < mRotaryRows; ++r)
         {
            for (int c = 0; c < (int)mTracks[i].mRotaryGrid[r].size() && c < mRotaryCols; ++c)
            {
               float ex = egX + c * kElemW;
               float ey = egY + r * kElemH;
               const Cell& cell = mTracks[i].mRotaryGrid[r][c];
               DrawRotaryKnob(ex + kElemW / 2, ey + kElemH / 2, kElemW / 2 - 3, cell.mValue, cell.mMin, cell.mMax);
            }
         }

         // slider grid
         float sliderX = egX + mRotaryCols * kElemW;
         for (int r = 0; r < (int)mTracks[i].mSliderGrid.size() && r < mSliderRows; ++r)
         {
            for (int c = 0; c < (int)mTracks[i].mSliderGrid[r].size() && c < mSliderCols; ++c)
            {
               float ex = sliderX + c * kElemW;
               float ey = egY + r * kElemH;
               const Cell& cell = mTracks[i].mSliderGrid[r][c];
               DrawSlider(ex, ey, kElemW, kElemH, cell.mValue, cell.mMin, cell.mMax);
            }
         }

         // button grid
         float btnX = sliderX + mSliderCols * kElemW;
         for (int r = 0; r < (int)mTracks[i].mButtonGrid.size() && r < mButtonRows; ++r)
         {
            for (int c = 0; c < (int)mTracks[i].mButtonGrid[r].size() && c < mButtonCols; ++c)
            {
               float ex = btnX + c * kElemW;
               float ey = egY + r * kElemH;
               DrawButton(ex, ey, kElemW, kElemH);
            }
         }
      }
   }

   // MIDI mapping overlay (draw after cells)
   if (mEditMapMode)
   {
      // column header (scene) overlay
      for (int c = 0; c < mNumSlots; ++c)
      {
         float x = gridX + c * kCellW;
         DrawSlotMidiOverlay(-1, c, x, headerY, kCellW, kHeaderH);
      }

      for (int i = 0; i < mNumTracks; ++i)
      {
         float y = GetTrackY(i, extraRows);
         float egX = GetElementGridStartX(mNumSlots);

         // pattern slot overlay
         for (int c = 0; c < mNumSlots; ++c)
         {
            float sx = gridX + c * kCellW;
            DrawSlotMidiOverlay(i, c, sx, y, kCellW, kCellH);
         }

         // rotary grid overlay
         for (int r = 0; r < (int)mTracks[i].mRotaryGrid.size() && r < mRotaryRows; ++r)
         {
            for (int c = 0; c < (int)mTracks[i].mRotaryGrid[r].size() && c < mRotaryCols; ++c)
            {
               float ex = egX + c * kElemW;
               float ey = y + r * kElemH;
               DrawMidiOverlay(i, CellType::kRotaryKnob, r, c, ex, ey, kElemW, kElemH);
            }
         }

         // slider grid overlay
         float sliderX = egX + mRotaryCols * kElemW;
         for (int r = 0; r < (int)mTracks[i].mSliderGrid.size() && r < mSliderRows; ++r)
         {
            for (int c = 0; c < (int)mTracks[i].mSliderGrid[r].size() && c < mSliderCols; ++c)
            {
               float ex = sliderX + c * kElemW;
               float ey = y + r * kElemH;
               DrawMidiOverlay(i, CellType::kSlider, r, c, ex, ey, kElemW, kElemH);
            }
         }

         // button grid overlay
         float btnX = sliderX + mSliderCols * kElemW;
         for (int r = 0; r < (int)mTracks[i].mButtonGrid.size() && r < mButtonRows; ++r)
         {
            for (int c = 0; c < (int)mTracks[i].mButtonGrid[r].size() && c < mButtonCols; ++c)
            {
               float ex = btnX + c * kElemW;
               float ey = y + r * kElemH;
               DrawMidiOverlay(i, CellType::kButton, r, c, ex, ey, kElemW, kElemH);
            }
         }
      }
   }

   ofPopStyle();
}

void PatternMatrix::DrawRotaryKnob(float cx, float cy, float radius, float val, float min, float max)
{
   float norm = (val - min) / (max - min);
   norm = ofClamp(norm, 0, 1);

   ofSetColor(60, 60, 60, gModuleDrawAlpha);
   ofFill();
   ofCircle(cx, cy, radius);

   ofSetColor(0, 180, 255, gModuleDrawAlpha);
   ofSetLineWidth(2);
   ofNoFill();
   ofCircle(cx, cy, radius);

   float startAngle = -M_PI * 0.75f;
   float endAngle = M_PI * 0.75f;
   float angle = startAngle + norm * (endAngle - startAngle);
   ofVec2f tip(cx + cos(angle) * radius * 0.7f, cy + sin(angle) * radius * 0.7f);
   ofLine(cx, cy, tip.x, tip.y);
   ofSetLineWidth(1);

   ofSetColor(200, 200, 200, gModuleDrawAlpha);
   DrawTextNormal(ofToString((int)(norm * 100)).c_str(), cx - 8, cy + 3, 8);
}

void PatternMatrix::DrawSlider(float x, float y, float w, float h, float val, float min, float max)
{
   float norm = (val - min) / (max - min);
   norm = ofClamp(norm, 0, 1);

   ofSetColor(40, 40, 40, gModuleDrawAlpha);
   ofFill();
   ofRect(x, y, w, h);

   ofSetColor(0, 180, 255, gModuleDrawAlpha);
   float fillH = norm * h;
   ofFill();
   ofRect(x, y + h - fillH, w, fillH);

   ofSetColor(60, 60, 60, gModuleDrawAlpha);
   ofNoFill();
   ofRect(x, y, w, h);
}

void PatternMatrix::DrawButton(float x, float y, float w, float h)
{
   ofSetColor(60, 60, 60, gModuleDrawAlpha);
   ofFill();
   ofRect(x, y, w, h);
   ofSetColor(200, 200, 200, gModuleDrawAlpha);
   DrawTextNormal("B", x + w / 2 - 4, y + h / 2 + 3, 10);
}

void PatternMatrix::GetModuleDimensions(float& width, float& height)
{
   int extraRows = GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows);
   float extraW = GetElementGridCols(mRotaryCols, mSliderCols, mButtonCols) * kElemW;
   if (extraW > 0)
      extraW += kMargin;

   width = kMargin + kSelW + mNumSlots * kCellW + extraW + kMargin;

   // when docked, span full window width
   if (mDock && GetOwningContainer())
      width = MAX(width, ofGetWidth() / GetOwningContainer()->GetDrawScale());

   float lastTrackY = GetTrackY(mNumTracks - 1, extraRows);
   height = lastTrackY + GetTrackHeight(extraRows) + kMargin + kBottomH;
}

// --- click handling ---

bool PatternMatrix::HitTestElementGrid(int trackIdx, float px, float py, Cell*& outCell, int* outRow, int* outCol)
{
   auto& t = mTracks[trackIdx];
   float egX = GetElementGridStartX(mNumSlots);
   float egY = GetTrackY(trackIdx, GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows));

   // check rotary grid
   float x0 = egX;
   for (int r = 0; r < (int)t.mRotaryGrid.size(); ++r)
   {
      for (int c = 0; c < (int)t.mRotaryGrid[r].size(); ++c)
      {
         float ex = x0 + c * kElemW;
         float ey = egY + r * kElemH;
         if (px >= ex && px < ex + kElemW && py >= ey && py < ey + kElemH)
         {
            outCell = &t.mRotaryGrid[r][c];
            if (outRow) *outRow = r;
            if (outCol) *outCol = c;
            return true;
         }
      }
   }

   // check slider grid
   float sx = egX + mRotaryCols * kElemW;
   for (int r = 0; r < (int)t.mSliderGrid.size(); ++r)
   {
      for (int c = 0; c < (int)t.mSliderGrid[r].size(); ++c)
      {
         float ex = sx + c * kElemW;
         float ey = egY + r * kElemH;
         if (px >= ex && px < ex + kElemW && py >= ey && py < ey + kElemH)
         {
            outCell = &t.mSliderGrid[r][c];
            if (outRow) *outRow = r;
            if (outCol) *outCol = c;
            return true;
         }
      }
   }

   // check button grid
   float bx = sx + mSliderCols * kElemW;
   for (int r = 0; r < (int)t.mButtonGrid.size(); ++r)
   {
      for (int c = 0; c < (int)t.mButtonGrid[r].size(); ++c)
      {
         float ex = bx + c * kElemW;
         float ey = egY + r * kElemH;
         if (px >= ex && px < ex + kElemW && py >= ey && py < ey + kElemH)
         {
            outCell = &t.mButtonGrid[r][c];
            if (outRow) *outRow = r;
            if (outCol) *outCol = c;
            return true;
         }
      }
   }

   return false;
}

void PatternMatrix::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   float gridX = GetGridX();
   float headerY = GetHeaderStartY();
   int extraRows = GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows);

   // column header click -> scene trigger / MIDI learn
   for (int c = 0; c < mNumSlots; ++c)
   {
      float colX = gridX + c * kCellW;
      if (x >= colX && x < colX + kCellW && y >= headerY && y < headerY + kHeaderH)
      {
         if (mEditMapMode && mMidiController && !right)
         {
            CancelCellLearn();
            mPendingCellLearn = true;
            mLearnTrack = -1;
            mLearnCellSlot = c;
            return;
         }
         TriggerScene(c);
         return;
      }
   }

    // cancel cell learn when clicking outside interactive area in edit mode
    if (mEditMapMode && mPendingCellLearn)
    {
       bool hitSomething = false;
       // check header area
       for (int c = 0; c < mNumSlots && !hitSomething; ++c)
       {
          float colX = gridX + c * kCellW;
          if (x >= colX && x < colX + kCellW && y >= headerY && y < headerY + kHeaderH)
             hitSomething = true;
       }
       // check track areas
       for (int i = 0; i < mNumTracks && !hitSomething; ++i)
       {
          float ty = GetTrackY(i, extraRows);
          float th = GetTrackHeight(extraRows);
          if (y >= ty && y < ty + th)
             hitSomething = true;
       }
       if (!hitSomething)
          CancelCellLearn();
    }

    // track rows
    for (int i = 0; i < mNumTracks; ++i)
   {
      float ty = GetTrackY(i, extraRows);
      float th = GetTrackHeight(extraRows);

      if (y < ty || y >= ty + th)
         continue;

      // pattern slot area (top part of track)
      float slotAreaBottom = ty + kCellH;
      if (y < slotAreaBottom)
      {
         for (int c = 0; c < mNumSlots; ++c)
         {
            float colX = gridX + c * kCellW;
            if (x >= colX && x < colX + kCellW)
            {
                // MIDI learn mode: click starts pending learn on this slot
                if (mEditMapMode && mMidiController && !right)
                {
                   CancelCellLearn();
                   mPendingCellLearn = true;
                   mLearnTrack = i;
                   mLearnCellSlot = c;
                   return;
                }

                if (right)
                {
                   // right-click only on the active (green) slot
                   if (mTracks[i].mCurrentPattern == c)
                   {
                      mContextTrack = i;
                      mContextSlot = c;
                      mContextMenu->SetPosition(x + kCellW, y);
                      mContextMenu->SetShowing(true);
                      mContextMenu->TestClick(x + kCellW, y, false);  // open menu immediately
                   }
                }
                else
                {
                   if (PatternHasData(i, c))
                      LoadPattern(i, c);  // click filled → load/switch
                   else
                      StorePattern(i, c); // click empty → save
                }
               return;
            }
         }
      }

      // element grid area (below pattern slots)
      Cell* hitCell = nullptr;
      int hitRow = -1, hitCol = -1;
      if (HitTestElementGrid(i, x, y, hitCell, &hitRow, &hitCol))
      {
         // MIDI learn mode: click starts pending learn on this cell
         if (mEditMapMode && mMidiController && !right)
         {
            CancelCellLearn();
            mPendingCellLearn = true;
            mLearnTrack = i;
            mLearnCellType = hitCell->mType;
            mLearnCellRow = hitRow;
            mLearnCellCol = hitCol;
            return;
         }

         if (right)
         {
            mContextTrack = i;
            mContextSlot = -1;
            mContextCell = hitCell;
            mContextMenu->SetPosition(x + kElemW, y);
            mContextMenu->SetShowing(true);
            mContextMenu->TestClick(x + kElemW, y, false);
         }
         else
         {
            switch (hitCell->mType)
            {
               case CellType::kRotaryKnob:
               case CellType::kSlider:
                  mDragTrack = i;
                  mDragCell = hitCell;
                  mDragStartY = y;
                  mDragStartVal = hitCell->mValue;
                  break;
               case CellType::kButton:
                  if (hitCell->mTarget)
                  {
                     float cur = hitCell->mTarget->GetValue();
                     hitCell->mTarget->SetValue(cur > 0 ? 0 : 1, gTime);
                  }
                  break;
            }
         }
         return;
      }
   }
}

bool PatternMatrix::MouseMoved(float x, float y)
{
   if (mDragCell == nullptr)
      return false;

   float dy = mDragStartY - y;
   float range = mDragCell->mMax - mDragCell->mMin;
   if (range == 0)
      range = 1;
   float delta = dy / 60.0f * range;
   mDragCell->mValue = ofClamp(mDragStartVal + delta, mDragCell->mMin, mDragCell->mMax);

   if (mDragCell->mTarget)
      mDragCell->mTarget->SetValue(mDragCell->mValue, gTime);

   return true;
}

void PatternMatrix::MouseReleased()
{
   mDragTrack = -1;
   mDragCell = nullptr;
}

// --- pattern operations ---

void PatternMatrix::StorePattern(int trackIdx, int slot)
{
   if (trackIdx < 0 || trackIdx >= mNumTracks || slot < 0 || slot >= mNumSlots)
      return;
   if (PatternSave(trackIdx, slot))
   {
      mTracks[trackIdx].mCurrentPattern = slot;
      mTracks[trackIdx].mQueuedPattern = -1;
   }
}

void PatternMatrix::LoadPattern(int trackIdx, int slot)
{
   if (trackIdx < 0 || trackIdx >= mNumTracks || slot < 0 || slot >= mNumSlots)
      return;
   if (PatternLoad(trackIdx, slot))
   {
      mTracks[trackIdx].mCurrentPattern = slot;
      mTracks[trackIdx].mQueuedPattern = -1;
   }
}

// --- sequencer dispatch helpers ---

bool PatternMatrix::PatternSave(int trackIdx, int slot)
{
   IDrawableModule* mod = mTracks[trackIdx].mModule;
   if (auto* nss = dynamic_cast<NoteStepSequencer*>(mod))
   {
      nss->SavePattern(slot);
      return true;
   }
   if (auto* ss = dynamic_cast<StepSequencer*>(mod))
   {
      ss->SavePattern(slot);
      return true;
   }
   return false;
}

bool PatternMatrix::PatternLoad(int trackIdx, int slot)
{
   IDrawableModule* mod = mTracks[trackIdx].mModule;
   if (auto* nss = dynamic_cast<NoteStepSequencer*>(mod))
   {
      nss->LoadPattern(slot);
      return true;
   }
   if (auto* ss = dynamic_cast<StepSequencer*>(mod))
   {
      ss->LoadPattern(slot);
      return true;
   }
   return false;
}

bool PatternMatrix::PatternHasData(int trackIdx, int slot)
{
   IDrawableModule* mod = mTracks[trackIdx].mModule;
   if (auto* nss = dynamic_cast<NoteStepSequencer*>(mod))
      return nss->HasPattern(slot);
   if (auto* ss = dynamic_cast<StepSequencer*>(mod))
      return ss->HasPattern(slot);
   return false;
}

bool PatternMatrix::PatternGetData(int trackIdx, int slot, std::vector<char>& out)
{
   IDrawableModule* mod = mTracks[trackIdx].mModule;
   if (auto* nss = dynamic_cast<NoteStepSequencer*>(mod))
      return nss->GetPatternData(slot, out);
   if (auto* ss = dynamic_cast<StepSequencer*>(mod))
      return ss->GetPatternData(slot, out);
   return false;
}

bool PatternMatrix::PatternSetData(int trackIdx, int slot, const std::vector<char>& in)
{
   IDrawableModule* mod = mTracks[trackIdx].mModule;
   if (auto* nss = dynamic_cast<NoteStepSequencer*>(mod))
      return nss->SetPatternData(slot, in);
   if (auto* ss = dynamic_cast<StepSequencer*>(mod))
      return ss->SetPatternData(slot, in);
   return false;
}

// --- scene trigger ---

void PatternMatrix::TriggerScene(int slot)
{
   if (slot < 0 || slot >= mNumSlots)
      return;
   for (int i = 0; i < mNumTracks; ++i)
   {
      if (mTracks[i].mModule && PatternHasData(i, slot))
         LoadPattern(i, slot);
   }
}

int PatternMatrix::GetDropdownTrackIndex(DropdownList* list) const
{
   for (int i = 0; i < kMaxTracks; ++i)
   {
      if (mTracks[i].mModuleSelector == list)
         return i;
   }
   return -1;
}

void PatternMatrix::ButtonClicked(ClickButton* button, double time)
{
   if (button == mEditMapButton)
   {
      mEditMapMode = !mEditMapMode;
      mMidiControllerDropdown->SetShowing(mEditMapMode);
      if (mEditMapMode)
      {
         RefreshMidiControllerDropdown();
      }
      else
      {
         CancelCellLearn();
      }
   }
}

void PatternMatrix::SetMidiController(std::string name)
{
   if (mMidiController)
      mMidiController->RemoveListener(this);

   mMidiControllerName = name;
   mMidiController = TheSynth->FindMidiController(name);
   if (mMidiController)
      mMidiController->AddListener(this, 0);
}

void PatternMatrix::RefreshMidiControllerDropdown()
{
   mMidiControllerDropdown->Clear();
   mMidiControllerDropdown->AddLabel("(none)", -1);
   auto names = TheSynth->GetModuleNames<MidiController*>();
   mMidiControllerDropdownSelection = -1;
   for (int i = 0; i < (int)names.size(); ++i)
   {
      mMidiControllerDropdown->AddLabel(names[i].c_str(), i);
      if (names[i] == mMidiControllerName)
         mMidiControllerDropdownSelection = i;
   }
}

void PatternMatrix::OnMidiNote(MidiNote& note)
{
   if (mPendingCellLearn)
   {
      MidiBinding binding;
      binding.mControl = note.mPitch;
      binding.mChannel = note.mChannel;
      binding.mTrack = mLearnTrack;
      binding.mSlot = mLearnCellSlot;

      if (mLearnCellSlot < 0)
      {
         binding.mCellType = mLearnCellType;
         binding.mRow = mLearnCellRow;
         binding.mCol = mLearnCellCol;
      }

      auto matchPred = [this](const MidiBinding& b)
      {
         if (mLearnCellSlot >= 0)
            return b.mTrack == mLearnTrack && b.mSlot == mLearnCellSlot;
         return b.mTrack == mLearnTrack && b.mCellType == mLearnCellType && b.mRow == mLearnCellRow && b.mCol == mLearnCellCol;
      };
      mMidiBindings.erase(std::remove_if(mMidiBindings.begin(), mMidiBindings.end(), matchPred), mMidiBindings.end());

      mMidiBindings.push_back(binding);
      mPendingCellLearn = false;
      ApplyBinding(binding, note.mVelocity / 127.0f);
      return;
   }

   for (auto& binding : mMidiBindings)
   {
      if (binding.mControl == note.mPitch && binding.mChannel == note.mChannel)
      {
         ApplyBinding(binding, note.mVelocity / 127.0f);
         break;
      }
   }
}

void PatternMatrix::OnMidiControl(MidiControl& control)
{
   if (mPendingCellLearn)
   {
      MidiBinding binding;
      binding.mControl = control.mControl;
      binding.mChannel = control.mChannel;
      binding.mTrack = mLearnTrack;
      binding.mSlot = mLearnCellSlot;

      if (mLearnCellSlot < 0)
      {
         binding.mCellType = mLearnCellType;
         binding.mRow = mLearnCellRow;
         binding.mCol = mLearnCellCol;
      }

      auto matchPred = [this](const MidiBinding& b)
      {
         if (mLearnCellSlot >= 0)
            return b.mTrack == mLearnTrack && b.mSlot == mLearnCellSlot;
         return b.mTrack == mLearnTrack && b.mCellType == mLearnCellType && b.mRow == mLearnCellRow && b.mCol == mLearnCellCol;
      };
      mMidiBindings.erase(std::remove_if(mMidiBindings.begin(), mMidiBindings.end(), matchPred), mMidiBindings.end());

      mMidiBindings.push_back(binding);
      mPendingCellLearn = false;
      ApplyBinding(binding, control.mValue / 127.0f);
      return;
   }

   for (auto& binding : mMidiBindings)
   {
      if (binding.mControl == control.mControl && binding.mChannel == control.mChannel)
      {
         ApplyBinding(binding, control.mValue / 127.0f);
         break;
      }
   }
}

void PatternMatrix::ApplyBinding(const MidiBinding& binding, float value)
{
   if (binding.mSlot >= 0)
   {
      // Pattern slot or scene trigger
      if (binding.mTrack >= 0)
      {
         if (binding.mTrack < mNumTracks && binding.mSlot < mNumSlots && value > 0)
         {
            if (PatternHasData(binding.mTrack, binding.mSlot))
               LoadPattern(binding.mTrack, binding.mSlot);
            else
               StorePattern(binding.mTrack, binding.mSlot);
         }
      }
      else
      {
         // Scene trigger (column header)
         if (binding.mSlot < mNumSlots && value > 0)
            TriggerScene(binding.mSlot);
      }
      return;
   }

   if (binding.mTrack < 0 || binding.mTrack >= mNumTracks)
      return;
   auto& track = mTracks[binding.mTrack];

   Cell* cell = nullptr;
   switch (binding.mCellType)
   {
      case CellType::kRotaryKnob:
         if (binding.mRow < (int)track.mRotaryGrid.size() && binding.mCol < (int)track.mRotaryGrid[binding.mRow].size())
            cell = &track.mRotaryGrid[binding.mRow][binding.mCol];
         break;
      case CellType::kSlider:
         if (binding.mRow < (int)track.mSliderGrid.size() && binding.mCol < (int)track.mSliderGrid[binding.mRow].size())
            cell = &track.mSliderGrid[binding.mRow][binding.mCol];
         break;
      case CellType::kButton:
         if (binding.mRow < (int)track.mButtonGrid.size() && binding.mCol < (int)track.mButtonGrid[binding.mRow].size())
            cell = &track.mButtonGrid[binding.mRow][binding.mCol];
         break;
   }

   if (cell)
   {
      cell->mValue = ofClamp(value, cell->mMin, cell->mMax);
      if (cell->mTarget)
         cell->mTarget->SetValue(cell->mValue, gTime);
   }
}

void PatternMatrix::CancelCellLearn()
{
   mPendingCellLearn = false;
   mLearnTrack = -1;
   mLearnCellRow = -1;
   mLearnCellCol = -1;
   mLearnCellSlot = -1;
}

void PatternMatrix::DrawMidiOverlay(int trackIdx, CellType type, int row, int col, float x, float y, float w, float h)
{
   bool hasBinding = false;
   for (const auto& b : mMidiBindings)
   {
      if (b.mSlot >= 0) continue;
      if (b.mTrack == trackIdx && b.mCellType == type && b.mRow == row && b.mCol == col)
      {
         hasBinding = true;
         break;
      }
   }

   bool isPending = mPendingCellLearn && mLearnCellSlot < 0 && mLearnTrack == trackIdx && mLearnCellType == type && mLearnCellRow == row && mLearnCellCol == col;

   ofColor borderColor;
   float borderAlpha;
   if (isPending)
   {
      borderColor.set(255, 255, 0);
      borderAlpha = 220;
   }
   else if (hasBinding)
   {
      borderColor.set(0, 255, 100);
      borderAlpha = 200;
   }
   else
   {
      borderColor.set(0, 200, 255);
      borderAlpha = 180;
   }

   borderColor.a = borderAlpha;
   ofSetColor(borderColor);
   ofNoFill();
   ofSetLineWidth(2);
   ofRect(x + 1, y + 1, w - 2, h - 2);
   ofSetLineWidth(1);
}

void PatternMatrix::DrawSlotMidiOverlay(int trackIdx, int slot, float x, float y, float w, float h)
{
   bool hasBinding = false;
   for (const auto& b : mMidiBindings)
   {
      if (b.mSlot >= 0 && b.mTrack == trackIdx && b.mSlot == slot)
      {
         hasBinding = true;
         break;
      }
   }

   bool isPending = mPendingCellLearn && mLearnCellSlot >= 0 && mLearnTrack == trackIdx && mLearnCellSlot == slot;

   ofColor borderColor;
   float borderAlpha;
   if (isPending)
   {
      borderColor.set(255, 255, 0);
      borderAlpha = 220;
   }
   else if (hasBinding)
   {
      borderColor.set(0, 255, 100);
      borderAlpha = 200;
   }
   else
   {
      borderColor.set(0, 200, 255);
      borderAlpha = 180;
   }

   borderColor.a = borderAlpha;
   ofSetColor(borderColor);
   ofNoFill();
   ofSetLineWidth(2);
   ofRect(x + 1, y + 1, w - 2, h - 2);
   ofSetLineWidth(1);
}

void PatternMatrix::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mContextMenu)
   {
      mContextMenu->SetShowing(false);
      ContextAction action = (ContextAction)mContextAction;

      if (mContextTrack < 0 || mContextTrack >= mNumTracks)
      {
         mContextAction = (int)ContextAction::kNone;
         return;
      }

        switch (action)
        {
            case ContextAction::kStore:
               if (mContextSlot >= 0)
                  StorePattern(mContextTrack, mContextSlot);
               break;

           case ContextAction::kCopy:
             if (mContextSlot >= 0 && PatternHasData(mContextTrack, mContextSlot))
                PatternGetData(mContextTrack, mContextSlot, mClipboardData);
             break;

          case ContextAction::kPaste:
             if (mContextSlot >= 0 && !mClipboardData.empty() && PatternSetData(mContextTrack, mContextSlot, mClipboardData))
                mTracks[mContextTrack].mCurrentPattern = mContextSlot;
             break;

          case ContextAction::kClear:
             if (mContextSlot >= 0)
             {
                std::vector<char> empty;
                PatternSetData(mContextTrack, mContextSlot, empty);
                if (mTracks[mContextTrack].mCurrentPattern == mContextSlot)
                   mTracks[mContextTrack].mCurrentPattern = -1;
             }
             break;

          case ContextAction::kRemoveElement:
             if (mContextSlot < 0 && mContextCell != nullptr)
             {
                mContextCell->mTargetPath.clear();
                mContextCell->mTarget = nullptr;
                mContextCell->mValue = 0;
             }
             break;

           case ContextAction::kBindElement:
              if (mContextSlot < 0 && mContextCell != nullptr)
              {
                 float px, py;
                 mContextMenu->GetPosition(px, py, true);
                 mPendingBindX = px;
                 mPendingBindY = py;
                 mPendingBind = true;
              }
              break;

          default:
             break;
       }

       mContextAction = (int)ContextAction::kNone;
       return;
    }

    if (list == mParamBrowser)
    {
       mParamBrowser->SetShowing(false);
       if (mContextCell != nullptr && mParamBrowserSelection >= 0 && mParamBrowserSelection < (int)mParamBrowserPaths.size())
       {
          mContextCell->mTargetPath = mParamBrowserPaths[mParamBrowserSelection];
          mContextCell->mTarget = TheSynth->FindUIControl(mContextCell->mTargetPath);
       }
       mParamBrowserSelection = -1;
       mContextCell = nullptr;
       return;
    }

    if (list == mMidiControllerDropdown)
    {
       CancelCellLearn();
       auto names = TheSynth->GetModuleNames<MidiController*>();
       std::string controllerName;
       if (mMidiControllerDropdownSelection >= 0 && mMidiControllerDropdownSelection < (int)names.size())
          controllerName = names[mMidiControllerDropdownSelection];
       SetMidiController(controllerName);
       return;
    }

    int trackIdx = GetDropdownTrackIndex(list);
   if (trackIdx >= 0)
   {
      if (mTrackDropdownSelection[trackIdx] >= 0 && mTrackDropdownSelection[trackIdx] < (int)mModuleNames.size())
         mTracks[trackIdx].mModuleName = mModuleNames[mTrackDropdownSelection[trackIdx]];
      else
         mTracks[trackIdx].mModuleName.clear();
      ResolveModulePtr(trackIdx);
   }
}

void PatternMatrix::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (checkbox == mDockCheckbox)
   {
      SetShouldDrawOutline(!mDock);

      if (mDock && GetOwningContainer() == TheSynth->GetRootContainer())
      {
         TheSynth->GetUIContainer()->TakeModule(this);
         gHoveredUIControl = nullptr;
      }

      if (!mDock && GetOwningContainer() == TheSynth->GetUIContainer())
      {
         float w, h;
         GetModuleDimensions(w, h);
         TheSynth->GetRootContainer()->TakeModule(this);
         SetPosition(-TheSynth->GetDrawOffset().x, -TheSynth->GetDrawOffset().y + ofGetHeight() / GetOwningContainer()->GetDrawScale() - h);
         gHoveredUIControl = nullptr;
      }
   }
}

void PatternMatrix::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadInt("numtracks", moduleInfo, 4, 1, kMaxTracks, K(isTextField));
   mModuleSaveData.LoadInt("numslots", moduleInfo, 8, 1, kMaxSlots, K(isTextField));

   mModuleSaveData.LoadInt("rotarycols", moduleInfo, 0, 0, 8, K(isTextField));
   mModuleSaveData.LoadInt("rotaryrows", moduleInfo, 0, 0, 8, K(isTextField));
   mModuleSaveData.LoadInt("slidercols", moduleInfo, 0, 0, 8, K(isTextField));
   mModuleSaveData.LoadInt("sliderrows", moduleInfo, 0, 0, 8, K(isTextField));
   mModuleSaveData.LoadInt("buttoncols", moduleInfo, 0, 0, 8, K(isTextField));
   mModuleSaveData.LoadInt("buttonrows", moduleInfo, 0, 0, 8, K(isTextField));

   SetUpFromSaveData();
}

void PatternMatrix::SetUpFromSaveData()
{
   mNumTracks = mModuleSaveData.GetInt("numtracks");
   mNumSlots = mModuleSaveData.GetInt("numslots");

   mRotaryCols = mModuleSaveData.GetInt("rotarycols");
   mRotaryRows = mModuleSaveData.GetInt("rotaryrows");
   mSliderCols = mModuleSaveData.GetInt("slidercols");
   mSliderRows = mModuleSaveData.GetInt("sliderrows");
   mButtonCols = mModuleSaveData.GetInt("buttoncols");
   mButtonRows = mModuleSaveData.GetInt("buttonrows");

   RebuildElementGrids();
   ResolveElementBindings();
}

void PatternMatrix::SaveState(FileStreamOut& out)
{
      IDrawableModule::SaveState(out);

   out << mNumTracks;
   out << mNumSlots;
   for (int i = 0; i < kMaxTracks; ++i)
      out << mTracks[i].mModuleName;

   out << mRotaryCols << mRotaryRows;
   out << mSliderCols << mSliderRows;
   out << mButtonCols << mButtonRows;

   // save element bindings per track
   for (int i = 0; i < kMaxTracks; ++i)
   {
      for (int r = 0; r < (int)mTracks[i].mRotaryGrid.size(); ++r)
         for (int c = 0; c < (int)mTracks[i].mRotaryGrid[r].size(); ++c)
            out << mTracks[i].mRotaryGrid[r][c].mTargetPath << mTracks[i].mRotaryGrid[r][c].mValue;

      for (int r = 0; r < (int)mTracks[i].mSliderGrid.size(); ++r)
         for (int c = 0; c < (int)mTracks[i].mSliderGrid[r].size(); ++c)
            out << mTracks[i].mSliderGrid[r][c].mTargetPath << mTracks[i].mSliderGrid[r][c].mValue;

      for (int r = 0; r < (int)mTracks[i].mButtonGrid.size(); ++r)
         for (int c = 0; c < (int)mTracks[i].mButtonGrid[r].size(); ++c)
            out << mTracks[i].mButtonGrid[r][c].mTargetPath;
   }

    // MIDI mapping data (rev 4+)
    out << mMidiControllerName;
    int numBindings = (int)mMidiBindings.size();
    out << numBindings;
    for (const auto& b : mMidiBindings)
    {
       out << b.mControl;
       out << b.mChannel;
       out << b.mTrack;
       int cellType = (int)b.mCellType;
       out << cellType;
       out << b.mRow;
       out << b.mCol;
       out << b.mSlot; // rev 5+
    }
}

void PatternMatrix::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   LoadStateValidate(rev <= GetModuleSaveStateRev());

   in >> mNumTracks;
   in >> mNumSlots;
   for (int i = 0; i < kMaxTracks; ++i)
      in >> mTracks[i].mModuleName;

    if (rev >= 3)
    {
       in >> mRotaryCols >> mRotaryRows;
       in >> mSliderCols >> mSliderRows;
       in >> mButtonCols >> mButtonRows;
       RebuildElementGrids();

       for (int i = 0; i < kMaxTracks; ++i)
       {
          for (int r = 0; r < (int)mTracks[i].mRotaryGrid.size(); ++r)
             for (int c = 0; c < (int)mTracks[i].mRotaryGrid[r].size(); ++c)
                in >> mTracks[i].mRotaryGrid[r][c].mTargetPath >> mTracks[i].mRotaryGrid[r][c].mValue;

          for (int r = 0; r < (int)mTracks[i].mSliderGrid.size(); ++r)
             for (int c = 0; c < (int)mTracks[i].mSliderGrid[r].size(); ++c)
                in >> mTracks[i].mSliderGrid[r][c].mTargetPath >> mTracks[i].mSliderGrid[r][c].mValue;

          for (int r = 0; r < (int)mTracks[i].mButtonGrid.size(); ++r)
             for (int c = 0; c < (int)mTracks[i].mButtonGrid[r].size(); ++c)
                in >> mTracks[i].mButtonGrid[r][c].mTargetPath;
       }

       if (rev >= 4)
       {
          std::string controllerName;
          in >> controllerName;
          SetMidiController(controllerName);
          mMidiControllerDropdown->SetShowing(false);

          int numBindings;
          in >> numBindings;
           mMidiBindings.clear();
           for (int i = 0; i < numBindings; ++i)
           {
              MidiBinding b;
              int cellType;
              in >> b.mControl;
              in >> b.mChannel;
              in >> b.mTrack;
              in >> cellType;
              b.mCellType = (CellType)cellType;
              in >> b.mRow;
              in >> b.mCol;
              if (rev >= 5)
                 in >> b.mSlot;
              else
                 b.mSlot = -1;
              mMidiBindings.push_back(b);
           }
       }
    }

   for (int i = 0; i < kMaxTracks; ++i)
      ResolveModulePtr(i);

   ResolveElementBindings();
}

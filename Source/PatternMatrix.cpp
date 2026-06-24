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
     mParamBrowser->SetCableTargetable(false);
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

void PatternMatrix::RebuildElementGrids()
{
   mDragCell = nullptr;
   mContextCell = nullptr;
   mRotaryGrid.assign(mRotaryRows, std::vector<Cell>(mRotaryCols, MakeCell(CellType::kRotaryKnob)));
   mSliderGrid.assign(mSliderRows, std::vector<Cell>(mSliderCols, MakeCell(CellType::kSlider)));
   mButtonGrid.assign(mButtonRows, std::vector<Cell>(mButtonCols, MakeCell(CellType::kButton)));
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
   for (auto& row : mRotaryGrid)
      for (auto& cell : row)
         cell.mTarget = TheSynth->FindUIControl(cell.mTargetPath);
   for (auto& row : mSliderGrid)
      for (auto& cell : row)
         cell.mTarget = TheSynth->FindUIControl(cell.mTargetPath);
   for (auto& row : mButtonGrid)
      for (auto& cell : row)
         cell.mTarget = TheSynth->FindUIControl(cell.mTargetPath);
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
   const float kBottomH = 20;
   const float kPageTabW = 26;
   const float kPageTabH = 14;

   ofColor kDefaultPageColors[8] = {
      ofColor(200, 80, 80),    // red
      ofColor(80, 180, 80),    // green
      ofColor(80, 100, 220),   // blue
      ofColor(200, 180, 60),   // yellow
      ofColor(200, 80, 200),   // magenta
      ofColor(60, 200, 200),   // cyan
      ofColor(220, 140, 60),   // orange
      ofColor(150, 150, 150),  // gray
   };

   float GetGridX() { return kMargin + kSelW; }
   float GetHeaderStartY() { return kMargin + 2; }
   float GetFirstTrackY() { return GetHeaderStartY() + kHeaderH + kRowPad; }

   float GetTracksBottom(int numTracks)
   {
      return GetFirstTrackY() + numTracks * (kCellH + kRowPad);
   }

   float GetElementAreaStartX(int numSlots)
   {
      return GetGridX() + numSlots * kCellW + kMargin;
   }

   int GetElementGridRows(int rotaryRows, int sliderRows, int buttonRows)
   {
      return MAX(rotaryRows, MAX(sliderRows, buttonRows));
   }

   int GetElementTotalCols(int rotaryCols, int sliderCols, int buttonCols)
   {
      return rotaryCols + sliderCols + buttonCols;
   }

   float GetElementAreaWidth(int rotaryCols, int sliderCols, int buttonCols)
   {
      return GetElementTotalCols(rotaryCols, sliderCols, buttonCols) * (kElemW + 2);
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
   }

   if (Minimized() || IsVisible() == false)
      return;

   float gridX = GetGridX();
   float headerY = GetHeaderStartY();
   float firstTrackY = GetFirstTrackY();
   float elemX0 = GetElementAreaStartX(mNumSlots);
   int elemGridRows = GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows);
   int elemTotalCols = GetElementTotalCols(mRotaryCols, mSliderCols, mButtonCols);

   ofPushStyle();

   // column headers P1-Px and element type labels
   ofSetColor(50, 50, 50, gModuleDrawAlpha);
   ofFill();
   for (int c = 0; c < mNumSlots; ++c)
      ofRect(gridX + c * kCellW, headerY, kCellW, kHeaderH);
   if (elemTotalCols > 0)
      ofRect(elemX0, headerY, elemTotalCols * (kElemW + 2), kHeaderH);

   for (int c = 0; c < mNumSlots; ++c)
   {
      float x = gridX + c * kCellW;
      ofSetColor(60, 60, 60, gModuleDrawAlpha);
      ofNoFill();
      ofRect(x, headerY, kCellW, kHeaderH);
      ofSetColor(200, 200, 200, gModuleDrawAlpha);
      DrawTextNormal(("P" + ofToString(c + 1)).c_str(), x + kCellW / 2 - 7, headerY + 11, 10);
   }

   if (elemTotalCols > 0)
   {
      ofSetColor(60, 60, 60, gModuleDrawAlpha);
      ofNoFill();
      ofRect(elemX0, headerY, elemTotalCols * (kElemW + 2), kHeaderH);
      float ex = elemX0;
      if (mRotaryCols > 0)
      {
         ofSetColor(200, 200, 200, gModuleDrawAlpha);
         DrawTextNormal("R", ex + 3, headerY + 11, 10);
      }
      ex += mRotaryCols * (kElemW + 2);
      if (mSliderCols > 0)
      {
         ofSetColor(200, 200, 200, gModuleDrawAlpha);
         DrawTextNormal("S", ex + 3, headerY + 11, 10);
      }
      ex += mSliderCols * (kElemW + 2);
      if (mButtonCols > 0)
      {
         ofSetColor(200, 200, 200, gModuleDrawAlpha);
         DrawTextNormal("B", ex + 3, headerY + 11, 10);
      }
   }

   // tracks
   for (int i = 0; i < mNumTracks; ++i)
   {
      float y = firstTrackY + i * (kCellH + kRowPad);

      mTracks[i].mModuleSelector->SetPosition(kMargin, y);
      mTracks[i].mModuleSelector->SetWidth(kSelW);
      mTracks[i].mModuleSelector->Draw();

      for (int c = 0; c < mNumSlots; ++c)
      {
         float x = gridX + c * kCellW;
         bool hasData = PatternHasData(i, c);

         ofColor pageCol = mPages[mCurrentPage].mColor;
         ofColor color;
         if (mTracks[i].mCurrentPattern == c)
            color = pageCol;
         else if (hasData)
            color = pageCol * 0.5f;
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
   }

   // standalone element grids (right side)
   if (elemGridRows > 0)
   {
      float elemY = firstTrackY;

      // rotary grid
      for (int r = 0; r < mRotaryRows && r < (int)mRotaryGrid.size(); ++r)
      {
         for (int c = 0; c < mRotaryCols && c < (int)mRotaryGrid[r].size(); ++c)
         {
            float ex = elemX0 + c * (kElemW + 2);
            float ey = elemY + r * (kElemH + kRowPad);
            DrawRotaryKnob(ex + kElemW / 2, ey + kElemH / 2, kElemW / 2 - 3, mRotaryGrid[r][c].mValue, mRotaryGrid[r][c].mMin, mRotaryGrid[r][c].mMax);
         }
      }

      // slider grid (right of rotary)
      float sliderX0 = elemX0 + mRotaryCols * (kElemW + 2);
      for (int r = 0; r < mSliderRows && r < (int)mSliderGrid.size(); ++r)
      {
         for (int c = 0; c < mSliderCols && c < (int)mSliderGrid[r].size(); ++c)
         {
            float ex = sliderX0 + c * (kElemW + 2);
            float ey = elemY + r * (kElemH + kRowPad);
            DrawSlider(ex, ey, kElemW, kElemH, mSliderGrid[r][c].mValue, mSliderGrid[r][c].mMin, mSliderGrid[r][c].mMax);
         }
      }

      // button grid (right of slider)
      float btnX0 = sliderX0 + mSliderCols * (kElemW + 2);
      for (int r = 0; r < mButtonRows && r < (int)mButtonGrid.size(); ++r)
      {
         for (int c = 0; c < mButtonCols && c < (int)mButtonGrid[r].size(); ++c)
         {
            float ex = btnX0 + c * (kElemW + 2);
            float ey = elemY + r * (kElemH + kRowPad);
            DrawButton(ex, ey, kElemW, kElemH);
         }
      }
   }

   // bottom bar
   float actualW, actualH;
   GetModuleDimensions(actualW, actualH);
   float tracksContentH = mNumTracks * (kCellH + kRowPad);
   float elemContentH = elemGridRows * (kElemH + kRowPad);
   float contentH = MAX(tracksContentH, elemContentH);
   float bottomY = firstTrackY + contentH + kMargin;
   ofSetColor(40, 40, 40, gModuleDrawAlpha);
   ofFill();
   ofRect(kMargin, bottomY, actualW - kMargin * 2, kBottomH);
   mDockCheckbox->SetShowing(GetOwningContainer() == TheSynth->GetRootContainer() || GetOwningContainer() == TheSynth->GetUIContainer());
   mDockCheckbox->SetPosition(kMargin + 2, bottomY + 2);
   mDockCheckbox->Draw();

   mEditMapButton->SetPosition(kMargin + 60, bottomY + 2);
   mEditMapButton->Draw();

   // page tabs
   float pageTabX = kMargin + 128;
   ofSetColor(60, 60, 60, gModuleDrawAlpha);
   ofFill();
   ofRect(pageTabX - 2, bottomY + 1, 1, kBottomH - 2);
   float tabY = bottomY + 3;
   float tabH = kPageTabH;
   for (int p = 0; p < mNumPages; ++p)
   {
      ofColor col = mPages[p].mColor;
      col.a = gModuleDrawAlpha;
      ofSetColor(col);
      ofFill();
      ofRect(pageTabX, tabY, kPageTabW, tabH);
      ofSetColor(p == mCurrentPage ? ofColor::white : ofColor(200, 200, 200), gModuleDrawAlpha);
      DrawTextNormal(ofToString(p + 1).c_str(), pageTabX + kPageTabW / 2 - 4, tabY + 10, 9);
      pageTabX += kPageTabW + 2;
   }

   // "+" button to add a page
   if (mNumPages < kMaxPages)
   {
      ofSetColor(80, 80, 80, gModuleDrawAlpha);
      ofFill();
      ofRect(pageTabX, tabY, 16, tabH);
      ofSetColor(200, 200, 200, gModuleDrawAlpha);
      DrawTextNormal("+", pageTabX + 5, tabY + 10, 9);
   }

   if (mEditMapMode)
   {
      mMidiControllerDropdown->SetPosition(pageTabX + 24, bottomY + 2);
      mMidiControllerDropdown->Draw();
   }

   // MIDI mapping overlay
   if (mEditMapMode)
   {
      for (int c = 0; c < mNumSlots; ++c)
      {
         float x = gridX + c * kCellW;
         DrawSlotMidiOverlay(-1, c, x, headerY, kCellW, kHeaderH);
      }

      for (int i = 0; i < mNumTracks; ++i)
      {
         float y = firstTrackY + i * (kCellH + kRowPad);
         for (int c = 0; c < mNumSlots; ++c)
         {
            float sx = gridX + c * kCellW;
            DrawSlotMidiOverlay(i, c, sx, y, kCellW, kCellH);
         }
      }

      // standalone element MIDI overlay
      if (elemGridRows > 0)
      {
         float elemY = firstTrackY;

         for (int r = 0; r < mRotaryRows && r < (int)mRotaryGrid.size(); ++r)
            for (int c = 0; c < mRotaryCols && c < (int)mRotaryGrid[r].size(); ++c)
               DrawMidiOverlay(-1, CellType::kRotaryKnob, r, c, elemX0 + c * (kElemW + 2), elemY + r * (kElemH + kRowPad), kElemW, kElemH);

         float sliderX0 = elemX0 + mRotaryCols * (kElemW + 2);
         for (int r = 0; r < mSliderRows && r < (int)mSliderGrid.size(); ++r)
            for (int c = 0; c < mSliderCols && c < (int)mSliderGrid[r].size(); ++c)
               DrawMidiOverlay(-1, CellType::kSlider, r, c, sliderX0 + c * (kElemW + 2), elemY + r * (kElemH + kRowPad), kElemW, kElemH);

         float btnX0 = sliderX0 + mSliderCols * (kElemW + 2);
         for (int r = 0; r < mButtonRows && r < (int)mButtonGrid.size(); ++r)
            for (int c = 0; c < mButtonCols && c < (int)mButtonGrid[r].size(); ++c)
               DrawMidiOverlay(-1, CellType::kButton, r, c, btnX0 + c * (kElemW + 2), elemY + r * (kElemH + kRowPad), kElemW, kElemH);
      }
   }

   ofPopStyle();
}

void PatternMatrix::DrawRotaryKnob(float cx, float cy, float radius, float val, float min, float max)
{
   float norm = 0;
   if (max != min)
      norm = (val - min) / (max - min);
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
   float norm = 0;
   if (max != min)
      norm = (val - min) / (max - min);
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
   width = kMargin + kSelW + mNumSlots * kCellW + kMargin;
   int elemCols = GetElementTotalCols(mRotaryCols, mSliderCols, mButtonCols);
   if (elemCols > 0)
      width += GetElementAreaWidth(mRotaryCols, mSliderCols, mButtonCols) + kMargin;

   // ensure minimum width for page tabs in the bottom bar
   float minPageWidth = kMargin + 130 + mNumPages * (kPageTabW + 2) + 22 + kMargin;
   if (mNumPages > 1 || minPageWidth > width)
      width = MAX(width, minPageWidth);

   int elemGridRows = GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows);
   float tracksContentH = mNumTracks * (kCellH + kRowPad);
   float elemContentH = elemGridRows * (kElemH + kRowPad);
   float contentH = MAX(tracksContentH, elemContentH);
   height = GetFirstTrackY() + contentH + kMargin + kBottomH;

   if (mDock && GetOwningContainer())
      width = MAX(width, ofGetWidth() / GetOwningContainer()->GetDrawScale());
}

void PatternMatrix::Resize(float w, float h)
{
}

// --- click handling ---

bool PatternMatrix::HitTestElementGrid(float px, float py, Cell*& outCell, int* outRow, int* outCol)
{
   float elemX0 = GetElementAreaStartX(mNumSlots);
   float firstTrackY = GetFirstTrackY();

   // check rotary grid
   for (int r = 0; r < mRotaryRows && r < (int)mRotaryGrid.size(); ++r)
   {
      for (int c = 0; c < mRotaryCols && c < (int)mRotaryGrid[r].size(); ++c)
      {
         float ex = elemX0 + c * (kElemW + 2);
         float ey = firstTrackY + r * (kElemH + kRowPad);
         if (px >= ex && px < ex + kElemW && py >= ey && py < ey + kElemH)
         {
            outCell = &mRotaryGrid[r][c];
            if (outRow) *outRow = r;
            if (outCol) *outCol = c;
            return true;
         }
      }
   }

   // check slider grid
   float sliderX0 = elemX0 + mRotaryCols * (kElemW + 2);
   for (int r = 0; r < mSliderRows && r < (int)mSliderGrid.size(); ++r)
   {
      for (int c = 0; c < mSliderCols && c < (int)mSliderGrid[r].size(); ++c)
      {
         float ex = sliderX0 + c * (kElemW + 2);
         float ey = firstTrackY + r * (kElemH + kRowPad);
         if (px >= ex && px < ex + kElemW && py >= ey && py < ey + kElemH)
         {
            outCell = &mSliderGrid[r][c];
            if (outRow) *outRow = r;
            if (outCol) *outCol = c;
            return true;
         }
      }
   }

   // check button grid
   float btnX0 = sliderX0 + mSliderCols * (kElemW + 2);
   for (int r = 0; r < mButtonRows && r < (int)mButtonGrid.size(); ++r)
   {
      for (int c = 0; c < mButtonCols && c < (int)mButtonGrid[r].size(); ++c)
      {
         float ex = btnX0 + c * (kElemW + 2);
         float ey = firstTrackY + r * (kElemH + kRowPad);
         if (px >= ex && px < ex + kElemW && py >= ey && py < ey + kElemH)
         {
            outCell = &mButtonGrid[r][c];
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
   float firstTrackY = GetFirstTrackY();
   float tracksContentH = mNumTracks * (kCellH + kRowPad);
   int elemGridRows = GetElementGridRows(mRotaryRows, mSliderRows, mButtonRows);
   float elemContentH = elemGridRows * (kElemH + kRowPad);
   float contentH = MAX(tracksContentH, elemContentH);
   float contentBottom = firstTrackY + contentH;

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

   // cancel cell learn in edit mode
   if (mEditMapMode && mPendingCellLearn)
   {
      bool hitSomething = (y >= headerY && y < contentBottom);
      if (!hitSomething)
         CancelCellLearn();
   }

   // track rows
   for (int i = 0; i < mNumTracks; ++i)
   {
      float ty = firstTrackY + i * (kCellH + kRowPad);

      if (y < ty || y >= ty + kCellH + kRowPad)
         continue;

      for (int c = 0; c < mNumSlots; ++c)
      {
         float colX = gridX + c * kCellW;
         if (x >= colX && x < colX + kCellW)
         {
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
               if (mTracks[i].mCurrentPattern == c)
               {
                  mContextTrack = i;
                  mContextSlot = c;
                  mContextMenu->SetPosition(x + kCellW, y);
                  mContextMenu->SetShowing(true);
                  mContextMenu->TestClick(x + kCellW, y, false);
               }
            }
            else
            {
               if (PatternHasData(i, c))
                  LoadPattern(i, c);
               else
                  StorePattern(i, c);
            }
            return;
         }
      }
   }

   // bottom bar clicks (page tabs, + button)
   float botY = firstTrackY + contentH + kMargin;
   if (y >= botY && y < botY + kBottomH)
   {
      float pageTabX = kMargin + 128;
      float tabY = botY + 3;
      for (int p = 0; p < mNumPages; ++p)
      {
         if (x >= pageTabX && x < pageTabX + kPageTabW && y >= tabY && y < tabY + kPageTabH)
         {
            if (p != mCurrentPage)
               SwitchPage(p);
            return;
         }
         pageTabX += kPageTabW + 2;
      }
      // "+" button
      if (mNumPages < kMaxPages && x >= pageTabX && x < pageTabX + 16 && y >= tabY && y < tabY + kPageTabH)
      {
         SnapshotPage(mCurrentPage);
         mNumPages++;
         while ((int)mPages.size() < mNumPages)
         {
            PageData page;
            int idx = (int)mPages.size();
            page.mColor = kDefaultPageColors[idx % 8];
            mPages.push_back(page);
         }
         return;
      }
      return;
   }

   // standalone element grid area
   Cell* hitCell = nullptr;
   int hitRow = -1, hitCol = -1;
   if (HitTestElementGrid(x, y, hitCell, &hitRow, &hitCol))
   {
      if (mEditMapMode && mMidiController && !right)
      {
         CancelCellLearn();
         mPendingCellLearn = true;
         mLearnTrack = -1;
         mLearnCellSlot = -1;
         mLearnCellType = hitCell->mType;
         mLearnCellRow = hitRow;
         mLearnCellCol = hitCol;
         return;
      }

      if (right)
      {
         mContextTrack = -1;
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

bool PatternMatrix::MouseMoved(float x, float y)
{
   if (mDragCell == nullptr)
      return false;

   float dy = mDragStartY - y;
   float range = mDragCell->mMax - mDragCell->mMin;
   if (range == 0)
      range = 1;
   float delta = dy / kDragSensitivity * range;
   mDragCell->mValue = ofClamp(mDragStartVal + delta, mDragCell->mMin, mDragCell->mMax);

   if (mDragCell->mTarget)
      mDragCell->mTarget->SetValue(mDragCell->mValue, gTime);

   return true;
}

void PatternMatrix::MouseReleased()
{
   mDragCell = nullptr;
}

// --- pattern operations ---

void PatternMatrix::StorePattern(int trackIdx, int slot)
{
   if (trackIdx < 0 || trackIdx >= mNumTracks || slot < 0 || slot >= mNumSlots)
      return;
   if (PatternSave(trackIdx, slot))
      mTracks[trackIdx].mCurrentPattern = slot;
}

void PatternMatrix::LoadPattern(int trackIdx, int slot)
{
   if (trackIdx < 0 || trackIdx >= mNumTracks || slot < 0 || slot >= mNumSlots)
      return;
   if (PatternLoad(trackIdx, slot))
      mTracks[trackIdx].mCurrentPattern = slot;
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

   // Element grid cell (standalone global grid, mTrack == -1)
   Cell* cell = nullptr;
   if (binding.mTrack >= 0)
   {
      // Per-track element bindings no longer supported in rev 6+
      return;
   }
   else
   {
      switch (binding.mCellType)
      {
         case CellType::kRotaryKnob:
            if (binding.mRow < (int)mRotaryGrid.size() && binding.mCol < (int)mRotaryGrid[binding.mRow].size())
               cell = &mRotaryGrid[binding.mRow][binding.mCol];
            break;
         case CellType::kSlider:
            if (binding.mRow < (int)mSliderGrid.size() && binding.mCol < (int)mSliderGrid[binding.mRow].size())
               cell = &mSliderGrid[binding.mRow][binding.mCol];
            break;
         case CellType::kButton:
            if (binding.mRow < (int)mButtonGrid.size() && binding.mCol < (int)mButtonGrid[binding.mRow].size())
               cell = &mButtonGrid[binding.mRow][binding.mCol];
            break;
      }
   }

   if (cell)
   {
      if (cell->mType == CellType::kButton && cell->mTarget)
      {
         if (value > 0.5f)
            cell->mTarget->SetValue(cell->mTarget->GetValue() > 0 ? 0 : 1, gTime);
      }
      else
      {
         cell->mValue = ofClamp(value, cell->mMin, cell->mMax);
         if (cell->mTarget)
            cell->mTarget->SetValue(cell->mValue, gTime);
      }
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
         // Allow element cell actions (remove/bind) even without a valid track
         if (action != ContextAction::kRemoveElement && action != ContextAction::kBindElement)
         {
            mContextAction = (int)ContextAction::kNone;
            return;
         }
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

// --- page management ---

void PatternMatrix::InitPages()
{
   if (!mPages.empty())
      return;

   mPages.clear();
   for (int i = 0; i < mNumPages; ++i)
   {
      PageData page;
      page.mColor = kDefaultPageColors[i % 8];
      mPages.push_back(page);
   }
   mCurrentPage = 0;
}

void PatternMatrix::SnapshotPage(int pageIdx)
{
   if (pageIdx < 0 || pageIdx >= (int)mPages.size())
      return;

   PageData& page = mPages[pageIdx];
   for (int i = 0; i < mNumTracks; ++i)
   {
      page.mCurrentPattern[i] = mTracks[i].mCurrentPattern;
      for (int s = 0; s < mNumSlots; ++s)
      {
         std::vector<char> blob;
         if (PatternGetData(i, s, blob))
            page.mPatternData[i][s] = std::move(blob);
         else
            page.mPatternData[i][s].clear();
      }
   }
   // clear unused tracks
   for (int i = mNumTracks; i < kMaxTracks; ++i)
   {
      page.mCurrentPattern[i] = -1;
      for (int s = 0; s < kMaxSlots; ++s)
         page.mPatternData[i][s].clear();
   }
}

void PatternMatrix::RestorePage(int pageIdx)
{
   if (pageIdx < 0 || pageIdx >= (int)mPages.size())
      return;

   PageData& page = mPages[pageIdx];
   for (int i = 0; i < mNumTracks; ++i)
   {
      for (int s = 0; s < mNumSlots; ++s)
      {
         if (!page.mPatternData[i][s].empty())
            PatternSetData(i, s, page.mPatternData[i][s]);
         else
         {
            std::vector<char> empty;
            PatternSetData(i, s, empty);
         }
      }
      mTracks[i].mCurrentPattern = -1;
      if (page.mCurrentPattern[i] >= 0 && page.mCurrentPattern[i] < mNumSlots)
      {
         if (!page.mPatternData[i][page.mCurrentPattern[i]].empty())
         {
            mTracks[i].mCurrentPattern = page.mCurrentPattern[i];
            PatternLoad(i, page.mCurrentPattern[i]);
         }
      }
   }
}

void PatternMatrix::SwitchPage(int pageIdx)
{
   if (pageIdx < 0 || pageIdx >= (int)mPages.size() || pageIdx == mCurrentPage)
      return;

   SnapshotPage(mCurrentPage);
   mCurrentPage = pageIdx;
   RestorePage(pageIdx);
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
   mModuleSaveData.LoadInt("numpages", moduleInfo, 1, 1, kMaxPages, K(isTextField));

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
   mNumPages = mModuleSaveData.GetInt("numpages");

   mRotaryCols = mModuleSaveData.GetInt("rotarycols");
   mRotaryRows = mModuleSaveData.GetInt("rotaryrows");
   mSliderCols = mModuleSaveData.GetInt("slidercols");
   mSliderRows = mModuleSaveData.GetInt("sliderrows");
   mButtonCols = mModuleSaveData.GetInt("buttoncols");
   mButtonRows = mModuleSaveData.GetInt("buttonrows");

   RebuildElementGrids();
   ResolveElementBindings();
   InitPages();
}

void PatternMatrix::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);

   out << mNumTracks;
   out << mNumSlots;

   // rev 7+ : save current page state snapshot first
   SnapshotPage(mCurrentPage);

   // track names
   for (int i = 0; i < kMaxTracks; ++i)
      out << mTracks[i].mModuleName;

   out << mRotaryCols << mRotaryRows;
   out << mSliderCols << mSliderRows;
   out << mButtonCols << mButtonRows;

   // rev 6+ : save standalone global element bindings
   for (int r = 0; r < (int)mRotaryGrid.size(); ++r)
      for (int c = 0; c < (int)mRotaryGrid[r].size(); ++c)
         out << mRotaryGrid[r][c].mTargetPath << mRotaryGrid[r][c].mValue;

   for (int r = 0; r < (int)mSliderGrid.size(); ++r)
      for (int c = 0; c < (int)mSliderGrid[r].size(); ++c)
         out << mSliderGrid[r][c].mTargetPath << mSliderGrid[r][c].mValue;

   for (int r = 0; r < (int)mButtonGrid.size(); ++r)
      for (int c = 0; c < (int)mButtonGrid[r].size(); ++c)
         out << mButtonGrid[r][c].mTargetPath;

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

   // rev 7+ : page data
   out << mNumPages;
   out << mCurrentPage;
   for (int p = 0; p < mNumPages; ++p)
   {
      const auto& page = mPages[p];
      for (int i = 0; i < kMaxTracks; ++i)
      {
         out << page.mCurrentPattern[i];
         for (int s = 0; s < kMaxSlots; ++s)
         {
            bool hasData = !page.mPatternData[i][s].empty();
            out << hasData;
            if (hasData)
            {
               int size = (int)page.mPatternData[i][s].size();
               out << size;
               out.WriteGeneric(page.mPatternData[i][s].data(), size);
            }
         }
      }
      out << page.mColor.r << page.mColor.g << page.mColor.b;
   }
}

void PatternMatrix::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   LoadStateValidate(rev <= GetModuleSaveStateRev());

   in >> mNumTracks;
   in >> mNumSlots;
   mNumTracks = ofClamp(mNumTracks, 1, kMaxTracks);
   mNumSlots = ofClamp(mNumSlots, 1, kMaxSlots);
   for (int i = 0; i < kMaxTracks; ++i)
      in >> mTracks[i].mModuleName;

   if (rev >= 3)
   {
      in >> mRotaryCols >> mRotaryRows;
      in >> mSliderCols >> mSliderRows;
      in >> mButtonCols >> mButtonRows;
      RebuildElementGrids();

      if (rev >= 6)
      {
         // rev 6+ : load standalone global element bindings
         for (int r = 0; r < (int)mRotaryGrid.size(); ++r)
            for (int c = 0; c < (int)mRotaryGrid[r].size(); ++c)
               in >> mRotaryGrid[r][c].mTargetPath >> mRotaryGrid[r][c].mValue;

         for (int r = 0; r < (int)mSliderGrid.size(); ++r)
            for (int c = 0; c < (int)mSliderGrid[r].size(); ++c)
               in >> mSliderGrid[r][c].mTargetPath >> mSliderGrid[r][c].mValue;

         for (int r = 0; r < (int)mButtonGrid.size(); ++r)
            for (int c = 0; c < (int)mButtonGrid[r].size(); ++c)
               in >> mButtonGrid[r][c].mTargetPath;
      }
      else
      {
         // rev 3-5 : skip per-track element data
         for (int i = 0; i < kMaxTracks; ++i)
         {
            for (int r = 0; r < (int)mRotaryGrid.size(); ++r)
               for (int c = 0; c < (int)mRotaryGrid[r].size(); ++c)
               {
                  std::string path;
                  float val;
                  in >> path >> val;
               }

            for (int r = 0; r < (int)mSliderGrid.size(); ++r)
               for (int c = 0; c < (int)mSliderGrid[r].size(); ++c)
               {
                  std::string path;
                  float val;
                  in >> path >> val;
               }

            for (int r = 0; r < (int)mButtonGrid.size(); ++r)
               for (int c = 0; c < (int)mButtonGrid[r].size(); ++c)
               {
                  std::string path;
                  in >> path;
               }
         }
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

   if (rev >= 7)
   {
      // load pages
      int numPages, currentPage;
      in >> numPages;
      in >> currentPage;
      mNumPages = ofClamp(numPages, 1, kMaxPages);
      mPages.clear();
      for (int p = 0; p < mNumPages; ++p)
      {
         PageData page;
         for (int i = 0; i < kMaxTracks; ++i)
         {
            in >> page.mCurrentPattern[i];
            for (int s = 0; s < kMaxSlots; ++s)
            {
               bool hasData;
               in >> hasData;
               if (hasData)
               {
                  int size;
                  in >> size;
                  page.mPatternData[i][s].resize(size);
                  in.ReadGeneric(page.mPatternData[i][s].data(), size);
               }
               else
               {
                  page.mPatternData[i][s].clear();
               }
            }
         }
         in >> page.mColor.r >> page.mColor.g >> page.mColor.b;
         mPages.push_back(page);
      }
      mCurrentPage = ofClamp(currentPage, 0, mNumPages - 1);
      RestorePage(mCurrentPage);
   }
   else
   {
      // rev < 7 : init single default page from current state
      InitPages();
      SnapshotPage(0);
   }

   for (int i = 0; i < kMaxTracks; ++i)
      ResolveModulePtr(i);

   ResolveElementBindings();
}

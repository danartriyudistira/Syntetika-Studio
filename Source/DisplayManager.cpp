#include "DisplayManager.h"
#include "VisualFBO.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "OpenFrameworksPort.h"
#include "PatchCable.h"
#include "ModuleSaveDataPanel.h"

#include <algorithm>

DisplayManager::DisplayManager()
{
}

DisplayManager::~DisplayManager()
{
   delete mOutputFBO;
}

void DisplayManager::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mRowsDropdown = new DropdownList(this, "rows", 3, 2, &mGridRows, 40);
   for (int i = 1; i <= 8; ++i)
      mRowsDropdown->AddLabel(std::to_string(i), i);
   AddUIControl(mRowsDropdown);

   mColsDropdown = new DropdownList(this, "cols", 48, 2, &mGridCols, 40);
   for (int i = 1; i <= 8; ++i)
      mColsDropdown->AddLabel(std::to_string(i), i);
   AddUIControl(mColsDropdown);

   mApplyButton = new ClickButton(this, "apply", 92, 2);
   AddUIControl(mApplyButton);

   mRootNoteDropdown = new DropdownList(this, "rootnote", 135, 2, &mRootNote, 55);
   for (int i = 0; i <= 127; ++i)
      mRootNoteDropdown->AddLabel(NoteName(i, false, true), i);
   AddUIControl(mRootNoteDropdown);

   mChannelDropdown = new DropdownList(this, "channel", 195, 2, &mMidiChannel, 40);
   mChannelDropdown->AddLabel("All", -1);
   for (int i = 1; i <= 16; ++i)
      mChannelDropdown->AddLabel(std::to_string(i), i);
   AddUIControl(mChannelDropdown);

   mAutoSwitchCheckbox = new Checkbox(this, "autoswitch", 240, 2, &mAutoSwitch);
   AddUIControl(mAutoSwitchCheckbox);

   mPriorityDropdown = new DropdownList(this, "priority", 315, 2, &mEditingPriority, 55);
   for (int i = 0; i <= 9; ++i)
      mPriorityDropdown->AddLabel(std::to_string(i), i);
   AddUIControl(mPriorityDropdown);

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mModuleWidth - 15, 10);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);

   ApplyGridSize();
}

void DisplayManager::GetModuleDimensions(float& width, float& height)
{
   width = mModuleWidth;
   height = mModuleHeight;
}

void DisplayManager::DrawModule()
{
   if (Minimized() || !mEnabled)
      return;

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   ResolveSources();

   float headerH = 20;

   ofPushStyle();
   ofSetColor(40, 40, 60);
   ofRect(0, 0, mModuleWidth, mModuleHeight);
   ofPopStyle();

   ofPushStyle();
   ofSetColor(50, 50, 72);
   ofRect(0, 0, mModuleWidth, headerH);
   ofPopStyle();

   mRowsDropdown->Draw();
   mColsDropdown->Draw();
   mApplyButton->Draw();
   mRootNoteDropdown->Draw();
   mChannelDropdown->Draw();
   mAutoSwitchCheckbox->Draw();
   if (mActiveCell >= 0 && mActiveCell < (int)mCellPriority.size())
   {
      mEditingPriority = mCellPriority[mActiveCell];
      mPriorityDropdown->Draw();
   }

   ofPushStyle();
   ofSetColor(200, 200, 200);
   DrawTextNormal("Display Manager", 3, (int)mModuleHeight - 12, 12);

   int infoY = (int)mModuleHeight - 24;
   std::string gridText = std::to_string(mGridRows) + "x" + std::to_string(mGridCols);
   ofSetColor(100, 200, 100);
   DrawTextNormal(gridText, 3, infoY, 11);

   if (mActiveCell >= 0)
   {
      ofSetColor(100, 255, 255);
      DrawTextNormal("active: " + std::to_string(mActiveCell + 1), 60, infoY, 11);
   }

   ofSetColor(180, 180, 100);
   DrawTextNormal(std::string("Rt:") + NoteName(mRootNote, false, true), 120, infoY, 11);
   ofSetColor(180, 180, 100);
   std::string chStr = (mMidiChannel < 0) ? "All" : std::to_string(mMidiChannel);
   DrawTextNormal(std::string("Ch:") + chStr, 190, infoY, 11);
   ofPopStyle();

   AutoSelect();

   float contentTop = headerH + 3;
   float contentH = mModuleHeight - contentTop - 3;
   if (contentH <= 0) contentH = 1;

   int cols = mGridCols;
   int rows = mGridRows;
   int numCells = cols * rows;

   float padding = 2;
   float cellW = (mModuleWidth - padding * (cols + 1)) / cols;
   float cellH = (contentH - padding * (rows + 1)) / rows;
   if (cellW <= 0) cellW = 1;
   if (cellH <= 0) cellH = 1;

   for (int i = 0; i < numCells; ++i)
   {
      int col = i % cols;
      int row = i / cols;
      float x = padding + col * (cellW + padding);
      float y = contentTop + row * (cellH + padding);

      ofPushStyle();
      ofSetColor(30, 30, 45);
      ofRect(x, y, cellW, cellH);
      ofPopStyle();

      if (i < (int)mSources.size() && mSources[i])
      {
         auto* fbo = mSources[i]->GetFBO();
         if (fbo && fbo->IsValid())
         {
            fbo->ReleaseDisplayImage();
            fbo->Draw(x, y, cellW, cellH);
         }
      }
      else
      {
         ofPushStyle();
         ofSetColor(80, 80, 80);
         float r = std::min(cellW, cellH) * 0.15f;
         ofCircle(x + cellW / 2, y + cellH / 2, r);
         ofPopStyle();
      }

      if (i < (int)mCellHeld.size() && mCellHeld[i])
      {
         ofPushStyle();
         ofSetColor(60, 200, 60, 60);
         ofRect(x, y, cellW, cellH);
         ofPopStyle();
      }

      if (i == mActiveCell)
      {
         ofPushStyle();
         ofNoFill();
         ofSetColor(0, 255, 255);
         ofSetLineWidth(3);
         ofRect(x, y, cellW, cellH);
         ofSetLineWidth(1);
         ofFill();
         ofPopStyle();
      }

      ofPushStyle();
      ofSetColor(40, 40, 60, 200);
      ofRect(x + 2, y + 2, 16, 12);
      ofSetColor(200, 200, 200, 220);
      DrawTextNormal(std::to_string(i + 1), x + 4, y + 12, 10);
      ofPopStyle();

      if (i < (int)mCellPriority.size() && mCellPriority[i] > 0)
      {
         ofPushStyle();
         ofSetColor(255, 200, 50, 220);
         DrawTextNormal("P" + std::to_string(mCellPriority[i]), x + cellW - 20, y + 12, 10);
         ofPopStyle();
      }
   }

   ofPushStyle();
   ofSetColor(120, 120, 140, 200);
   for (size_t i = 0; i < mInputCables.size(); ++i)
   {
      if (mInputCables[i])
      {
         float labelY = 25 + i * 18;
         ofRect(1, labelY - 1, 14, 10);
         ofSetColor(220, 220, 220, 220);
         DrawTextNormal(std::to_string((int)i + 1), 3, labelY + 8, 9);
         ofSetColor(120, 120, 140, 200);
      }
   }
   ofPopStyle();
}

void DisplayManager::PostRender()
{
   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   if (!mEnabled || mActiveCell < 0 || mActiveCell >= (int)mSources.size())
      return;

   IVisualSource* src = mSources[mActiveCell];
   if (!src)
      return;

   VisualFBO* srcFBO = src->GetFBO();
   if (!srcFBO || !srcFBO->IsValid())
      return;

   int w = srcFBO->GetWidth();
   int h = srcFBO->GetHeight();
   if (w <= 0 || h <= 0)
      return;

   if (!mOutputFBO)
      mOutputFBO = new VisualFBO();
   if (mOutputFBO->GetWidth() != w || mOutputFBO->GetHeight() != h)
      mOutputFBO->Create(std::max(64, w), std::max(64, h));

   mOutputFBO->Bind();

   srcFBO->ReleaseDisplayImage();
   srcFBO->Draw(0, 0, (float)w, (float)h);

   mOutputFBO->Unbind();
}

void DisplayManager::PostRepatch(PatchCableSource* source, bool fromUserClick)
{
   for (size_t i = 0; i < mInputCables.size(); ++i)
   {
      if (source == mInputCables[i])
      {
         if (!mInputCables[i]->GetPatchCables().empty())
         {
            auto* target = mInputCables[i]->GetPatchCables()[0]->GetTarget();
            mSources[i] = dynamic_cast<IVisualSource*>(target);
         }
         else
         {
            mSources[i] = nullptr;
         }
         break;
      }
   }
}

void DisplayManager::ButtonClicked(ClickButton* button, double time)
{
   if (button == mApplyButton)
      ApplyGridSize();
}

void DisplayManager::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mRootNoteDropdown || list == mChannelDropdown)
   {
   }
   if (list == mPriorityDropdown)
   {
      if (mActiveCell >= 0 && mActiveCell < (int)mCellPriority.size())
         mCellPriority[mActiveCell] = mEditingPriority;
   }
}

void DisplayManager::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled)
      return;

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   int channel = voiceIdx + 1;
   if (mMidiChannel >= 0 && channel != mMidiChannel)
      return;

   int cellIndex = pitch - mRootNote;
   int numCells = mGridRows * mGridCols;
   if (cellIndex < 0 || cellIndex >= numCells)
      return;

   if (velocity > 0)
   {
      mNoteVelocity = velocity / 127.0f;
      if (mAutoSwitch)
      {
         if (cellIndex < (int)mCellHeld.size())
            mCellHeld[cellIndex] = true;
         AutoSelect();
      }
      else
      {
         mActiveCell = cellIndex;
      }
   }
   else
   {
      if (cellIndex < (int)mCellHeld.size())
         mCellHeld[cellIndex] = false;
      if (mAutoSwitch)
      {
         AutoSelect();
      }
      else
      {
         if (mActiveCell == cellIndex)
            mActiveCell = -1;
      }
      bool anyHeld = false;
      for (bool h : mCellHeld)
      {
         if (h) { anyHeld = true; break; }
      }
      if (!anyHeld)
         mNoteVelocity = 0;
   }
}

void DisplayManager::OnPulse(double time, float velocity, int flags)
{
   if (!mEnabled)
      return;

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   int numCells = mGridRows * mGridCols;
   if (numCells == 0)
      return;

   if (mAutoSwitch)
   {
      int start = mActiveCell >= 0 ? mActiveCell : 0;
      int bestIdx = -1;
      int bestPriority = -1;
      for (int step = 1; step <= numCells; ++step)
      {
         int idx = (start + step * mPulseAdvanceDir + numCells) % numCells;
         if (idx < (int)mCellHeld.size() && mCellHeld[idx])
         {
            int pri = idx < (int)mCellPriority.size() ? mCellPriority[idx] : 0;
            if (pri > bestPriority)
            {
               bestPriority = pri;
               bestIdx = idx;
            }
         }
      }
      if (bestIdx >= 0)
         mActiveCell = bestIdx;
      else
         mActiveCell = (mActiveCell + mPulseAdvanceDir + numCells) % numCells;
   }
   else
   {
      int next = (mActiveCell + mPulseAdvanceDir + numCells) % numCells;
      mActiveCell = next;
   }
}

void DisplayManager::KeyPressed(int key, bool isRepeat)
{
   int numCells = mGridRows * mGridCols;
   if (key >= '1' && key <= '9')
   {
      int idx = key - '1';
      if (idx < numCells)
      {
         mActiveCell = idx;
         if (mAutoSwitch)
         {
            std::fill(mCellHeld.begin(), mCellHeld.end(), false);
            if (idx < (int)mCellHeld.size())
               mCellHeld[idx] = true;
         }
      }
   }
   else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
   {
      int dir = (key == juce::KeyPress::leftKey) ? -1 : -mGridCols;
      mActiveCell = (mActiveCell + dir + numCells) % numCells;
   }
   else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
   {
      int dir = (key == juce::KeyPress::rightKey) ? 1 : mGridCols;
      mActiveCell = (mActiveCell + dir + numCells) % numCells;
   }
}

void DisplayManager::AutoSelect()
{
   if (!mAutoSwitch)
      return;

   int numCells = mGridRows * mGridCols;
   int bestIdx = -1;
   int bestPriority = -1;
   for (int i = 0; i < numCells; ++i)
   {
      if (i < (int)mCellHeld.size() && mCellHeld[i])
      {
         int pri = i < (int)mCellPriority.size() ? mCellPriority[i] : 0;
         if (pri > bestPriority || (pri == bestPriority && bestIdx < 0))
         {
            bestPriority = pri;
            bestIdx = i;
         }
      }
   }
   if (bestIdx >= 0)
      mActiveCell = bestIdx;
}

void DisplayManager::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   if (right)
   {
      if (y >= 0 && IsSaveable())
      {
         if (TheSaveDataPanel->GetModule() == this)
            TheSaveDataPanel->SetModule(nullptr);
         else
            TheSaveDataPanel->SetModule(this);
      }
      return;
   }

   float headerH = 20;
   float contentTop = headerH + 3;
   float contentH = mModuleHeight - contentTop - 3;
   if (contentH <= 0) return;

   int cols = mGridCols;
   int rows = mGridRows;

   float padding = 2;
   float cellW = (mModuleWidth - padding * (cols + 1)) / cols;
   float cellH = (contentH - padding * (rows + 1)) / rows;
   if (cellW <= 0 || cellH <= 0) return;

   for (int i = 0; i < cols * rows; ++i)
   {
      int col = i % cols;
      int row = i / cols;
      float cx = padding + col * (cellW + padding);
      float cy = contentTop + row * (cellH + padding);

      if (x >= cx && x <= cx + cellW && y >= cy && y <= cy + cellH)
      {
         if (mAutoSwitch)
         {
            std::fill(mCellHeld.begin(), mCellHeld.end(), false);
            if (i < (int)mCellHeld.size())
               mCellHeld[i] = true;
            AutoSelect();
         }
         else
         {
            mActiveCell = (mActiveCell == i) ? -1 : i;
         }
         break;
      }
   }
}

void DisplayManager::Resize(float w, float h)
{
   mModuleWidth = std::max(kMinWidth, w);
   mModuleHeight = std::max(kMinHeight, h);
   if (mOutputCable)
      mOutputCable->SetManualPosition(mModuleWidth - 15, 10);
}

void DisplayManager::ResolveSources()
{
   // Method 1: check our own input cables
   for (size_t i = 0; i < mInputCables.size() && i < mSources.size(); ++i)
   {
      if (mInputCables[i] && !mInputCables[i]->GetPatchCables().empty())
      {
         auto* target = mInputCables[i]->GetPatchCables()[0]->GetTarget();
         mSources[i] = dynamic_cast<IVisualSource*>(target);
      }
      else
      {
         mSources[i] = nullptr;
      }
   }

   // Method 2: scan all modules for cables targeting THIS module (body drop)
   std::vector<IDrawableModule*> allModules;
   TheSynth->GetAllModules(allModules);
   IClickable* me = dynamic_cast<IClickable*>(this);
   for (auto* mod : allModules)
   {
      if (mod == this) continue;
      for (auto* source : mod->GetPatchCableSources())
      {
         for (auto* cable : source->GetPatchCables())
         {
            if (cable->GetTarget() && cable->GetTarget() == me)
            {
               auto* vis = dynamic_cast<IVisualSource*>(mod);
               if (vis)
               {
                  for (size_t i = 0; i < mSources.size(); ++i)
                  {
                     if (mSources[i] == nullptr)
                     {
                        mSources[i] = vis;
                        break;
                     }
                  }
               }
            }
         }
      }
   }
}

void DisplayManager::SaveState(FileStreamOut& out)
{
   out << GetModuleSaveStateRev();

   out << mGridRows;
   out << mGridCols;
   out << mActiveCell;
   out << mModuleWidth;
   out << mModuleHeight;
   out << mRootNote;
   out << mMidiChannel;
   out << mAutoSwitch;
   out << (int)mCellPriority.size();
   for (int p : mCellPriority)
      out << p;

   SaveStateBase(out);
}

void DisplayManager::LoadState(FileStreamIn& in, int rev)
{
   if (rev >= 2)
   {
      in >> mGridRows;
      in >> mGridCols;
      int loadedActiveCell;
      in >> loadedActiveCell;
      in >> mModuleWidth;
      in >> mModuleHeight;
      in >> mRootNote;
      in >> mMidiChannel;

      if (rev >= 3)
      {
         in >> mAutoSwitch;
         int priorityCount;
         in >> priorityCount;
         ApplyGridSize();
         for (int i = 0; i < priorityCount && i < (int)mCellPriority.size(); ++i)
            in >> mCellPriority[i];
      }
      else
      {
         ApplyGridSize();
      }

      IDrawableModule::LoadState(in, rev);

      if (loadedActiveCell >= 0 && loadedActiveCell < mGridRows * mGridCols)
         mActiveCell = loadedActiveCell;
   }
   else
   {
      IDrawableModule::LoadState(in, rev);
      in >> mGridRows;
      in >> mGridCols;
      int loadedActiveCell;
      in >> loadedActiveCell;
      in >> mModuleWidth;
      in >> mModuleHeight;
      if (rev >= 1)
      {
         in >> mRootNote;
         in >> mMidiChannel;
      }
      ApplyGridSize();
      if (loadedActiveCell >= 0 && loadedActiveCell < mGridRows * mGridCols)
         mActiveCell = loadedActiveCell;
   }
   if (mOutputCable)
      mOutputCable->SetManualPosition(mModuleWidth - 15, 10);
}

void DisplayManager::ApplyGridSize()
{
   int newTotal = mGridRows * mGridCols;
   if (newTotal < 1) newTotal = 1;
   int oldTotal = (int)mInputCables.size();

   if (newTotal > oldTotal)
   {
      mInputCables.resize(newTotal, nullptr);
      mSources.resize(newTotal, nullptr);
      mCellPriority.resize(newTotal, 0);
      mCellHeld.resize(newTotal, false);

      for (int i = oldTotal; i < newTotal; ++i)
      {
         mInputCables[i] = new PatchCableSource(this, kConnectionType_Special);
         mInputCables[i]->SetManualPosition(0, 25 + i * 18);
         mInputCables[i]->SetManualSide(PatchCableSource::Side::kLeft);
         AddPatchCableSource(mInputCables[i]);
      }
   }
   else if (newTotal < oldTotal)
   {
      for (int i = newTotal; i < oldTotal; ++i)
      {
         if (mInputCables[i])
         {
            mInputCables[i]->Clear();
            RemovePatchCableSource(mInputCables[i]);
            delete mInputCables[i];
         }
      }
      mInputCables.resize(newTotal);
      mSources.resize(newTotal);
      mCellPriority.resize(newTotal);
      mCellHeld.resize(newTotal);
   }

   for (int i = 0; i < newTotal; ++i)
   {
      if (mInputCables[i])
         mInputCables[i]->SetManualPosition(0, 25 + i * 18);
   }

   if (mActiveCell >= newTotal)
      mActiveCell = -1;
   mEditingPriority = 0;

   if (mOutputCable)
      mOutputCable->SetManualPosition(mModuleWidth - 15, 10);
}

VisualFBO* DisplayManager::GetFBO()
{
   return mOutputFBO;
}

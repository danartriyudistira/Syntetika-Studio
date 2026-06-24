#include "TrigMatrixFX.h"
#include "VisualFBO.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "OpenFrameworksPort.h"
#include "PatchCable.h"
#include "ModuleSaveDataPanel.h"

const char* TrigMatrixFX::sEffectNames[TRIGMATRIXFX_CELLS + 1] = {
   "pulse", "bars", "sparkle", "wave",
   "ripple", "tunnel", "glitch", "scanline",
   "plasma", "strobe", "spiral", "noise",
   "custom"
};

const char* TrigMatrixFX::sParamNames[4] = {
   "color", "speed", "intensity", "size"
};

TrigMatrixFX::TrigMatrixFX()
{
   for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
   {
      mCells[i].mEffectType = i;
      mCells[i].mParam1 = 0.5f;
      mCells[i].mParam2 = 0.5f;
      mCells[i].mParam3 = 0.5f;
      mCells[i].mParam4 = 0.5f;
      mCells[i].mCustomCode = "sin(t + x*0.01 + y*0.01)";
      mCodeValid[i] = false;
   }
}

TrigMatrixFX::~TrigMatrixFX()
{
   delete mFBO;
}

void TrigMatrixFX::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mEffectDropdown = new DropdownList(this, "effect", 5, 22, (int*)&mCells[0].mEffectType, 60);
   for (int i = 0; i <= TRIGMATRIXFX_CELLS; ++i)
      mEffectDropdown->AddLabel(sEffectNames[i], i);
   AddUIControl(mEffectDropdown);

   mParam1Slider = new FloatSlider(this, sParamNames[0], 70, 22, 70, 12, &mCells[0].mParam1, 0, 1);
   AddUIControl(mParam1Slider);
   mParam2Slider = new FloatSlider(this, sParamNames[1], 145, 22, 70, 12, &mCells[0].mParam2, 0, 1);
   AddUIControl(mParam2Slider);
   mParam3Slider = new FloatSlider(this, sParamNames[2], 220, 22, 70, 12, &mCells[0].mParam3, 0, 1);
   AddUIControl(mParam3Slider);
   mParam4Slider = new FloatSlider(this, sParamNames[3], 295, 22, 70, 12, &mCells[0].mParam4, 0, 1);
   AddUIControl(mParam4Slider);

   mCodeEntry = new TextEntry(this, "code", 5, 37, 40, &mCurrentCode);
   AddUIControl(mCodeEntry);

   mInputCable = new PatchCableSource(this, kConnectionType_Special);
   mInputCable->SetManualPosition((int)(mModuleWidth * 0.5f), (int)mModuleHeight);
   mInputCable->SetManualSide(PatchCableSource::Side::kBottom);
   AddPatchCableSource(mInputCable);
}

void TrigMatrixFX::GetModuleDimensions(float& width, float& height)
{
   width = mModuleWidth;
   height = mModuleHeight;
}

void TrigMatrixFX::Resize(float w, float h)
{
   mModuleWidth = std::max(kMinWidth, w);
   mModuleHeight = std::max(kMinHeight, h);
   if (mInputCable)
      mInputCable->SetManualPosition((int)(mModuleWidth * 0.5f), (int)mModuleHeight);
}

void TrigMatrixFX::SetActiveCell(int idx)
{
   mActiveCell = idx;
   if (idx >= 0 && idx < TRIGMATRIXFX_CELLS)
   {
      mEffectDropdown->SetVar((int*)&mCells[idx].mEffectType);
      mParam1Slider->SetVar(&mCells[idx].mParam1);
      mParam2Slider->SetVar(&mCells[idx].mParam2);
      mParam3Slider->SetVar(&mCells[idx].mParam3);
      mParam4Slider->SetVar(&mCells[idx].mParam4);
      mCurrentCode = mCells[idx].mCustomCode;
      mCodeEntry->SetText(mCurrentCode);
   }
}

void TrigMatrixFX::CompileExpression(int idx)
{
   mCodeValid[idx] = false;
   if (idx < 0 || idx >= TRIGMATRIXFX_CELLS) return;
   const std::string& code = mCells[idx].mCustomCode;
   if (code.empty()) return;
   mCustomSym[idx] = exprtk::symbol_table<float>();
   mCustomExpr[idx] = exprtk::expression<float>();
   mCustomSym[idx].add_variable("t", mTimeFloat);
   mCustomSym[idx].add_variable("x", mCells[idx].mExprX);
   mCustomSym[idx].add_variable("y", mCells[idx].mExprY);
   mCustomSym[idx].add_variable("w", mCells[idx].mExprW);
   mCustomSym[idx].add_variable("h", mCells[idx].mExprH);
   mCustomSym[idx].add_variable("p1", mCells[idx].mParam1);
   mCustomSym[idx].add_variable("p2", mCells[idx].mParam2);
   mCustomSym[idx].add_variable("p3", mCells[idx].mParam3);
   mCustomSym[idx].add_variable("p4", mCells[idx].mParam4);
   mCustomSym[idx].add_constants();
   mCustomExpr[idx].register_symbol_table(mCustomSym[idx]);
   exprtk::parser<float> parser;
   mCodeValid[idx] = parser.compile(code, mCustomExpr[idx]);
}

void TrigMatrixFX::TextEntryComplete(TextEntry* entry)
{
   if (entry == mCodeEntry && mActiveCell >= 0 && mActiveCell < TRIGMATRIXFX_CELLS)
   {
      mCells[mActiveCell].mCustomCode = mCurrentCode;
      CompileExpression(mActiveCell);
   }
}

void TrigMatrixFX::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
}

void TrigMatrixFX::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
}

void TrigMatrixFX::PostRepatch(PatchCableSource* source, bool fromUserClick)
{
   if (source == mInputCable)
   {
      if (!mInputCable->GetPatchCables().empty())
      {
         auto* target = mInputCable->GetPatchCables()[0]->GetTarget();
         mSource = dynamic_cast<IVisualSource*>(target);
      }
      else
      {
         mSource = nullptr;
      }
   }
}

void TrigMatrixFX::ButtonClicked(ClickButton* button, double time)
{
}

void TrigMatrixFX::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled)
      return;

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   int cellIndex = pitch % TRIGMATRIXFX_CELLS;
   if (velocity > 0)
   {
      for (int j = 0; j < TRIGMATRIXFX_CELLS; ++j)
         if (j != cellIndex) mCells[j].mTriggerFlash = 0;
      mCells[cellIndex].mTriggerFlash = 1.0f;
      mCells[cellIndex].mTriggerTime = time;
      SetActiveCell(cellIndex);
   }
}

void TrigMatrixFX::OnPulse(double time, float velocity, int flags)
{
   if (!mEnabled)
      return;

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   int next = (mActiveCell + 1) % TRIGMATRIXFX_CELLS;
   for (int j = 0; j < TRIGMATRIXFX_CELLS; ++j)
      if (j != next) mCells[j].mTriggerFlash = 0;
   mCells[next].mTriggerFlash = 1.0f;
   mCells[next].mTriggerTime = time;
   SetActiveCell(next);
}

void TrigMatrixFX::OnClicked(float x, float y, bool right)
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

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   // check "trig all" button
   if (x >= mModuleWidth - 54 && x <= mModuleWidth - 4 && y >= 3 && y <= 17)
   {
      for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
      {
         mCells[i].mTriggerFlash = 1.0f;
         mCells[i].mTriggerTime = gTime;
      }
      return;
   }

   const float stripH = 14;

   // grid mode: check grid selector buttons in content area
   if (mSource == nullptr)
   {
      float contentTop = kHeaderH + 36;
      float contentH = mModuleHeight - contentTop - stripH - kPad - 2;
      float cellW = (mModuleWidth - kPad * (TRIGMATRIXFX_COLS + 1)) / TRIGMATRIXFX_COLS;
      float cellH = (contentH - kPad * (TRIGMATRIXFX_ROWS + 1)) / TRIGMATRIXFX_ROWS;
      if (cellW > 0 && cellH > 0)
      {
         for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
         {
            int col = i % TRIGMATRIXFX_COLS;
            int row = i / TRIGMATRIXFX_COLS;
            float cx = kPad + col * (cellW + kPad);
            float cy = contentTop + row * (cellH + kPad);
            if (x >= cx && x <= cx + cellW && y >= cy && y <= cy + cellH)
            {
               for (int j = 0; j < TRIGMATRIXFX_CELLS; ++j)
                  if (j != i) mCells[j].mTriggerFlash = 0;
               mCells[i].mTriggerFlash = 1.0f;
               mCells[i].mTriggerTime = gTime;
               SetActiveCell(i);
               return;
            }
         }
      }
   }


}

void TrigMatrixFX::PostRender()
{
   if (!mEnabled || mModuleWidth < 20 || mModuleHeight < 50)
      return;

   std::lock_guard<std::recursive_mutex> lock(mDataMutex);

   // decay trigger flashes
   for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
      mCells[i].mTriggerFlash *= 0.993f;
   if (mTriggerAll) mTriggerAll = false;

   if (!mFBO || !mFBO->IsValid() ||
       mFBO->GetWidth() != (int)mModuleWidth ||
       mFBO->GetHeight() != (int)mModuleHeight)
   {
      delete mFBO;
      mFBO = new VisualFBO();
      mFBO->Create(std::max(64, (int)mModuleWidth), std::max(64, (int)mModuleHeight));
   }

   if (!mFBO || !mFBO->IsValid())
      return;

   // resolve source
   if (mInputCable && !mInputCable->GetPatchCables().empty())
   {
      auto* target = mInputCable->GetPatchCables()[0]->GetTarget();
      mSource = dynamic_cast<IVisualSource*>(target);
   }

   mFBO->Bind();  // clears to black via glClear in VisualFBO::Bind

   mTime = gTime;

   // Draw source image if connected (centered full-FBO, aspect ratio preserved, letterboxed)
   if (mSource != nullptr && mSource->GetFBO() && mSource->GetFBO()->IsValid())
   {
      auto* srcFBO = mSource->GetFBO();
      srcFBO->ReleaseDisplayImage(); // force recreate in current (FBO) NVG context
      float srcW = (float)srcFBO->GetWidth();
      float srcH = (float)srcFBO->GetHeight();
      float aspect = srcW / srcH;
      float drawW = mModuleWidth;
      float drawH = drawW / aspect;
      if (drawH > mModuleHeight)
      {
         drawH = mModuleHeight;
         drawW = drawH * aspect;
      }
      float drawX = (mModuleWidth - drawW) * 0.5f;
      float drawY = (mModuleHeight - drawH) * 0.5f;
      srcFBO->Draw(drawX, drawY, drawW, drawH);
   }

   // Draw triggered effects full-screen (same for source and grid mode)
   for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
   {
      if (mCells[i].mTriggerFlash > 0.01f)
      {
         switch (mCells[i].mEffectType)
         {
         case 0: DrawFX0_Pulse(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 1: DrawFX1_Bars(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 2: DrawFX2_Sparkle(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 3: DrawFX3_Wave(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 4: DrawFX4_Ripple(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 5: DrawFX5_Tunnel(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 6: DrawFX6_Glitch(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 7: DrawFX7_Scanline(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 8: DrawFX8_Plasma(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 9: DrawFX9_Strobe(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
         case 10: DrawFX10_Spiral(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
          case 11: DrawFX11_Noise(0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
          default: DrawCustomEffect(i, 0, 0, mModuleWidth, mModuleHeight, mCells[i]); break;
          }
      }
   }

   mFBO->Unbind();
}

VisualFBO* TrigMatrixFX::GetFBO()
{
   return mFBO;
}

void TrigMatrixFX::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   if (mFBO && mFBO->IsValid())
      mFBO->Draw(0, 0, mModuleWidth, mModuleHeight);

   // semi-transparent overlay for controls
   ofPushStyle();
   ofSetColor(0, 0, 0, 160);
   ofFill();
   ofRect(0, 0, mModuleWidth, kHeaderH + 34);
   ofPopStyle();

   // title + source indicator
   ofPushStyle();
   ofSetColor(200, 200, 200);
   DrawTextNormal("TrigMatrixFX", 5, 13, 12);
   if (mSource)
   {
      ofSetColor(100, 255, 100);
      DrawTextNormal("SRC", 90, 13, 10);
   }
   ofPopStyle();

   // trigger all button
   ofPushStyle();
   ofSetColor(60, 60, 80, 200);
   ofRect(mModuleWidth - 54, 3, 50, 14);
   ofSetColor(200, 200, 200);
   DrawTextNormal("trig all", mModuleWidth - 50, 14, 10);
   ofPopStyle();

   // controls area
   mEffectDropdown->Draw();
   mParam1Slider->Draw();
   mParam2Slider->Draw();
   mParam3Slider->Draw();
   mParam4Slider->Draw();

   // code entry (only for custom effect type)
   if (mActiveCell >= 0 && mActiveCell < TRIGMATRIXFX_CELLS &&
       mCells[mActiveCell].mEffectType == TRIGMATRIXFX_CUSTOM_TYPE)
   {
      mCodeEntry->Draw();
   }

   if (mActiveCell >= 0 && mActiveCell < TRIGMATRIXFX_CELLS)
   {
      ofPushStyle();
      ofSetColor(180, 220, 255);
      DrawTextNormal(sEffectNames[mCells[mActiveCell].mEffectType], mModuleWidth - 100, 30, 10);
      ofPopStyle();
   }

   // grid mode: 12 visual effect cells in content area
   if (mSource == nullptr)
   {
      float contentTop = kHeaderH + 36;
      float stripHlocal = 14;
      float contentH = mModuleHeight - contentTop - stripHlocal - kPad - 2;
      float cellW = (mModuleWidth - kPad * (TRIGMATRIXFX_COLS + 1)) / TRIGMATRIXFX_COLS;
      float cellH = (contentH - kPad * (TRIGMATRIXFX_ROWS + 1)) / TRIGMATRIXFX_ROWS;
      for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
      {
         int col = i % TRIGMATRIXFX_COLS;
         int row = i / TRIGMATRIXFX_COLS;
         float x = kPad + col * (cellW + kPad);
         float y = contentTop + row * (cellH + kPad);
         DrawCellEffect(i, x, y, cellW, cellH);
      }
   }

   // status bar at bottom
   float stripH = 14;
   {
      float stripY = mModuleHeight - stripH - kPad;
      ofSetColor(25, 25, 35, 200);
      ofFill();
      ofRect(0, stripY, mModuleWidth, stripH);

      // active cell indicator
      if (mActiveCell >= 0)
      {
         float r, g, b;
         HSVtoRGB(fmodf(mActiveCell * 30.0f + mTime * 20.0f, 360.0f), 0.6f, 1.0f, r, g, b);
         ofSetColor((int)(r * 180), (int)(g * 180), (int)(b * 180), 200);
         ofFill();
         ofRect(kPad, stripY + kPad, 14, stripH - kPad * 2);
         ofSetColor(0, 0, 0, 220);
         DrawTextNormal(std::to_string(mActiveCell + 1), kPad + 3, stripY + stripH - kPad - 3, 9);
         ofSetColor(200, 200, 200, 200);
         DrawTextNormal(sEffectNames[mCells[mActiveCell].mEffectType], kPad + 18, stripY + stripH - kPad - 3, 9);
      }

      // trigger flash dots for all cells
      float dotSpacing = (mModuleWidth - 120) / (TRIGMATRIXFX_CELLS + 1);
      float dotX = 120;
      for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
      {
         float dx = dotX + i * dotSpacing;
         float dy = stripY + stripH * 0.5f;
         if (mCells[i].mTriggerFlash > 0.01f)
         {
            float r, g, b;
            HSVtoRGB(fmodf(i * 30.0f, 360.0f), 0.7f, 1.0f, r, g, b);
            ofSetColor((int)(r * 200), (int)(g * 200), (int)(b * 200), (int)(mCells[i].mTriggerFlash * 255));
            ofFill();
            ofCircle(dx, dy, 3);
         }
         else if (i == mActiveCell)
         {
            ofSetColor(0, 255, 255, 120);
            ofFill();
            ofCircle(dx, dy, 2);
         }
         else
         {
            ofSetColor(40, 40, 55, 150);
            ofFill();
            ofCircle(dx, dy, 2);
         }
      }
   }
   // active cell border (source mode only)
   if (mSource != nullptr && mSource->GetFBO() && mSource->GetFBO()->IsValid() && mActiveCell >= 0)
   {
      ofPushStyle();
      ofNoFill();
      float r, g, b;
      HSVtoRGB(fmodf(mActiveCell * 30.0f + mTime * 20.0f, 360.0f), 0.6f, 1.0f, r, g, b);
      ofSetColor((int)(r * 255), (int)(g * 255), (int)(b * 255), 150);
      ofSetLineWidth(2);
      ofRect(2, kHeaderH + 36, mModuleWidth - 4, mModuleHeight - (kHeaderH + 36) - stripH - 6);
      ofSetLineWidth(1);
      ofPopStyle();
   }
}

// static
void TrigMatrixFX::HSVtoRGB(float h, float s, float v, float& r, float& g, float& b)
{
   if (s < 0.001f) { r = v; g = v; b = v; return; }
   float hh = fmodf(h, 360.0f) / 60.0f;
   int hi = (int)hh;
   float f = hh - hi;
   float p = v * (1.0f - s);
   float q = v * (1.0f - s * f);
   float t = v * (1.0f - s * (1.0f - f));
   switch (hi)
   {
   case 0: r=v; g=t; b=p; break;
   case 1: r=q; g=v; b=p; break;
   case 2: r=p; g=v; b=t; break;
   case 3: r=p; g=q; b=v; break;
   case 4: r=t; g=p; b=v; break;
   default: r=v; g=p; b=q; break;
   }
}

void TrigMatrixFX::DrawCustomEffect(int idx, float x, float y, float w, float h, const FXCell& cell)
{
   if (!mCodeValid[idx])
   {
      CompileExpression(idx);
      if (!mCodeValid[idx])
      {
         ofSetColor(60, 20, 20, 255);
         ofFill();
         ofRect(x, y, w, h);
         ofSetColor(255, 100, 100);
         DrawTextNormal("err", x + 2, y + 12, 10);
         return;
      }
   }

   if (!mCodeValid[idx])
      return;

    mTimeFloat = (float)(mTime * 0.001);
    mCells[idx].mExprW = w;
    mCells[idx].mExprH = h;
    int step = (std::min(w, h) < 150) ? 12 : 6;
   for (int gy = 0; gy < (int)h; gy += step)
   {
      for (int gx = 0; gx < (int)w; gx += step)
      {
         mCells[idx].mExprX = (float)gx;
         mCells[idx].mExprY = (float)gy;

         float val = mCustomExpr[idx].value();
         val = ofClamp(val * 0.5f + 0.5f, 0.0f, 1.0f);

         float hue = fmodf(val * 360.0f + mTimeFloat * 20.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.8f, 0.4f + 0.6f * val, r, g, b);
         ofSetColor((int)(r * 200), (int)(g * 200), (int)(b * 200));
         ofFill();
         ofRect(x + gx, y + gy, step, step);
      }
   }
}

void TrigMatrixFX::DrawCellEffect(int idx, float x, float y, float w, float h)
{
   const FXCell& cell = mCells[idx];

   // cell background
   ofSetColor(25, 25, 35, 255);
   ofFill();
   ofRect(x, y, w, h);

   // draw mini effect preview
   ofPushMatrix();
   ofTranslate(x, y);
   if (cell.mEffectType == TRIGMATRIXFX_CUSTOM_TYPE)
   {
      DrawCustomEffect(idx, 0, 0, w, h, cell);
   }
   else
   {
      switch (cell.mEffectType)
      {
      case 0: DrawFX0_Pulse(0, 0, w, h, cell); break;
      case 1: DrawFX1_Bars(0, 0, w, h, cell); break;
      case 2: DrawFX2_Sparkle(0, 0, w, h, cell); break;
      case 3: DrawFX3_Wave(0, 0, w, h, cell); break;
      case 4: DrawFX4_Ripple(0, 0, w, h, cell); break;
      case 5: DrawFX5_Tunnel(0, 0, w, h, cell); break;
      case 6: DrawFX6_Glitch(0, 0, w, h, cell); break;
      case 7: DrawFX7_Scanline(0, 0, w, h, cell); break;
      case 8: DrawFX8_Plasma(0, 0, w, h, cell); break;
      case 9: DrawFX9_Strobe(0, 0, w, h, cell); break;
      case 10: DrawFX10_Spiral(0, 0, w, h, cell); break;
      case 11: DrawFX11_Noise(0, 0, w, h, cell); break;
      }
   }
   ofPopMatrix();

   // flash overlay on trigger
   if (cell.mTriggerFlash > 0.01f)
   {
      ofPushStyle();
      float flash = cell.mTriggerFlash;
      float r, g, b;
      HSVtoRGB(fmodf(idx * 30.0f + mTime * 20.0f, 360.0f), 0.6f, 1.0f, r, g, b);
      ofSetColor((int)(r * flash * 120), (int)(g * flash * 120), (int)(b * flash * 120), (int)(flash * 180));
      ofFill();
      ofRect(x, y, w, h);
      ofPopStyle();

      ofPushStyle();
      ofNoFill();
      ofSetColor((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(flash * 255));
      ofSetLineWidth(2);
      ofRect(x, y, w, h);
      ofSetLineWidth(1);
      ofFill();
      ofPopStyle();
   }

   // active cell highlight
   if (idx == mActiveCell)
   {
      ofPushStyle();
      ofNoFill();
      ofSetColor(0, 255, 255, 200);
      ofSetLineWidth(2);
      ofRect(x, y, w, h);
      ofSetLineWidth(1);
      ofFill();
      ofPopStyle();
   }

   // cell number + name label
   ofPushStyle();
   ofSetColor(200, 200, 200, 200);
   ofFill();
   ofRect(x + 2, y + 2, 14, 11);
   ofSetColor(20, 20, 20, 220);
   DrawTextNormal(std::to_string(idx + 1), x + 4, y + 11, 9);
   ofSetColor(180, 180, 200, 180);
   DrawTextNormal(sEffectNames[cell.mEffectType], x + 20, y + 11, 9);
   ofPopStyle();
}

// --- Effect Draw Functions ---

void TrigMatrixFX::DrawFX0_Pulse(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f + 0.5f;
   float intensity = cell.mParam3;
   float size = cell.mParam4 * 0.8f + 0.2f;
   float hue = cell.mParam1 * 360.0f + mTime * 10.0f;

   float pulse = flash > 0.01f ? flash : fmodf(mTime * speed * 0.5f, 1.0f);

   float r, g, b;
   HSVtoRGB(fmodf(hue + pulse * 60.0f, 360.0f), 0.8f, 1.0f, r, g, b);

   float radius = pulse * fminf(w, h) * 0.5f * size;
   float alpha = (1.0f - pulse) * 200 * intensity;

   ofNoFill();
   ofSetLineWidth(2);
   ofSetColor((int)(r * alpha), (int)(g * alpha), (int)(b * alpha));
   ofCircle(w * 0.5f, h * 0.5f, radius);

   radius *= 0.6f;
   ofSetLineWidth(1);
   ofSetColor((int)(r * alpha * 0.5f), (int)(g * alpha * 0.5f), (int)(b * alpha * 0.5f));
   ofCircle(w * 0.5f, h * 0.5f, radius);
}

void TrigMatrixFX::DrawFX1_Bars(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f;
   float intensity = cell.mParam3;
   float size = cell.mParam4 * 0.8f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   int numBars = (int)(5 + size * 10);
   float barW = w / numBars;
   float phase = mTime * speed;

   for (int i = 0; i < numBars; ++i)
   {
      float barH = (0.2f + 0.8f * (0.5f + 0.5f * sinf(i * 1.5f + phase))) * h * intensity;
      if (flash > 0.01f)
         barH = h * (0.3f + 0.7f * flash) * intensity;
      float hue = fmodf(hueOff + i * 25.0f + mTime * 30.0f, 360.0f);
      float r, g, b;
      HSVtoRGB(hue, 0.7f, 0.6f + 0.4f * barH / h, r, g, b);
      ofSetColor((int)(r * 200), (int)(g * 200), (int)(b * 200));
      ofFill();
      ofRect(i * barW, h - barH, barW - 1, barH);
   }
}

void TrigMatrixFX::DrawFX2_Sparkle(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 4.0f + 0.5f;
   float intensity = cell.mParam3;
   float density = cell.mParam4 * 0.8f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   int numPoints = (int)(10 + density * 40);

   for (int i = 0; i < numPoints; ++i)
   {
      float seed = i * 1.618f;
      float px = fmodf(seed * 1.1f + mTime * speed * 0.1f, 1.0f) * w;
      float py = fmodf(seed * 2.3f + mTime * speed * 0.07f + sinf(mTime * speed * 0.3f + i) * 0.1f, 1.0f) * h;
      float life = fmodf(mTime * speed * 0.2f + seed, 1.0f);
      float alpha = (1.0f - life) * 200 * intensity;

      if (flash > 0.01f && fmodf(i * 0.7f + mTime * 10.0f, 1.0f) < flash * 0.5f)
         alpha *= 1.0f + flash * 2.0f;

      if (alpha > 5)
      {
         float hue = fmodf(hueOff + i * 40.0f + mTime * 50.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.8f, 1.0f, r, g, b);
         float sz = 1.5f + life * 3.0f;
         ofSetColor((int)(r * alpha), (int)(g * alpha), (int)(b * alpha));
         ofFill();
         ofCircle(px, py, sz);
      }
   }
}

void TrigMatrixFX::DrawFX3_Wave(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f + 0.5f;
   float intensity = cell.mParam3;
   float thickness = cell.mParam4 * 5.0f + 1.0f;
   float hueOff = cell.mParam1 * 360.0f;

   float phase = mTime * speed;
   float amp = h * 0.3f * intensity;
   if (flash > 0.01f)
      amp *= 1.0f + flash;

   ofSetLineWidth(thickness);
   ofNoFill();

   int segments = 40;
   float step = w / segments;

   float r, g, b;
   HSVtoRGB(fmodf(hueOff + mTime * 20.0f, 360.0f), 0.8f, 1.0f, r, g, b);
   ofSetColor((int)(r * 200), (int)(g * 200), (int)(b * 200));

   ofBeginShape();
   for (int i = 0; i <= segments; ++i)
   {
      float sx = i * step;
      float sy = h * 0.5f + sinf(i * 0.3f + phase) * amp;
      ofVertex(sx, sy);
   }
   ofEndShape(false);

   // second wave
   HSVtoRGB(fmodf(hueOff + 120.0f + mTime * 30.0f, 360.0f), 0.6f, 0.8f, r, g, b);
   ofSetColor((int)(r * 150), (int)(g * 150), (int)(b * 150));
   ofBeginShape();
   for (int i = 0; i <= segments; ++i)
   {
      float sx = i * step;
      float sy = h * 0.5f + sinf(i * 0.5f + phase * 1.3f + 1.0f) * amp * 0.6f;
      ofVertex(sx, sy);
   }
   ofEndShape(false);
   ofSetLineWidth(1);
}

void TrigMatrixFX::DrawFX4_Ripple(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f;
   float intensity = cell.mParam3;
   float spacing = cell.mParam4 * 0.5f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   float triggerTime = flash > 0.01f ? 1.0f : 0.0f;
   float phase = mTime * speed;

   ofNoFill();
   int numRings = (int)(3 + spacing * 5);
   for (int i = 0; i < numRings; ++i)
   {
      float radius = (fmodf(phase + i * spacing, 1.0f)) * fminf(w, h) * 0.45f;
      float alpha = (1.0f - radius / (fminf(w, h) * 0.45f)) * 150 * intensity;
      if (triggerTime > 0.01f)
      {
         radius += flash * fminf(w, h) * 0.2f;
         alpha *= 1.0f + flash;
      }

      if (alpha > 3)
      {
         float hue = fmodf(hueOff + i * 40.0f + mTime * 20.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.7f, 0.9f, r, g, b);
         ofSetLineWidth(1.5f);
         ofSetColor((int)(r * alpha), (int)(g * alpha), (int)(b * alpha));
         ofCircle(w * 0.5f, h * 0.5f, radius);
      }
   }
}

void TrigMatrixFX::DrawFX5_Tunnel(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 2.0f + 0.5f;
   float intensity = cell.mParam3;
   float size = cell.mParam4 * 0.6f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   float phase = mTime * speed;
   float cx = w * 0.5f;
   float cy = h * 0.5f;
   float maxR = fminf(w, h) * 0.5f * size;

   if (flash > 0.01f)
      phase += flash * 2.0f;

   ofNoFill();
   int numCircles = 10;
   for (int i = 0; i < numCircles; ++i)
   {
      float t = fmodf(phase + i * 0.15f, 1.0f);
      float radius = t * maxR;
      float alpha = (1.0f - t) * 180 * intensity;
      if (alpha > 3)
      {
         float hue = fmodf(hueOff + i * 30.0f + mTime * 40.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.8f, 0.7f + 0.3f * (1.0f - t), r, g, b);
         ofSetLineWidth(1.5f + t * 2.0f);
         ofSetColor((int)(r * alpha), (int)(g * alpha), (int)(b * alpha));
         ofCircle(cx, cy, radius);
      }
   }
}

void TrigMatrixFX::DrawFX6_Glitch(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 4.0f;
   float intensity = cell.mParam3;
   float density = cell.mParam4 * 0.8f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   int numStrips = (int)(5 + density * 15);
   float stripH = h / numStrips;

   for (int s = 0; s < numStrips; ++s)
   {
      float offset = 0;
      float flicker = fmodf(mTime * speed * 0.5f + s * 0.7f, 1.0f);
      if (flicker < 0.3f * intensity || (flash > 0.01f && ofRandom(0, 1) < flash * 0.6f))
         offset = (flicker - 0.5f) * w * 0.15f;

      float y0 = s * stripH;
      float y1 = y0 + stripH;

      if (fabsf(offset) > 0.5f)
      {
         float hue = fmodf(hueOff + s * 30.0f + mTime * 50.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.8f, 0.5f, r, g, b);
         ofSetColor((int)(r * 100), (int)(g * 100), (int)(b * 100), 150);
      }
      else
      {
         ofSetColor(10, 10, 15, 200);
      }
      ofFill();
      ofRect(offset, y0, w, stripH);

      // thin horizontal line per strip
      float lineAlpha = 40 + 40 * sinf(s * 2.0f + mTime * speed * 2.0f);
      if (flash > 0.01f)
         lineAlpha += flash * 100;
      float hue2 = fmodf(hueOff + s * 20.0f, 360.0f);
      float r, g, b;
      HSVtoRGB(hue2, 0.6f, 0.8f, r, g, b);
      ofSetLineWidth(1);
      ofSetColor((int)(r * lineAlpha), (int)(g * lineAlpha), (int)(b * lineAlpha));
      ofLine(offset, y0 + stripH * 0.5f, w + offset, y0 + stripH * 0.5f);
   }
}

void TrigMatrixFX::DrawFX7_Scanline(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f + 0.5f;
   float intensity = cell.mParam3;
   float density = cell.mParam4 * 0.6f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   int numLines = (int)(10 + density * 30);
   float spacing = h / numLines;
   float phase = mTime * speed;

   // background gradient
   for (int i = 0; i < numLines; ++i)
   {
      float t = (float)i / numLines;
      float alpha = 20 + 30 * (0.5f + 0.5f * sinf(t * 10.0f + phase));
      if (flash > 0.01f)
         alpha += flash * 80;
      float hue = fmodf(hueOff + t * 60.0f + mTime * 20.0f, 360.0f);
      float r, g, b;
      HSVtoRGB(hue, 0.5f, 0.6f, r, g, b);
      ofSetLineWidth(1);
      ofSetColor((int)(r * alpha), (int)(g * alpha), (int)(b * alpha));
      ofLine(0, i * spacing, w, i * spacing);
   }

   // bright moving scanline
   float scanPos = fmodf(phase, 1.0f) * h;
   float scanAlpha = 150 * intensity;
   if (flash > 0.01f)
      scanAlpha *= 1.0f + flash * 2.0f;
   float r, g, b;
   HSVtoRGB(fmodf(hueOff + mTime * 60.0f, 360.0f), 0.8f, 1.0f, r, g, b);
   ofSetColor((int)(r * scanAlpha), (int)(g * scanAlpha), (int)(b * scanAlpha));
   ofSetLineWidth(2);
   ofLine(0, scanPos, w, scanPos);
   ofSetLineWidth(1);
}

void TrigMatrixFX::DrawFX8_Plasma(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 2.0f + 0.5f;
   float intensity = cell.mParam3;
   float scale = cell.mParam4 * 4.0f + 1.0f;
   float hueOff = cell.mParam1 * 360.0f;

   float t = mTime * speed;

    ofFill();
    int step = (std::min(w, h) < 150) ? 12 : 6;
    for (int gy = 0; gy < (int)h; gy += step)
    {
       for (int gx = 0; gx < (int)w; gx += step)
       {
          float v = sinf(gx * 0.05f * scale + t) + sinf(gy * 0.03f * scale + t * 0.7f) + sinf((gx + gy) * 0.04f * scale + t * 1.3f);
         v = v / 3.0f + 0.5f;
         v = ofClamp(v, 0.0f, 1.0f);

         if (flash > 0.01f)
            v = v * (1.0f - flash * 0.5f) + flash * 0.5f;

         float hue = fmodf(hueOff + v * 240.0f + mTime * 15.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.8f, 0.4f + 0.6f * v * intensity, r, g, b);
         ofSetColor((int)(r * 200), (int)(g * 200), (int)(b * 200));
         ofRect(gx, gy, step, step);
      }
   }
}

void TrigMatrixFX::DrawFX9_Strobe(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 8.0f + 1.0f;
   float intensity = cell.mParam3;
   float duty = cell.mParam4 * 0.4f + 0.05f;
   float hueOff = cell.mParam1 * 360.0f;

   float phase = fmodf(mTime * speed, 1.0f);
   bool on = phase < duty;

   if (flash > 0.01f)
      on = true;

   if (on)
   {
      float r, g, b;
      HSVtoRGB(fmodf(hueOff + mTime * 80.0f, 360.0f), 0.6f, 1.0f, r, g, b);
      ofSetColor((int)(r * 150 * intensity), (int)(g * 150 * intensity), (int)(b * 150 * intensity));
      ofFill();
      ofRect(0, 0, w, h);
   }
}

void TrigMatrixFX::DrawFX10_Spiral(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f + 0.5f;
   float intensity = cell.mParam3;
   float size = cell.mParam4 * 0.6f + 0.2f;
   float hueOff = cell.mParam1 * 360.0f;

   float phase = mTime * speed;
   float cx = w * 0.5f;
   float cy = h * 0.5f;
   float maxR = fminf(w, h) * 0.4f * size;

   if (flash > 0.01f)
      phase += flash * 3.0f;

   ofNoFill();
   ofSetLineWidth(1.5f);

   int points = 100;
   float step = 1.0f / points;
   for (int i = 0; i < 3; ++i)
   {
      float r, g, b;
      HSVtoRGB(fmodf(hueOff + i * 120.0f + mTime * 30.0f, 360.0f), 0.8f, 1.0f, r, g, b);
      ofSetColor((int)(r * 180 * intensity), (int)(g * 180 * intensity), (int)(b * 180 * intensity));

      ofBeginShape();
      for (int p = 0; p < points; ++p)
      {
         float t = p * step;
         float angle = t * 6.0f * TWO_PI + phase + i * 2.0f;
         float radius = t * maxR;
         float sx = cx + cosf(angle) * radius;
         float sy = cy + sinf(angle) * radius;
         ofVertex(sx, sy);
      }
      ofEndShape(false);
   }
   ofSetLineWidth(1);
}

void TrigMatrixFX::DrawFX11_Noise(float x, float y, float w, float h, const FXCell& cell)
{
   float flash = cell.mTriggerFlash;
   float speed = cell.mParam2 * 3.0f + 0.5f;
   float intensity = cell.mParam3;
   float scale = cell.mParam4 * 8.0f + 2.0f;
   float hueOff = cell.mParam1 * 360.0f;

   float t = mTime * speed;

    ofFill();
    int step = (std::min(w, h) < 150) ? 10 : 5;
    for (int gy = 0; gy < (int)h; gy += step)
    {
       for (int gx = 0; gx < (int)w; gx += step)
       {
          float noise = sinf(gx * 0.1f * scale + t * 0.5f) * cosf(gy * 0.08f * scale + t * 0.3f);
         noise = noise * 0.5f + 0.5f;
         noise = ofClamp(noise, 0.0f, 1.0f);

         if (flash > 0.01f)
            noise = fmodf(noise + flash * 0.5f, 1.0f);

         float hue = fmodf(hueOff + noise * 180.0f + gx * 0.5f + gy * 0.5f + mTime * 40.0f, 360.0f);
         float r, g, b;
         HSVtoRGB(hue, 0.7f, 0.3f + 0.7f * noise * intensity, r, g, b);
         ofSetColor((int)(r * 200), (int)(g * 200), (int)(b * 200));
         ofRect(gx, gy, step, step);
      }
   }
}

void TrigMatrixFX::SaveState(FileStreamOut& out)
{
   out << GetModuleSaveStateRev();
   out << mModuleWidth;
   out << mModuleHeight;
   out << mActiveCell;
   for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
   {
      out << mCells[i].mEffectType;
      out << mCells[i].mParam1;
      out << mCells[i].mParam2;
      out << mCells[i].mParam3;
      out << mCells[i].mParam4;
      out << mCells[i].mCustomCode;
   }
   SaveStateBase(out);
}

void TrigMatrixFX::LoadState(FileStreamIn& in, int rev)
{
   in >> mModuleWidth;
   in >> mModuleHeight;
   in >> mActiveCell;
   for (int i = 0; i < TRIGMATRIXFX_CELLS; ++i)
   {
      in >> mCells[i].mEffectType;
      in >> mCells[i].mParam1;
      in >> mCells[i].mParam2;
      in >> mCells[i].mParam3;
      in >> mCells[i].mParam4;
      if (rev >= 2)
         in >> mCells[i].mCustomCode;
      CompileExpression(i);
   }
}

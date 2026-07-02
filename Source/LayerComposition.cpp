#include "LayerComposition.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "VisualFBO.h"
#include <cmath>

LayerComposition::LayerComposition()
{
}

LayerComposition::~LayerComposition()
{
   delete mOutputFBO;
}

void LayerComposition::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   float py = 3;
   float pw = mWidth - kPanelW + 2;

   for (int i = 0; i < kNumLayers; ++i)
   {
      mLayers[i].mInputCable = new PatchCableSource(this, kConnectionType_Special);
      mLayers[i].mInputCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
      mLayers[i].mInputCable->SetManualSide(PatchCableSource::Side::kLeft);
      mLayers[i].mInputCable->SetManualPosition(0, 12 + i * 22);
      AddPatchCableSource(mLayers[i].mInputCable);
   }

   mSourceDropdown = new DropdownList(this, "src", pw, py, &mEditSourceIndex);
   mSourceDropdown->SetWidth(kPanelW - 10);

   mBlendDropdown = new DropdownList(this, "bl", pw, py + 18, &mEditBlendMode);
   mBlendDropdown->SetWidth(kPanelW - 10);
   mBlendDropdown->AddLabel("Normal", 0);
   mBlendDropdown->AddLabel("Add", 1);
   mBlendDropdown->AddLabel("Mul", 2);
   mBlendDropdown->AddLabel("Screen", 3);

   mOpacitySlider = new FloatSlider(this, "op", pw, py + 36 + 1, kPanelW - 10, 14, &mEditOpacity, 0, 1, 2);

   mMirrorXCheck = new Checkbox(this, "mirX", pw, py + 54, &mEditMirrorX);
   mMirrorYCheck = new Checkbox(this, "mirY", pw + 22, py + 54, &mEditMirrorY);

   mScaleXSlider = new FloatSlider(this, "sx", pw, py + 72 + 2, kPanelW - 10, 13, &mEditScaleX, 0.1f, 3.0f, 2);
   mScaleYSlider = new FloatSlider(this, "sy", pw, py + 88 + 2, kPanelW - 10, 13, &mEditScaleY, 0.1f, 3.0f, 2);

   mOffsetXSlider = new FloatSlider(this, "ox", pw, py + 104 + 2, kPanelW - 10, 13, &mEditOffsetX, -1.0f, 1.0f, 2);
   mOffsetYSlider = new FloatSlider(this, "oy", pw, py + 120 + 2, kPanelW - 10, 13, &mEditOffsetY, -1.0f, 1.0f, 2);

   mRotationSlider = new FloatSlider(this, "rot", pw, py + 136 + 2, kPanelW - 10, 13, &mEditRotation, -180, 180, 1);

   mEnabledCheck = new Checkbox(this, "en", pw, py + 155, &mEditEnabled);

   float barY = mHeight - kBarH;
   mOutputResDropdown = new DropdownList(this, "out", 2, barY + 2, &mOutputRes);
   mOutputResDropdown->SetWidth(52);
   mOutputResDropdown->AddLabel("src", kRes_Source);
   mOutputResDropdown->AddLabel("1080", kRes_1080p);
   mOutputResDropdown->AddLabel("720", kRes_720p);
   mOutputResDropdown->AddLabel("540", kRes_540p);
   mOutputResDropdown->AddLabel("cust", kRes_Custom);

   mCustomWidthSlider = new IntSlider(this, "cw", 58, barY + 2, 52, 13, &mCustomWidth, 64, 4096);
   mCustomHeightSlider = new IntSlider(this, "ch", 114, barY + 2, 52, 13, &mCustomHeight, 64, 4096);

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   mOutputCable->SetManualPosition(mWidth - 8, mHeight / 2);
   AddPatchCableSource(mOutputCable);
}

void LayerComposition::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   float previewW = mWidth - kPanelW;
   float previewH = mHeight - kBarH;

   if (mOutputFBO && mOutputFBO->IsValid())
      mOutputFBO->Draw(0, 0, previewW, previewH);

   ofPushStyle();
   ofSetColor(20, 20, 35, 255);
   ofFill();
   ofRect(previewW, 0, kPanelW, previewH);
   ofRect(0, previewH, mWidth, kBarH);

   ofSetColor(60, 60, 80);
   ofSetLineWidth(1);
   ofLine(previewW, 0, previewW, previewH);
   ofLine(0, previewH, mWidth, previewH);

   for (int i = 0; i < kNumLayers; ++i)
   {
      ofSetColor(80, 80, 110);
      DrawTextRightJustify(("L" + ofToString(i + 1)).c_str(), 14, 24 + i * 22, 9);
   }

   if (mSelectedLayer >= 0)
   {
      float cx, cy, hw, hh, cosR, sinR;
      ComputeLayerScreenBounds(mSelectedLayer, cx, cy, hw, hh, cosR, sinR);

      float corners[4][2] = {
         { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh }
      };

      ofSetColor(255, 255, 60, 200);
      ofSetLineWidth(1.5f);
      ofNoFill();
      ofBeginShape();
      for (int c = 0; c < 4; ++c)
      {
         float sx = cx + (corners[c][0] * cosR - corners[c][1] * sinR);
         float sy = cy + (corners[c][0] * sinR + corners[c][1] * cosR);
         ofVertex(sx, sy);
      }
      ofEndShape(true);

      ofSetColor(255, 255, 255, 230);
      ofFill();
      for (int c = 0; c < 4; ++c)
      {
         float sx = cx + (corners[c][0] * cosR - corners[c][1] * sinR);
         float sy = cy + (corners[c][0] * sinR + corners[c][1] * cosR);
         ofRect(sx - kHandleSz * 0.5f, sy - kHandleSz * 0.5f, kHandleSz, kHandleSz);
      }
   }

   mSourceDropdown->Draw();
   mBlendDropdown->Draw();
   mOpacitySlider->Draw();
   mMirrorXCheck->Draw();
   mMirrorYCheck->Draw();
   mScaleXSlider->Draw();
   mScaleYSlider->Draw();
   mOffsetXSlider->Draw();
   mOffsetYSlider->Draw();
   mRotationSlider->Draw();
   mEnabledCheck->Draw();
   mOutputResDropdown->Draw();
   mCustomWidthSlider->Draw();
   mCustomHeightSlider->Draw();

   ofPopStyle();
}

void LayerComposition::PostRender()
{
   int outW = (int)mWidth;
   int outH = (int)mHeight;
   switch (mOutputRes)
   {
   case kRes_1080p: outW = 1920; outH = 1080; break;
   case kRes_720p:  outW = 1280; outH = 720; break;
   case kRes_540p:  outW = 960; outH = 540; break;
   case kRes_Custom: outW = mCustomWidth; outH = mCustomHeight; break;
   default: break;
   }
   outW = std::max(64, outW);
   outH = std::max(64, outH);

   if (!mOutputFBO || !mOutputFBO->IsValid() ||
       mOutputFBO->GetWidth() != outW || mOutputFBO->GetHeight() != outH)
   {
      delete mOutputFBO;
      mOutputFBO = new VisualFBO();
      mOutputFBO->Create(outW, outH);
   }

   if (!mOutputFBO || !mOutputFBO->IsValid())
      return;

   mOutputFBO->Bind();

   ofSetColor(0, 0, 0, 0);
   ofFill();
   ofRect(0, 0, (float)outW, (float)outH);

   for (int i = 0; i < kNumLayers; ++i)
   {
      LayerInfo& lyr = mLayers[i];
      if (!lyr.mEnabled || !lyr.mSource)
         continue;

      VisualFBO* srcFBO = lyr.mSource->GetFBO();
      if (!srcFBO || !srcFBO->IsValid())
         continue;

      float srcW = (float)srcFBO->GetWidth();
      float srcH = (float)srcFBO->GetHeight();
      if (srcW < 1.0f || srcH < 1.0f)
         continue;

      float fitScale = std::min((float)outW / srcW, (float)outH / srcH);
      float drawW = srcW * fitScale * lyr.mScaleX;
      float drawH = srcH * fitScale * lyr.mScaleY;

      float centerX = outW * 0.5f + lyr.mOffsetX * outW * 0.5f;
      float centerY = outH * 0.5f + lyr.mOffsetY * outH * 0.5f;

      if (lyr.mBlendMode == 1)
         nvgGlobalCompositeBlendFunc(gNanoVG, NVG_SRC_ALPHA, NVG_ONE);
      else if (lyr.mBlendMode == 2)
         nvgGlobalCompositeBlendFunc(gNanoVG, NVG_DST_COLOR, NVG_ZERO);
      else if (lyr.mBlendMode == 3)
         nvgGlobalCompositeBlendFunc(gNanoVG, NVG_ONE, NVG_ONE_MINUS_SRC_COLOR);

      if (lyr.mOpacity < 1.0f)
         nvgGlobalAlpha(gNanoVG, lyr.mOpacity);

      nvgSave(gNanoVG);
      nvgTranslate(gNanoVG, centerX, centerY);
      nvgRotate(gNanoVG, lyr.mRotation * ((float)M_PI / 180.0f));
      nvgScale(gNanoVG, lyr.mMirrorX ? -1.0f : 1.0f, lyr.mMirrorY ? -1.0f : 1.0f);
      nvgTranslate(gNanoVG, -drawW * 0.5f, -drawH * 0.5f);

      srcFBO->ReleaseDisplayImage();
      srcFBO->Draw(0, 0, drawW, drawH);

      nvgRestore(gNanoVG);

      if (lyr.mOpacity < 1.0f)
         nvgGlobalAlpha(gNanoVG, 1.0f);
      if (lyr.mBlendMode != 0)
         nvgGlobalCompositeOperation(gNanoVG, NVG_SOURCE_OVER);
   }

   mOutputFBO->Unbind();
}

VisualFBO* LayerComposition::GetFBO()
{
   return mOutputFBO;
}

void LayerComposition::Resize(float w, float h)
{
   mWidth = std::max(kMinWidth, w);
   mHeight = std::max(kMinHeight, h);
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth - 8, mHeight / 2);
}

void LayerComposition::PostRepatch(PatchCableSource* source, bool fromUserClick)
{
   for (int i = 0; i < kNumLayers; ++i)
   {
      if (source == mLayers[i].mInputCable)
      {
         ResolveSourceFromCable(i);
         return;
      }
   }
}

void LayerComposition::ResolveSourceFromCable(int idx)
{
   auto* cable = mLayers[idx].mInputCable;
   if (!cable || cable->GetPatchCables().empty())
   {
      mLayers[idx].mSource = nullptr;
      mLayers[idx].mSourceIndex = -1;
   }
   else
   {
      auto* target = cable->GetPatchCables()[0]->GetTarget();
      mLayers[idx].mSource = dynamic_cast<IVisualSource*>(target);

      mLayers[idx].mSourceIndex = -1;
      if (mLayers[idx].mSource)
      {
         std::vector<IDrawableModule*> allModules;
         TheSynth->GetAllModules(allModules);
         int count = 0;
         for (auto* mod : allModules)
         {
            if (mod == this) continue;
            IVisualSource* vs = dynamic_cast<IVisualSource*>(mod);
            if (vs == nullptr) continue;
            if (vs == mLayers[idx].mSource)
            {
               mLayers[idx].mSourceIndex = count;
               break;
            }
            ++count;
         }
      }
   }

   if (idx == mSelectedLayer)
      SyncEditFromLayer();
}

IVisualSource* LayerComposition::FindSourceByIndex(int idx) const
{
   if (idx < 0) return nullptr;
   std::vector<IDrawableModule*> allModules;
   TheSynth->GetAllModules(allModules);
   int count = 0;
   for (auto* mod : allModules)
   {
      if (mod == this) continue;
      IVisualSource* vs = dynamic_cast<IVisualSource*>(mod);
      if (vs == nullptr) continue;
      if (count == idx) return vs;
      ++count;
   }
   return nullptr;
}

void LayerComposition::DropdownClicked(DropdownList* list)
{
   if (list != mSourceDropdown) return;

   IVisualSource* oldSrc = nullptr;
   if (mSelectedLayer >= 0)
      oldSrc = mLayers[mSelectedLayer].mSource;

   list->Clear();
   list->AddLabel("-- none --", -1);

   std::vector<IDrawableModule*> allModules;
   TheSynth->GetAllModules(allModules);

   int mIdx = 0;
   for (auto* mod : allModules)
   {
      if (mod == this) continue;
      IVisualSource* vs = dynamic_cast<IVisualSource*>(mod);
      if (vs == nullptr) continue;
      list->AddLabel(mod->Name(), mIdx);
      if (vs == oldSrc)
         mEditSourceIndex = mIdx;
      ++mIdx;
   }
}

void LayerComposition::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mSourceDropdown || list == mBlendDropdown)
      SyncEditToLayer();
}

void LayerComposition::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   SyncEditToLayer();
}

void LayerComposition::SelectLayer(int idx)
{
   if (idx == mSelectedLayer) return;
   if (mSelectedLayer >= 0)
      SyncEditToLayer();
   mSelectedLayer = idx;
   if (idx >= 0)
      SyncEditFromLayer();
}

void LayerComposition::SyncEditFromLayer()
{
   if (mSelectedLayer < 0) return;
   auto& l = mLayers[mSelectedLayer];
   mEditSourceIndex = l.mSourceIndex;
   mEditOpacity = l.mOpacity;
   mEditBlendMode = l.mBlendMode;
   mEditScaleX = l.mScaleX;
   mEditScaleY = l.mScaleY;
   mEditOffsetX = l.mOffsetX;
   mEditOffsetY = l.mOffsetY;
   mEditRotation = l.mRotation;
   mEditMirrorX = l.mMirrorX;
   mEditMirrorY = l.mMirrorY;
   mEditEnabled = l.mEnabled;
}

void LayerComposition::SyncEditToLayer()
{
   if (mSelectedLayer < 0) return;
   auto& l = mLayers[mSelectedLayer];
   l.mSourceIndex = mEditSourceIndex;
   l.mSource = FindSourceByIndex(mEditSourceIndex);
   l.mOpacity = mEditOpacity;
   l.mBlendMode = mEditBlendMode;
   l.mScaleX = mEditScaleX;
   l.mScaleY = mEditScaleY;
   l.mOffsetX = mEditOffsetX;
   l.mOffsetY = mEditOffsetY;
   l.mRotation = mEditRotation;
   l.mMirrorX = mEditMirrorX;
   l.mMirrorY = mEditMirrorY;
   l.mEnabled = mEditEnabled;
}

void LayerComposition::ComputeLayerScreenBounds(int idx, float& cx, float& cy, float& hw, float& hh, float& cosR, float& sinR) const
{
   const auto& lyr = mLayers[idx];
   VisualFBO* srcFBO = lyr.mSource ? lyr.mSource->GetFBO() : nullptr;
   float srcW = srcFBO ? (float)srcFBO->GetWidth() : 100.0f;
   float srcH = srcFBO ? (float)srcFBO->GetHeight() : 100.0f;
   if (srcW < 1.0f) srcW = 100.0f;
   if (srcH < 1.0f) srcH = 100.0f;

   int outW = (int)mWidth;
   int outH = (int)mHeight;
   float fitScale = std::min((float)outW / srcW, (float)outH / srcH);
   float drawW = srcW * fitScale * lyr.mScaleX;
   float drawH = srcH * fitScale * lyr.mScaleY;

   float previewW = mWidth - kPanelW;
   float previewH = mHeight - kBarH;
   float sx = previewW / (float)outW;
   float sy = previewH / (float)outH;

   float centerX = outW * 0.5f + lyr.mOffsetX * outW * 0.5f;
   float centerY = outH * 0.5f + lyr.mOffsetY * outH * 0.5f;

   cx = centerX * sx;
   cy = centerY * sy;
   hw = drawW * 0.5f * sx;
   hh = drawH * 0.5f * sy;

   float rad = lyr.mRotation * ((float)M_PI / 180.0f);
   cosR = cosf(rad);
   sinR = sinf(rad);
}

int LayerComposition::HitTestLayer(float mx, float my, float& outLocalX, float& outLocalY) const
{
   float previewW = mWidth - kPanelW;
   float previewH = mHeight - kBarH;
   if (mx < 0 || mx > previewW || my < 0 || my > previewH)
      return -1;

   int outW = std::max(64, (int)mWidth);
   int outH = std::max(64, (int)mHeight);
   float sx = previewW / (float)outW;
   float sy = previewH / (float)outH;
   float fbx = mx / sx;
   float fby = my / sy;

   for (int i = kNumLayers - 1; i >= 0; --i)
   {
      const auto& lyr = mLayers[i];
      if (!lyr.mEnabled || !lyr.mSource) continue;

      VisualFBO* srcFBO = lyr.mSource->GetFBO();
      if (!srcFBO || !srcFBO->IsValid()) continue;

      float srcW = (float)srcFBO->GetWidth();
      float srcH = (float)srcFBO->GetHeight();
      if (srcW < 1.0f || srcH < 1.0f) continue;

      float fitScale = std::min((float)outW / srcW, (float)outH / srcH);
      float drawW = srcW * fitScale * lyr.mScaleX;
      float drawH = srcH * fitScale * lyr.mScaleY;

      float centerX = outW * 0.5f + lyr.mOffsetX * outW * 0.5f;
      float centerY = outH * 0.5f + lyr.mOffsetY * outH * 0.5f;

      float dx = fbx - centerX;
      float dy = fby - centerY;

      float rad = lyr.mRotation * ((float)M_PI / 180.0f);
      float cr = cosf(-rad);
      float sr = sinf(-rad);

      float lx = dx * cr - dy * sr;
      float ly = dx * sr + dy * cr;

      if (lyr.mMirrorX) lx = -lx;
      if (lyr.mMirrorY) ly = -ly;

      if (fabsf(lx) <= drawW * 0.5f && fabsf(ly) <= drawH * 0.5f)
      {
         outLocalX = lx / (drawW * 0.5f);
         outLocalY = ly / (drawH * 0.5f);
         return i;
      }
   }
   return -1;
}

int LayerComposition::HitTestHandle(float mx, float my) const
{
   if (mSelectedLayer < 0) return -1;

   float cx, cy, hw, hh, cosR, sinR;
   ComputeLayerScreenBounds(mSelectedLayer, cx, cy, hw, hh, cosR, sinR);

   float corners[4][2] = {
      { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh }
   };

   float hitR = kHandleSz + 4;
   for (int c = 0; c < 4; ++c)
   {
      float sx = cx + (corners[c][0] * cosR - corners[c][1] * sinR);
      float sy = cy + (corners[c][0] * sinR + corners[c][1] * cosR);
      float dst = sqrtf((mx - sx) * (mx - sx) + (my - sy) * (my - sy));
      if (dst <= hitR) return c;
   }
   return -1;
}

void LayerComposition::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   if (right) return;

   float previewW = mWidth - kPanelW;
   if (x > previewW || y > mHeight - kBarH)
      return;

   int handle = HitTestHandle(x, y);
   if (handle >= 0)
   {
      SelectLayer(mSelectedLayer);
      switch (handle)
      {
      case 0: mDragMode = kScaleTL; break;
      case 1: mDragMode = kScaleTR; break;
      case 2: mDragMode = kScaleBR; break;
      case 3: mDragMode = kScaleBL; break;
      }
      return;
   }

   float localX, localY;
   int hit = HitTestLayer(x, y, localX, localY);
   if (hit >= 0)
   {
      SelectLayer(hit);
      mDragMode = kMove;
   }
   else
   {
      SelectLayer(-1);
   }
}

void LayerComposition::MouseReleased()
{
   mDragMode = kNone;
   IDrawableModule::MouseReleased();
}

bool LayerComposition::MouseMoved(float x, float y)
{
   IDrawableModule::MouseMoved(x, y);

   float previewW = mWidth - kPanelW;
   float previewH = mHeight - kBarH;

   if (mDragMode == kMove && mSelectedLayer >= 0)
   {
       int outW = std::max(64, (int)mWidth);
       int outH = std::max(64, (int)mHeight);
       float sx = previewW / (float)outW;
       float sy = previewH / (float)outH;

       float fbx = x / sx;
       float fby = y / sy;

      mEditOffsetX = (fbx - outW * 0.5f) / (outW * 0.5f);
      mEditOffsetY = (fby - outH * 0.5f) / (outH * 0.5f);

      mEditOffsetX = ofClamp(mEditOffsetX, -1.0f, 1.0f);
      mEditOffsetY = ofClamp(mEditOffsetY, -1.0f, 1.0f);

      SyncEditToLayer();
      return true;
   }

   if ((mDragMode == kScaleTL || mDragMode == kScaleTR ||
        mDragMode == kScaleBL || mDragMode == kScaleBR) && mSelectedLayer >= 0)
   {
      float cx, cy, hw, hh, cosR, sinR;
      ComputeLayerScreenBounds(mSelectedLayer, cx, cy, hw, hh, cosR, sinR);

      int anchorCorner = (mDragMode == kScaleTL) ? 2 :
                          (mDragMode == kScaleTR) ? 3 :
                          (mDragMode == kScaleBR) ? 0 : 1;

      float corners[4][2] = {
         { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh }
      };

      float ax = cx + (corners[anchorCorner][0] * cosR - corners[anchorCorner][1] * sinR);
      float ay = cy + (corners[anchorCorner][0] * sinR + corners[anchorCorner][1] * cosR);

      float dx = x - ax;
      float dy = y - ay;

      float rad = mLayers[mSelectedLayer].mRotation * ((float)M_PI / 180.0f);
      float cr = cosf(-rad);
      float sr = sinf(-rad);
      float lx = fabsf(dx * cr - dy * sr);
      float ly = fabsf(dx * sr + dy * cr);

      VisualFBO* srcFBO = mLayers[mSelectedLayer].mSource ? mLayers[mSelectedLayer].mSource->GetFBO() : nullptr;
      float srcW = srcFBO ? (float)srcFBO->GetWidth() : 100.0f;
      float srcH = srcFBO ? (float)srcFBO->GetHeight() : 100.0f;

      int outW = std::max(64, (int)mWidth);
      int outH = std::max(64, (int)mHeight);
      float fitScale = std::min((float)outW / srcW, (float)outH / srcH);
      float ss = previewW / (float)outW;

      float newScaleX = (lx / ss) / (srcW * fitScale);
      float newScaleY = (ly / ss) / (srcH * fitScale);

      newScaleX = ofClamp(newScaleX, 0.1f, 3.0f);
      newScaleY = ofClamp(newScaleY, 0.1f, 3.0f);

      mEditScaleX = newScaleX;
      mEditScaleY = newScaleY;

      SyncEditToLayer();
      return true;
   }

   if (x <= previewW && y <= previewH)
   {
      int handle = HitTestHandle(x, y);
      float localX, localY;
      int hit = HitTestLayer(x, y, localX, localY);
      mHoveredLayer = (handle >= 0) ? mSelectedLayer : hit;
   }
   else
   {
      mHoveredLayer = -1;
   }

   return false;
}

void LayerComposition::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mWidth;
   out << mHeight;
   out << mSelectedLayer;
   out << mOutputRes;
   out << mCustomWidth;
   out << mCustomHeight;

   for (int i = 0; i < kNumLayers; ++i)
   {
      out << mLayers[i].mOpacity;
      out << mLayers[i].mBlendMode;
      out << mLayers[i].mSourceIndex;
      out << mLayers[i].mScaleX;
      out << mLayers[i].mScaleY;
      out << mLayers[i].mOffsetX;
      out << mLayers[i].mOffsetY;
      out << mLayers[i].mRotation;
      out << mLayers[i].mMirrorX;
      out << mLayers[i].mMirrorY;
      out << mLayers[i].mEnabled;
   }
}

void LayerComposition::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mWidth;
   in >> mHeight;
   mWidth = std::max(kMinWidth, mWidth);
   mHeight = std::max(kMinHeight, mHeight);

   if (rev >= 7)
   {
      in >> mSelectedLayer;
      in >> mOutputRes;
      in >> mCustomWidth;
      in >> mCustomHeight;
   }
   else if (rev >= 6)
   {
      in >> mOutputRes;
      in >> mCustomWidth;
      in >> mCustomHeight;
   }

   for (int i = 0; i < kNumLayers; ++i)
   {
      if (rev >= 4)
      {
         in >> mLayers[i].mOpacity;
         in >> mLayers[i].mBlendMode;
      }
      if (rev >= 5)
         in >> mLayers[i].mSourceIndex;
      else
         mLayers[i].mSourceIndex = -1;

      if (rev >= 6)
      {
         in >> mLayers[i].mScaleX;
         in >> mLayers[i].mScaleY;
         in >> mLayers[i].mOffsetX;
         in >> mLayers[i].mOffsetY;
         in >> mLayers[i].mRotation;
         in >> mLayers[i].mMirrorX;
         in >> mLayers[i].mMirrorY;
         in >> mLayers[i].mEnabled;
      }
   }

   if (mSelectedLayer >= 0)
      SyncEditFromLayer();

   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth - 8, mHeight / 2);
}

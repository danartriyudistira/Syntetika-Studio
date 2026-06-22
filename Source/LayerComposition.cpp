#include "LayerComposition.h"
#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "Profiler.h"

#include <algorithm>

#include "juce_opengl/juce_opengl.h"
using namespace juce::gl;

#include "nanovg/nanovg.h"

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg/nanovg_gl.h"

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

   for (int i = 0; i < kNumLayers; ++i)
   {
      auto& layer = mLayers[i];
      float y = 3 + i * kRowHeight;

      layer.mSourceIndex = -1;
      layer.mSourceDropdown = new DropdownList(this, ("src" + ofToString(i)).c_str(), 8, y, &layer.mSourceIndex, 140);
      AddUIControl(layer.mSourceDropdown);

      layer.mOpacitySlider = new FloatSlider(this, ("op" + ofToString(i)).c_str(), 152, y, 40, 14, &layer.mOpacity, 0, 1, 2);
      AddUIControl(layer.mOpacitySlider);

      layer.mBlendModeDropdown = new DropdownList(this, ("bl" + ofToString(i)).c_str(), 195, y, (int*)&layer.mBlendMode, 70);
      layer.mBlendModeDropdown->AddLabel("Normal", 0);
      layer.mBlendModeDropdown->AddLabel("Additive", 1);
      layer.mBlendModeDropdown->AddLabel("Multiply", 2);
      layer.mBlendModeDropdown->AddLabel("Screen", 3);
      AddUIControl(layer.mBlendModeDropdown);
   }

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mWidth - 8, mHeight / 2);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);
}

void LayerComposition::DrawModule()
{
   if (Minimized() || !mEnabled)
      return;

   ResolveSources();

   RenderComposite();

   ofPushStyle();
   ofSetColor(25, 25, 45);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   ofPushStyle();
   ofSetColor(255, 255, 0, 100);
   ofNoFill();
   ofSetLineWidth(2);
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   for (int i = 0; i < kNumLayers; ++i)
   {
      float y = 3 + i * kRowHeight;

      ofPushStyle();
      ofSetColor(45, 45, 65);
      ofNoFill();
      ofRect(1, y - 1, mWidth - 2, kRowHeight - 2);
      ofPopStyle();

      if (mLayers[i].mSource)
      {
         auto* mod = dynamic_cast<IDrawableModule*>(mLayers[i].mSource);
         if (mod)
         {
            ofPushStyle();
            ofSetColor(100, 220, 100);
            DrawTextNormal(mod->Name(), 270, y + 2, 11);
            ofPopStyle();
         }
      }
   }
}

void LayerComposition::PostRender()
{
   ++mPostRenderCount;
}

void LayerComposition::Resize(float w, float h)
{
   mWidth = std::max(kMinWidth, w);
   mHeight = std::max(kMinHeight, h);
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth - 8, mHeight / 2);
}

namespace
{
   IVisualSource* FindSourceByIndex(int index)
   {
      if (index < 0)
         return nullptr;
      std::vector<IDrawableModule*> allModules;
      TheSynth->GetAllModules(allModules);
      int idx = 0;
      for (auto* mod : allModules)
      {
         if (dynamic_cast<LayerComposition*>(mod))
            continue;
         auto* vis = dynamic_cast<IVisualSource*>(mod);
         if (!vis)
            continue;
         if (idx == index)
            return vis;
         ++idx;
      }
      return nullptr;
   }
}

void LayerComposition::DropdownClicked(DropdownList* list)
{
   for (int i = 0; i < kNumLayers; ++i)
   {
      auto& layer = mLayers[i];
      if (list != layer.mSourceDropdown)
         continue;

      // Remember current source by pointer (not index)
      IVisualSource* currentSource = layer.mSource;
      layer.mSourceDropdown->Clear();

      // Rebuild list with all IVisualSource modules
      std::vector<IDrawableModule*> allModules;
      TheSynth->GetAllModules(allModules);
      int newIdx = -1;
      int idx = 0;
      for (auto* mod : allModules)
      {
         if (dynamic_cast<LayerComposition*>(mod))
            continue;
         auto* vis = dynamic_cast<IVisualSource*>(mod);
         if (!vis)
            continue;
         layer.mSourceDropdown->AddLabel(mod->Name(), idx);
         if (vis == currentSource)
            newIdx = idx;
         ++idx;
      }

      // Set index directly without triggering DropdownUpdated
      layer.mSourceIndex = newIdx;
      break;
   }
}

void LayerComposition::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   for (int i = 0; i < kNumLayers; ++i)
   {
      auto& layer = mLayers[i];
      if (list != layer.mSourceDropdown)
         continue;
      layer.mSource = FindSourceByIndex(layer.mSourceIndex);
      break;
   }
}

void LayerComposition::ResolveSources()
{
   for (int i = 0; i < kNumLayers; ++i)
      mLayers[i].mSource = FindSourceByIndex(mLayers[i].mSourceIndex);
}

void LayerComposition::RenderComposite()
{
   if (!mOutputFBO)
      mOutputFBO = new VisualFBO();
   if (mOutputFBO->GetWidth() != 640 || mOutputFBO->GetHeight() != 480)
      mOutputFBO->Create(640, 480);

   mOutputFBO->Bind();

   nvgBeginPath(gNanoVG);
   nvgRect(gNanoVG, 0, 0, 640, 480);
   nvgFillColor(gNanoVG, nvgRGBA(60, 60, 120, 255));
   nvgFill(gNanoVG);

   std::vector<int> tempImages;

   for (int i = 0; i < kNumLayers; ++i)
   {
      auto& layer = mLayers[i];
      if (!layer.mSource)
         continue;
      auto* srcFBO = layer.mSource->GetFBO();
      if (!srcFBO || !srcFBO->IsValid())
         continue;

      float srcW = (float)srcFBO->GetWidth();
      float srcH = (float)srcFBO->GetHeight();
      if (srcW <= 0 || srcH <= 0)
         continue;

      auto pixels = srcFBO->ReadPixels();
      if (pixels.empty())
         continue;

      int w = (int)srcW;
      int h = (int)srcH;

      int imgHandle = nvgCreateImageRGBA(gNanoVG, w, h, 0, pixels.data());
      if (imgHandle < 0)
         continue;
      tempImages.push_back(imgHandle);

      float scaleX = 640.0f / w;
      float scaleY = 480.0f / h;
      float fitScale = std::min(scaleX, scaleY);
      float drawW = w * fitScale;
      float drawH = h * fitScale;
      float offsetX = (640 - drawW) / 2;
      float offsetY = (480 - drawH) / 2;

      NVGpaint paint = nvgImagePattern(gNanoVG, offsetX, offsetY, drawW, drawH, 0.0f, imgHandle, layer.mOpacity);
      nvgBeginPath(gNanoVG);
      nvgRect(gNanoVG, offsetX, offsetY, drawW, drawH);
      nvgFillPaint(gNanoVG, paint);
      nvgFill(gNanoVG);
   }

   NVGcontext* outputNvg = mOutputFBO->GetNVGContext();
   mOutputFBO->Unbind();

   for (int h : tempImages)
      nvgDeleteImage(outputNvg, h);
}

VisualFBO* LayerComposition::GetFBO()
{
   return mOutputFBO;
}

void LayerComposition::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mWidth;
   out << mHeight;
   for (int i = 0; i < kNumLayers; ++i)
   {
      out << mLayers[i].mOpacity;
      out << mLayers[i].mBlendMode;
      out << mLayers[i].mSourceIndex;
   }
}

void LayerComposition::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mWidth;
   in >> mHeight;
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
   }
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth - 8, mHeight / 2);
}

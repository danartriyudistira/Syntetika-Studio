#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "PatchCableSource.h"

class VisualFBO;

class LayerComposition : public IDrawableModule, public IVisualSource, public IFloatSliderListener, public IDropdownListener
{
public:
   LayerComposition();
   virtual ~LayerComposition();
   static IDrawableModule* Create() { return new LayerComposition(); }
   static bool CanCreate() { return true; }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void DrawModule() override;
   void PostRender() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override { w = mWidth; h = mHeight; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void DropdownClicked(DropdownList* list) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   VisualFBO* GetFBO() override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 5; }

private:
   struct LayerInfo
   {
      DropdownList* mSourceDropdown{ nullptr };
      IVisualSource* mSource{ nullptr };
      DropdownList* mBlendModeDropdown{ nullptr };
      FloatSlider* mOpacitySlider{ nullptr };
      float mOpacity{ 1.0f };
      int mBlendMode{ 0 };
      int mSourceIndex{ -1 };
   };

   static constexpr int kNumLayers = 4;
   static constexpr float kRowHeight = 28;
   static constexpr float kMinWidth = 200;
   static constexpr float kMinHeight = 150;

   void ResolveSources();
   void RenderComposite();

   LayerInfo mLayers[kNumLayers];
   PatchCableSource* mOutputCable{ nullptr };
   VisualFBO* mOutputFBO{ nullptr };
   float mWidth{ kMinWidth };
   float mHeight{ kMinHeight };
   int mPostRenderCount{ 0 };
};

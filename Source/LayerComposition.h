#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "Checkbox.h"
#include "PatchCableSource.h"

class VisualFBO;

class LayerComposition : public IDrawableModule,
                         public IVisualSource,
                         public IFloatSliderListener,
                         public IIntSliderListener,
                         public IDropdownListener
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

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override { w = mWidth; h = mHeight; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override {}
   void DropdownClicked(DropdownList* list) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void OnClicked(float x, float y, bool right) override;
   void MouseReleased() override;
   bool MouseMoved(float x, float y) override;

   void PostRepatch(PatchCableSource* source, bool fromUserClick) override;

   VisualFBO* GetFBO() override;
   void PostRender() override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 7; }

private:
   enum OutputRes { kRes_Source, kRes_1080p, kRes_720p, kRes_540p, kRes_Custom };
   enum DragMode { kNone, kMove, kScaleTL, kScaleTR, kScaleBL, kScaleBR };

   struct LayerInfo
   {
      IVisualSource* mSource{ nullptr };
      PatchCableSource* mInputCable{ nullptr };
      int mSourceIndex{ -1 };
      float mOpacity{ 1.0f };
      int mBlendMode{ 0 };
      bool mEnabled{ true };
      float mScaleX{ 1.0f };
      float mScaleY{ 1.0f };
      float mOffsetX{ 0.0f };
      float mOffsetY{ 0.0f };
      float mRotation{ 0.0f };
      bool mMirrorX{ false };
      bool mMirrorY{ false };
   };

   void DrawModule() override;
   void ResolveSourceFromCable(int idx);
   IVisualSource* FindSourceByIndex(int idx) const;
   void SelectLayer(int idx);
   void SyncEditFromLayer();
   void SyncEditToLayer();
   int HitTestLayer(float mx, float my, float& outLocalX, float& outLocalY) const;
   int HitTestHandle(float mx, float my) const;
   void ComputeLayerScreenBounds(int idx, float& cx, float& cy, float& hw, float& hh, float& cosR, float& sinR) const;

   static constexpr int kNumLayers = 4;
   static constexpr float kPanelW = 126;
   static constexpr float kBarH = 24;
   static constexpr float kHandleSz = 7;
   static constexpr float kMinWidth = 380;
   static constexpr float kMinHeight = 280;

   LayerInfo mLayers[kNumLayers];

   int mSelectedLayer{ -1 };
   int mHoveredLayer{ -1 };
   DragMode mDragMode{ kNone };

   int mEditSourceIndex{ -1 };
   float mEditOpacity{ 1.0f };
   int mEditBlendMode{ 0 };
   float mEditScaleX{ 1.0f }, mEditScaleY{ 1.0f };
   float mEditOffsetX{ 0.0f }, mEditOffsetY{ 0.0f };
   float mEditRotation{ 0.0f };
   bool mEditMirrorX{ false }, mEditMirrorY{ false };
   bool mEditEnabled{ true };

   DropdownList* mSourceDropdown{ nullptr };
   FloatSlider* mOpacitySlider{ nullptr };
   DropdownList* mBlendDropdown{ nullptr };
   FloatSlider* mScaleXSlider{ nullptr };
   FloatSlider* mScaleYSlider{ nullptr };
   FloatSlider* mOffsetXSlider{ nullptr };
   FloatSlider* mOffsetYSlider{ nullptr };
   FloatSlider* mRotationSlider{ nullptr };
   Checkbox* mMirrorXCheck{ nullptr };
   Checkbox* mMirrorYCheck{ nullptr };
   Checkbox* mEnabledCheck{ nullptr };

   int mOutputRes{ kRes_Source };
   int mCustomWidth{ 1920 };
   int mCustomHeight{ 1080 };
   DropdownList* mOutputResDropdown{ nullptr };
   IntSlider* mCustomWidthSlider{ nullptr };
   IntSlider* mCustomHeightSlider{ nullptr };

   PatchCableSource* mOutputCable{ nullptr };
   VisualFBO* mOutputFBO{ nullptr };

   float mWidth{ kMinWidth };
   float mHeight{ kMinHeight };
};

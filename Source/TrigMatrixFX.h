#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "INoteReceiver.h"
#include "IPulseReceiver.h"
#include "DropdownList.h"
#include "Slider.h"
#include "ClickButton.h"
#include "Checkbox.h"
#include "PatchCableSource.h"
#include "TextEntry.h"
#include "exprtk.hpp"
#include <mutex>

#define TRIGMATRIXFX_COLS 4
#define TRIGMATRIXFX_ROWS 3
#define TRIGMATRIXFX_CELLS (TRIGMATRIXFX_COLS * TRIGMATRIXFX_ROWS)
#define TRIGMATRIXFX_CUSTOM_TYPE TRIGMATRIXFX_CELLS

class VisualFBO;

class TrigMatrixFX : public IDrawableModule, public IVisualSource, public INoteReceiver, public IPulseReceiver,
                     public IDropdownListener, public IFloatSliderListener, public IButtonListener, public ITextEntryListener
{
public:
   TrigMatrixFX();
   ~TrigMatrixFX();
   static IDrawableModule* Create() { return new TrigMatrixFX(); }
   static bool CanCreate() { return true; }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return true; }

   void CreateUIControls() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& width, float& height) override;

   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void ButtonClicked(ClickButton* button, double time) override;
   void OnClicked(float x, float y, bool right) override;
   void PostRepatch(PatchCableSource* source, bool fromUserClick) override;

   //INoteReceiver
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void SendMidi(const juce::MidiMessage& message) override {}

   //IPulseReceiver
   void OnPulse(double time, float velocity, int flags) override;

   //IVisualSource
   VisualFBO* GetFBO() override;

   //ITextEntryListener
   void TextEntryComplete(TextEntry* entry) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 2; }

   bool IsEnabled() const override { return mEnabled; }

private:
   void DrawModule() override;
   void PostRender() override;

   struct FXCell
   {
      int mEffectType{ 0 };
      float mTriggerFlash{ 0 };
      double mTriggerTime{ 0 };
      float mParam1{ 0.5f };
      float mParam2{ 0.5f };
      float mParam3{ 0.5f };
      float mParam4{ 0.5f };
      std::string mCustomCode;
      float mExprX{ 0 };
      float mExprY{ 0 };
      float mExprW{ 0 };
      float mExprH{ 0 };
   };

   void SetActiveCell(int idx);
   void DrawCellEffect(int idx, float x, float y, float w, float h);
   void DrawCustomEffect(int idx, float x, float y, float w, float h, const FXCell& cell);
   void CompileExpression(int idx);
   void DrawFX0_Pulse(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX1_Bars(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX2_Sparkle(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX3_Wave(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX4_Ripple(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX5_Tunnel(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX6_Glitch(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX7_Scanline(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX8_Plasma(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX9_Strobe(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX10_Spiral(float x, float y, float w, float h, const FXCell& cell);
   void DrawFX11_Noise(float x, float y, float w, float h, const FXCell& cell);
   static void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b);

   float mModuleWidth{ 400 };
   float mModuleHeight{ 380 };
   static constexpr float kMinWidth = 280;
   static constexpr float kMinHeight = 300;
   static constexpr float kHeaderH = 18;
   static constexpr float kPad = 2;

   FXCell mCells[TRIGMATRIXFX_CELLS];
   int mActiveCell{ -1 };

   // compiled expressions for custom code (per cell)
   exprtk::symbol_table<float> mCustomSym[TRIGMATRIXFX_CELLS];
   exprtk::expression<float> mCustomExpr[TRIGMATRIXFX_CELLS];
   bool mCodeValid[TRIGMATRIXFX_CELLS]{};

   // UI controls (shown per active cell)
   DropdownList* mEffectDropdown{ nullptr };
   FloatSlider* mParam1Slider{ nullptr };
   FloatSlider* mParam2Slider{ nullptr };
   FloatSlider* mParam3Slider{ nullptr };
   FloatSlider* mParam4Slider{ nullptr };
   TextEntry* mCodeEntry{ nullptr };
   bool mTriggerAll{ false };

   std::string mCurrentCode;
   static const char* sEffectNames[TRIGMATRIXFX_CELLS + 1];
   static const char* sParamNames[4];

   PatchCableSource* mInputCable{ nullptr };
   IVisualSource* mSource{ nullptr };

   mutable std::recursive_mutex mDataMutex;
   double mTime{ 0 };
   float mTimeFloat{ 0 };
   VisualFBO* mFBO{ nullptr };
};

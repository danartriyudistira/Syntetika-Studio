#pragma once

#include <vector>
#include <string>
#include <cstdint>

class GIFAnimator
{
public:
   GIFAnimator();
   ~GIFAnimator();

   bool Load(const std::string& path);
   void Reset();

   int GetNumFrames() const { return (int)mFrames.size(); }
   int GetCanvasWidth() const { return mCanvasWidth; }
   int GetCanvasHeight() const { return mCanvasHeight; }

   struct FrameData
   {
      int DelayMs;
      int PositionX;
      int PositionY;
      int Width;
      int Height;
      std::vector<uint8_t> RGBA;
   };

   const FrameData& GetFrame(int index) const;
   const uint8_t* GetFrameRGBA(int index) const;
   int GetFrameDelay(int index) const;

private:
   struct PaletteEntry { uint8_t r, g, b; };
   struct FrameInfo
   {
      int delayCs;
      int posX, posY;
      int width, height;
      bool interlace;
      bool hasLocalPalette;
      int localPaletteSize;
      int transparentIndex;
      int lzwMinCodeSize;
      int dataOffset;
   };

   bool ParseHeader();
   bool ParseFrames();
   bool ReadPalette(std::vector<PaletteEntry>& pal, int size);
   bool ReadDataBlock(uint8_t* dest, int& outLen);
   bool LZWDecompress(const FrameInfo& finfo, const std::vector<PaletteEntry>& globalPal, int globalPalSize, bool hasGlobalPal, std::vector<uint8_t>& outRGBA);

   std::vector<uint8_t> mFileData;
   int mReadPos;
   int mCanvasWidth;
   int mCanvasHeight;
   bool mHasGlobalPalette;
   int mGlobalPaletteSize;
   std::vector<PaletteEntry> mGlobalPalette;
   int mBgColorIndex;
   std::vector<FrameInfo> mFrameInfos;
   std::vector<FrameData> mFrames;
};

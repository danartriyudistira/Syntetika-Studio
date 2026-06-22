#include "GIFAnimator.h"
#include <cstring>
#include <algorithm>

GIFAnimator::GIFAnimator()
   : mReadPos(0)
   , mCanvasWidth(0)
   , mCanvasHeight(0)
   , mHasGlobalPalette(false)
   , mGlobalPaletteSize(0)
   , mBgColorIndex(0)
{
}

GIFAnimator::~GIFAnimator()
{
}

void GIFAnimator::Reset()
{
   mFileData.clear();
   mReadPos = 0;
   mCanvasWidth = 0;
   mCanvasHeight = 0;
   mHasGlobalPalette = false;
   mGlobalPaletteSize = 0;
   mGlobalPalette.clear();
   mBgColorIndex = 0;
   mFrameInfos.clear();
   mFrames.clear();
}

static uint16_t ReadLE16(const uint8_t* p)
{
   return (uint16_t)(p[0] | (p[1] << 8));
}

bool GIFAnimator::Load(const std::string& path)
{
   Reset();

   FILE* f = nullptr;
   if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
      return false;

   fseek(f, 0, SEEK_END);
   long len = ftell(f);
   if (len <= 0) { fclose(f); return false; }
   fseek(f, 0, SEEK_SET);

   mFileData.resize((size_t)len);
   if (fread(mFileData.data(), 1, (size_t)len, f) != (size_t)len)
   {
      fclose(f);
      Reset();
      return false;
   }
   fclose(f);

   if (!ParseHeader())
   {
      Reset();
      return false;
   }

   if (!ParseFrames())
   {
      Reset();
      return false;
   }

   if (mFrameInfos.empty())
   {
      Reset();
      return false;
   }

   // Decompress all frames
   for (size_t i = 0; i < mFrameInfos.size(); ++i)
   {
      FrameData fd;
      fd.DelayMs = mFrameInfos[i].delayCs * 10;
      if (fd.DelayMs < 10) fd.DelayMs = 10;
      fd.PositionX = mFrameInfos[i].posX;
      fd.PositionY = mFrameInfos[i].posY;
      fd.Width = mFrameInfos[i].width;
      fd.Height = mFrameInfos[i].height;
      fd.RGBA.resize(mCanvasWidth * mCanvasHeight * 4, 0);

      if (!LZWDecompress(mFrameInfos[i], mGlobalPalette, mGlobalPaletteSize, mHasGlobalPalette, fd.RGBA))
      {
         Reset();
         return false;
      }

      mFrames.push_back(std::move(fd));
   }

   return true;
}

bool GIFAnimator::ParseHeader()
{
   if (mFileData.size() < 13)
      return false;

   // Check GIF header
   if (std::memcmp(mFileData.data(), "GIF87a", 6) != 0 &&
       std::memcmp(mFileData.data(), "GIF89a", 6) != 0)
      return false;

   mReadPos = 6;

   // Logical Screen Descriptor
   mCanvasWidth = ReadLE16(mFileData.data() + mReadPos);
   mCanvasHeight = ReadLE16(mFileData.data() + mReadPos + 2);
   mReadPos += 4;
   if (mCanvasWidth <= 0 || mCanvasHeight <= 0)
      return false;

   uint8_t packed = mFileData[mReadPos++];
   mBgColorIndex = mFileData[mReadPos++];
   mReadPos++; // pixel aspect ratio

   mHasGlobalPalette = (packed & 0x80) != 0;
   if (mHasGlobalPalette)
   {
      mGlobalPaletteSize = 2 << (packed & 7);
      if (!ReadPalette(mGlobalPalette, mGlobalPaletteSize))
         return false;
   }

   return true;
}

bool GIFAnimator::ParseFrames()
{
   int transparent = -1;
   int delayCs = 10;

   while (mReadPos < (int)mFileData.size())
   {
      uint8_t blockType = mFileData[mReadPos++];

      if (blockType == 0x3B) // Trailer
         break;

      if (blockType == 0x21) // Extension
      {
         if (mReadPos >= (int)mFileData.size())
            return false;
         uint8_t extType = mFileData[mReadPos++];

         if (extType == 0xF9) // Graphics Control Extension
         {
            uint8_t blockSize = mFileData[mReadPos++];
            if (mReadPos + blockSize > (int)mFileData.size())
               return false;

            uint8_t flags = mFileData[mReadPos];
            transparent = (flags & 1) ? mFileData[mReadPos + 3] : -1;
            delayCs = ReadLE16(mFileData.data() + mReadPos + 1);
            if (delayCs == 0) delayCs = 10;

            mReadPos += blockSize;
            // Skip block terminator (0x00)
            if (mReadPos < (int)mFileData.size() && mFileData[mReadPos] == 0)
               mReadPos++;
         }
         else
         {
            // Skip sub-blocks
            while (mReadPos < (int)mFileData.size())
            {
               int n = mFileData[mReadPos++];
               if (n == 0) break;
               mReadPos += n;
               if (mReadPos > (int)mFileData.size())
                  return false;
            }
         }
         continue;
      }

      if (blockType == 0x2C) // Image Descriptor
      {
         if (mReadPos + 8 > (int)mFileData.size())
            return false;

         FrameInfo fi;
         fi.posX = ReadLE16(mFileData.data() + mReadPos);
         fi.posY = ReadLE16(mFileData.data() + mReadPos + 2);
         fi.width = ReadLE16(mFileData.data() + mReadPos + 4);
         fi.height = ReadLE16(mFileData.data() + mReadPos + 6);
         mReadPos += 8;

         if (fi.width <= 0 || fi.height <= 0)
            return false;

         uint8_t imgPacked = mFileData[mReadPos++];
         fi.interlace = (imgPacked & 0x40) != 0;
         fi.hasLocalPalette = (imgPacked & 0x80) != 0;
         fi.localPaletteSize = 0;
         fi.transparentIndex = transparent;
         fi.delayCs = delayCs;

         if (fi.hasLocalPalette)
         {
            fi.localPaletteSize = 2 << (imgPacked & 7);
            // Skip local palette
            mReadPos += fi.localPaletteSize * 3;
            if (mReadPos > (int)mFileData.size())
               return false;
         }

         if (mReadPos >= (int)mFileData.size())
            return false;

         fi.lzwMinCodeSize = mFileData[mReadPos++];

         fi.dataOffset = mReadPos;

         // Skip image data sub-blocks
         while (mReadPos < (int)mFileData.size())
         {
            int n = mFileData[mReadPos++];
            if (n == 0) break;
            mReadPos += n;
            if (mReadPos > (int)mFileData.size())
               return false;
         }

         mFrameInfos.push_back(fi);

         // Reset per-frame state
         transparent = -1;
         delayCs = 10;
         continue;
      }

      // Unknown block type, skip
      continue;
   }

   return !mFrameInfos.empty();
}

bool GIFAnimator::ReadPalette(std::vector<PaletteEntry>& pal, int size)
{
   pal.resize(size);
   if (mReadPos + size * 3 > (int)mFileData.size())
      return false;
   for (int i = 0; i < size; ++i)
   {
      pal[i].r = mFileData[mReadPos++];
      pal[i].g = mFileData[mReadPos++];
      pal[i].b = mFileData[mReadPos++];
   }
   return true;
}

bool GIFAnimator::ReadDataBlock(uint8_t* dest, int& outLen)
{
   if (mReadPos >= (int)mFileData.size())
      return false;
   outLen = mFileData[mReadPos++];
   if (outLen == 0)
      return true;
   if (mReadPos + outLen > (int)mFileData.size())
      return false;
   std::memcpy(dest, mFileData.data() + mReadPos, outLen);
   mReadPos += outLen;
   return true;
}

struct LZWDecoder
{
   int table0[4096], table1[4096];
   int stack[8192];
   int* sp;

   int setCodeSize, codeSize, clearCode, endCode, maxCodeSize, maxCode;
   int currentBit, lastBit, lastByteIndex;
   uint8_t buffer[260];
   bool fresh, finished, dataBlockIsZero;

   int xpos, ypos;
   int firstcode, oldcode;

   int frameW, frameH;
   bool interlace;
   int yStep, pass;
   int canvasW;

   const std::vector<uint8_t>& fileData;
   int& readPos;

   LZWDecoder(const std::vector<uint8_t>& fd, int& rp) : fileData(fd), readPos(rp) {}

   int ReadDataBlock(uint8_t* dest)
   {
      if (readPos < 0 || readPos >= (int)fileData.size()) return -1;
      int n = fileData[readPos++];
      if (n == 0) { dataBlockIsZero = true; return 0; }
      if (readPos + n > (int)fileData.size()) return -1;
      std::memcpy(dest, fileData.data() + readPos, n);
      readPos += n;
      return n;
   }

   int GetCode(int cs, bool init)
   {
      if (init)
      {
         currentBit = 0;
         lastBit = 0;
         finished = false;
         return 0;
      }
      if ((currentBit + cs) >= lastBit)
      {
         if (finished) return -1;
         buffer[0] = buffer[std::max(0, lastByteIndex - 2)];
         buffer[1] = buffer[std::max(0, lastByteIndex - 1)];
         int n = ReadDataBlock(buffer + 2);
         if (n < 0) return -1;
         if (n == 0) finished = true;
         lastByteIndex = 2 + n;
         currentBit = (currentBit - lastBit) + 16;
         lastBit = (2 + n) * 8;
      }
      int result = 0;
      int i = currentBit;
      for (int j = 0; j < cs; ++j)
      {
         result |= ((buffer[i >> 3] >> (i & 7)) & 1) << j;
         ++i;
      }
      currentBit += cs;
      return result;
   }

   void ClearTable()
   {
      for (int i = 0; i < clearCode; ++i)
      {
         table0[i] = 0;
         table1[i] = i;
      }
      for (int i = clearCode; i < 4096; ++i)
      {
         table0[i] = 0;
         table1[i] = 0;
      }
   }

   int ReadLZWByte()
   {
      if (fresh)
      {
         fresh = false;
         for (;;)
         {
            int code = GetCode(codeSize, false);
            if (code < 0) return -1;
            if (code != clearCode)
            {
               firstcode = code;
               oldcode = code;
               return firstcode;
            }
         }
      }

      if (sp > stack)
         return *--sp;

      for (;;)
      {
         int code = GetCode(codeSize, false);
         if (code < 0) return code;

         if (code == clearCode)
         {
            ClearTable();
            codeSize = setCodeSize + 1;
            maxCodeSize = 2 * clearCode;
            maxCode = clearCode + 2;
            sp = stack;
            for (;;)
            {
               int fc = GetCode(codeSize, false);
               if (fc < 0) return fc;
               if (fc != clearCode)
               {
                  oldcode = fc;
                  firstcode = fc;
                  return firstcode;
               }
            }
         }

         if (code == endCode)
         {
            if (dataBlockIsZero) return -2;
            uint8_t buf[260];
            int n;
            while ((n = ReadDataBlock(buf)) > 0) {}
            return -2;
         }

         int incode = code;

         if (code >= maxCode)
         {
            *sp++ = firstcode;
            code = oldcode;
         }

         while (code >= clearCode)
         {
            *sp++ = table1[code];
            if (code == table0[code]) return -2;
            code = table0[code];
         }

         *sp++ = firstcode = table1[code];

         if ((code = maxCode) < 4096)
         {
            table0[code] = oldcode;
            table1[code] = firstcode;
            ++maxCode;
            if (maxCode >= maxCodeSize && maxCodeSize < 4096)
            {
               maxCodeSize <<= 1;
               ++codeSize;
            }
         }

         oldcode = incode;

         if (sp > stack)
            return *--sp;
      }
   }
};

bool GIFAnimator::LZWDecompress(const FrameInfo& finfo, const std::vector<PaletteEntry>& globalPal, int globalPalSize, bool hasGlobalPal, std::vector<uint8_t>& outRGBA)
{
   int savedPos = mReadPos;
   mReadPos = finfo.dataOffset;

   struct { uint8_t r, g, b, a; } pal[256];
   int palSize = 0;

   if (finfo.hasLocalPalette)
   {
      palSize = finfo.localPaletteSize;
      int localPalPos = finfo.dataOffset - finfo.lzwMinCodeSize - 1 - finfo.localPaletteSize * 3;
      if (localPalPos < 0) { mReadPos = savedPos; return false; }
      for (int i = 0; i < palSize; ++i)
      {
         pal[i].r = mFileData[localPalPos + i * 3];
         pal[i].g = mFileData[localPalPos + i * 3 + 1];
         pal[i].b = mFileData[localPalPos + i * 3 + 2];
         pal[i].a = (i == finfo.transparentIndex) ? 0 : 255;
      }
   }
   else if (hasGlobalPal)
   {
      palSize = globalPalSize;
      for (int i = 0; i < palSize; ++i)
      {
         pal[i].r = globalPal[i].r;
         pal[i].g = globalPal[i].g;
         pal[i].b = globalPal[i].b;
         pal[i].a = (i == finfo.transparentIndex) ? 0 : 255;
      }
   }
   else
   {
      mReadPos = savedPos;
      return false;
   }

   int frameW = finfo.width;
   int frameH = finfo.height;
   int canvasW = mCanvasWidth;

   std::vector<uint8_t> framePixels(frameW * frameH * 4, 0);

   LZWDecoder dec(mFileData, mReadPos);
   dec.setCodeSize = finfo.lzwMinCodeSize;
   dec.codeSize = dec.setCodeSize + 1;
   dec.clearCode = 1 << dec.setCodeSize;
   dec.endCode = dec.clearCode + 1;
   dec.maxCodeSize = 2 * dec.clearCode;
   dec.maxCode = dec.clearCode + 2;
   dec.currentBit = 0;
   dec.lastBit = 0;
   dec.lastByteIndex = 0;
   dec.fresh = true;
   dec.finished = false;
   dec.dataBlockIsZero = false;
   dec.sp = dec.stack;
   dec.xpos = 0;
   dec.ypos = 0;
   dec.frameW = frameW;
   dec.frameH = frameH;
   dec.interlace = finfo.interlace;
   dec.yStep = 8;
   dec.pass = 0;
   dec.canvasW = canvasW;

   dec.GetCode(0, true);
   dec.fresh = true;
   dec.ClearTable();
   dec.sp = dec.stack;

   for (;;)
   {
      int index = dec.ReadLZWByte();
      if (index < 0) break;

      int pixelIdx = (dec.ypos * frameW + dec.xpos);
      if (pixelIdx >= 0 && pixelIdx < frameW * frameH && index < palSize)
      {
         framePixels[pixelIdx * 4 + 0] = pal[index].r;
         framePixels[pixelIdx * 4 + 1] = pal[index].g;
         framePixels[pixelIdx * 4 + 2] = pal[index].b;
         framePixels[pixelIdx * 4 + 3] = pal[index].a;
      }

      if (++dec.xpos == frameW)
      {
         dec.xpos = 0;
         if (finfo.interlace)
         {
            dec.ypos += dec.yStep;
            while (dec.ypos >= frameH)
            {
               switch (++dec.pass)
               {
                  case 1: dec.ypos = 4; dec.yStep = 8; break;
                  case 2: dec.ypos = 2; dec.yStep = 4; break;
                  case 3: dec.ypos = 1; dec.yStep = 2; break;
                  default: goto decompressDone;
               }
            }
         }
         else
         {
            if (++dec.ypos >= frameH)
               break;
         }
      }

      // Handle transparent index: if we set palette entry with A=0, skip the composite
      // This is already handled by the color table setup above
   }

decompressDone:
   for (int y = 0; y < frameH; ++y)
   {
      for (int x = 0; x < frameW; ++x)
      {
         int srcIdx = (y * frameW + x) * 4;
         uint8_t a = framePixels[srcIdx + 3];
         int dstX = finfo.posX + x;
         int dstY = finfo.posY + y;
         if (dstX < 0 || dstX >= canvasW || dstY < 0 || dstY >= mCanvasHeight)
            continue;

         int dstIdx = (dstY * canvasW + dstX) * 4;
         if (a == 255)
         {
            outRGBA[dstIdx + 0] = framePixels[srcIdx + 0];
            outRGBA[dstIdx + 1] = framePixels[srcIdx + 1];
            outRGBA[dstIdx + 2] = framePixels[srcIdx + 2];
            outRGBA[dstIdx + 3] = 255;
         }
         else if (a > 0)
         {
            float f = a / 255.0f;
            outRGBA[dstIdx + 0] = (uint8_t)(framePixels[srcIdx + 0] * f + outRGBA[dstIdx + 0] * (1 - f));
            outRGBA[dstIdx + 1] = (uint8_t)(framePixels[srcIdx + 1] * f + outRGBA[dstIdx + 1] * (1 - f));
            outRGBA[dstIdx + 2] = (uint8_t)(framePixels[srcIdx + 2] * f + outRGBA[dstIdx + 2] * (1 - f));
            outRGBA[dstIdx + 3] = 255;
         }
      }
   }

   mReadPos = savedPos;
   return true;
}

const GIFAnimator::FrameData& GIFAnimator::GetFrame(int index) const
{
   return mFrames[index];
}

const uint8_t* GIFAnimator::GetFrameRGBA(int index) const
{
   return mFrames[index].RGBA.data();
}

int GIFAnimator::GetFrameDelay(int index) const
{
   return mFrames[index].DelayMs;
}

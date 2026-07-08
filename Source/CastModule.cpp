#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#include "CastModule.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "OpenFrameworksPort.h"
#include "UIControlMacros.h"
#include "VisualFBO.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"
#include "SSDPDiscoverer.h"

#include <cstdio>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>

// ---- LoadLayout must come before CastChannel.h to avoid macro pollution ----

void CastModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadInt("outputRes", moduleInfo, kOutputRes_720p, 0, 4);
   mModuleSaveData.LoadFloat("framerate", moduleInfo, 30, 1, 60);
   mModuleSaveData.LoadFloat("width", moduleInfo, 320);
   mModuleSaveData.LoadFloat("height", moduleInfo, 300);
   if (moduleInfo["targetIP"].isString())
      mTargetIP = moduleInfo["targetIP"].asString();
   if (!moduleInfo["targetPort"].isString())
      mTargetPort = "8009";
   else
      mTargetPort = moduleInfo["targetPort"].asString();
   SetUpFromSaveData();
}

void CastModule::SetUpFromSaveData()
{
   mWidth = mModuleSaveData.GetFloat("width");
   mHeight = mModuleSaveData.GetFloat("height");
   mOutputRes = mModuleSaveData.GetInt("outputRes");
   mFramerate = mModuleSaveData.GetFloat("framerate");
}

// ---- Include CastChannel/CastHTTPServer now (defines LoadString macro) ----

#include "CastChannel.h"
#include "CastHTTPServer.h"

CastModule::CastModule()
{
}

CastModule::~CastModule()
{
   StopStream();
   delete mCastChannel;
   delete mHttpServer;
}

void CastModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mInputCable = new PatchCableSource(this, kConnectionType_Special);
   AddPatchCableSource(mInputCable);

   UIBLOCK0();
   TEXTENTRY(mTargetIPEntry, "target IP", 15, &mTargetIP);
   UIBLOCK_SHIFTRIGHT();
   TEXTENTRY(mTargetPortEntry, "port", 5, &mTargetPort);
   ENDUIBLOCK0();

   UIBLOCK(3, 28, 200);
   BUTTON(mDiscoverButton, "discover");
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mDeviceDropdown, "device", &mSelectedDeviceIndex, 130);
   mDeviceDropdown->AddLabel("-- manual --", -1);
   ENDUIBLOCK0();

   UIBLOCK(3, 52, 200);
   DROPDOWN(mOutputResDropdown, "output res", &mOutputRes, 130);
   mOutputResDropdown->AddLabel("source", kOutputRes_Source);
   mOutputResDropdown->AddLabel("1920x1080", kOutputRes_1080p);
   mOutputResDropdown->AddLabel("1280x720", kOutputRes_720p);
   mOutputResDropdown->AddLabel("854x480", kOutputRes_480p);
   mOutputResDropdown->AddLabel("640x360", kOutputRes_360p);
   ENDUIBLOCK0();

   UIBLOCK(3, 76, 200);
   FLOATSLIDER(mFramerateSlider, "fps", &mFramerate, 1, 60);
   ENDUIBLOCK0();

   UIBLOCK(3, 102, 200);
   BUTTON(mStartButton, "start");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mStopButton, "stop");
   ENDUIBLOCK(mHeight);

   mCastChannel = new CastChannel();
   mHttpServer = new CastHTTPServer();
}

void CastModule::Init()
{
   IDrawableModule::Init();
}

void CastModule::Poll()
{
   IDrawableModule::Poll();

   if (!mStreaming || mFFmpegPipe == nullptr)
      return;

   // Pump Cast messages
   if (mCastChannel && mCastChannel->IsConnected())
      mCastChannel->PumpMessage(0);

   // Throttle frame writes
   double now = gTime;
   double frameInterval = 1000.0 / mFramerate;
   if (now - mLastFrameTime < frameInterval)
      return;
   mLastFrameTime = now;

   // Read FBO and write to ffmpeg stdin
   IVisualSource* source = FindVisualSource();
   if (source == nullptr || source->GetFBO() == nullptr || !source->GetFBO()->IsValid())
      return;

   std::vector<uint8_t> pixels = source->GetFBO()->ReadPixels();
   if (pixels.empty())
      return;

   fwrite(pixels.data(), 1, pixels.size(), mFFmpegPipe);
   fflush(mFFmpegPipe);
}

void CastModule::PostRender()
{
}

void CastModule::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   if (cableSource == mInputCable)
      FindVisualSource();
}

IVisualSource* CastModule::FindVisualSource()
{
   IVisualSource* source = nullptr;
   if (mInputCable && !mInputCable->GetPatchCables().empty())
   {
      IClickable* target = mInputCable->GetPatchCables()[0]->GetTarget();
      source = dynamic_cast<IVisualSource*>(target);
   }

   if (source == nullptr)
   {
      std::vector<IDrawableModule*> allModules;
      TheSynth->GetAllModules(allModules);
      IClickable* me = dynamic_cast<IClickable*>(this);
      for (auto* mod : allModules)
      {
         if (mod == this) continue;
         for (auto* cableSource : mod->GetPatchCableSources())
         {
            for (auto* cable : cableSource->GetPatchCables())
            {
               if (cable->GetTarget() && cable->GetTarget() == me)
               {
                  source = dynamic_cast<IVisualSource*>(mod);
                  if (source) return source;
               }
            }
         }
      }
   }

   mSource = source;
   return source;
}

void CastModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mStartButton)
      StartStream();
   if (button == mStopButton)
      StopStream();
   if (button == mDiscoverButton)
      DiscoverDevices();
}

void CastModule::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mDeviceDropdown)
   {
      mSelectedDeviceIndex = (int)mDeviceDropdown->GetValue();
      if (mSelectedDeviceIndex >= 0 && mSelectedDeviceIndex < (int)mDiscoveredDevices.size())
      {
         const auto& device = mDiscoveredDevices[mSelectedDeviceIndex];
         mTargetIP = device.mIP;
      }
   }
}

// Get local IP address as a string
static std::string GetLocalIP()
{
   char hostname[256];
   if (gethostname(hostname, sizeof(hostname)) != 0)
      return "127.0.0.1";
   addrinfo hints = {};
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   addrinfo* info = nullptr;
   if (getaddrinfo(hostname, NULL, &hints, &info) != 0)
      return "127.0.0.1";
   std::string ip = "127.0.0.1";
   if (info)
   {
      sockaddr_in* addr = (sockaddr_in*)info->ai_addr;
      char ipStr[64];
      inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
      ip = ipStr;
   }
   freeaddrinfo(info);
   return ip;
}

void CastModule::StartStream()
{
   StopStream();

   int outW = 1280, outH = 720;
   GetOutputDimensions(mOutputRes, outW, outH);

   // Check that a visual source is connected
   IVisualSource* sourceCheck = FindVisualSource();
   if (sourceCheck == nullptr)
   {
      ofLog() << "CastModule: no visual source connected";
      mCastState = kCastState_Error;
      mCastError = "no visual source connected";
      return;
   }

   // Create temp directory for HLS
   char tempPath[MAX_PATH];
   GetTempPathA(MAX_PATH, tempPath);
   mStreamDir = std::string(tempPath) + "syntetika_cast_" + std::to_string(GetCurrentProcessId());
   CreateDirectoryA(mStreamDir.c_str(), NULL);

   std::string segPathPattern = mStreamDir + "\\stream_%03d.ts";
   std::string streamPath = mStreamDir + "\\stream.m3u8";

   // Start ffmpeg with HLS output
   std::string cmd = "ffmpeg.exe -hide_banner -loglevel error";
   cmd += " -f rawvideo -pixel_format rgba -video_size " + ofToString(outW) + "x" + ofToString(outH);
   cmd += " -framerate " + ofToString(mFramerate);
   cmd += " -i pipe:";
   cmd += " -c:v libx264 -preset ultrafast -tune zerolatency";
   cmd += " -pix_fmt yuv420p -g " + ofToString((int)mFramerate);
   cmd += " -f hls -hls_time 2 -hls_list_size 6 -hls_flags delete_segments";
   cmd += " -hls_segment_filename \"" + segPathPattern + "\"";
   cmd += " \"" + streamPath + "\"";
   cmd += " 2>\"" + mStreamDir + "\\ffmpeg_err.txt\"";

   ofLog() << "CastModule: starting ffmpeg: " << cmd;

    mFFmpegPipe = _popen(cmd.c_str(), "wb");
    if (mFFmpegPipe == nullptr)
    {
       int err = errno;
       ofLog() << "CastModule: failed to start ffmpeg (errno=" << err << ")";
       RemoveDirectoryA(mStreamDir.c_str());
       mCastState = kCastState_Error;
       mCastError = "ffmpeg not found or failed to start";
       return;
    }

   // Wait for first HLS segment to appear (up to 12 seconds)
   std::string segPath = mStreamDir + "\\stream_000.ts";
   bool segmentGenerated = false;
   int frameSize = outW * outH * 4;
   std::vector<uint8_t> blackFrame(frameSize, 0);
   for (int i = 0; i < 60; ++i)
   {
      std::ifstream f(segPath.c_str());
      if (f.good())
      {
         f.close();
         segmentGenerated = true;
         break;
      }
      // Write frames to ffmpeg stdin
      IVisualSource* src = FindVisualSource();
      if (src && src->GetFBO() && src->GetFBO()->IsValid())
      {
         auto px = src->GetFBO()->ReadPixels();
         if (px.size() == (size_t)frameSize)
         {
            for (int j = 0; j < 3; ++j)
               fwrite(px.data(), 1, px.size(), mFFmpegPipe);
         }
         else
         {
            for (int j = 0; j < 3; ++j)
               fwrite(blackFrame.data(), 1, blackFrame.size(), mFFmpegPipe);
         }
      }
      else
      {
         for (int j = 0; j < 3; ++j)
            fwrite(blackFrame.data(), 1, blackFrame.size(), mFFmpegPipe);
      }
      fflush(mFFmpegPipe);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
   }

   if (!segmentGenerated)
   {
      // Read ffmpeg stderr for diagnostics
      std::string errPath = mStreamDir + "\\ffmpeg_err.txt";
      std::ifstream ef(errPath.c_str());
      std::string errMsg;
      if (ef.good())
      {
         std::stringstream ss;
         ss << ef.rdbuf();
         errMsg = ss.str();
      }

      ofLog() << "CastModule: HLS segment not generated. ffmpeg error: " << errMsg;
      StopStream();
      mCastState = kCastState_Error;
      mCastError = "HLS init failed";
      return;
   }

   // Start HTTP server on random port
   int httpPort = 0;
   for (int port = 18080; port < 18100; ++port)
   {
      if (mHttpServer->Start(port, mStreamDir))
      {
         httpPort = port;
         break;
      }
   }

   if (httpPort == 0)
   {
      ofLog() << "CastModule: failed to start HTTP server";
      StopStream();
      mCastState = kCastState_Error;
      mCastError = "HTTP server failed";
      return;
   }

   ofLog() << "CastModule: HTTP server on port " << httpPort;

   // Connect to TV via Cast protocol
   mCastState = kCastState_Connecting;

   int castPort = 8009;
   if (!mTargetPort.empty())
   {
      try { castPort = std::stoi(mTargetPort); }
      catch (...) {}
   }

   if (!mCastChannel->Connect(mTargetIP, castPort))
   {
      ofLog() << "CastModule: failed to connect to TV at " << mTargetIP << ":" << castPort;
      StopStream();
      mCastState = kCastState_Error;
      mCastError = "TV connection failed (port 8009)";
      return;
   }

    ofLog() << "CastModule: TLS connected to TV";

    // handle incoming messages from TV (STOP, PAUSE, etc.)
    mCastChannel->SetOnMessage([this](const CastChannel::Message& msg) {
       if (msg.namespace_ == "urn:x-cast:com.google.cast.tp.connection" && msg.payload.find("CLOSE") != std::string::npos)
       {
          ofLog() << "CastModule: TV closed connection";
          StopStream();
       }
       else if (msg.namespace_ == "urn:x-cast:com.google.cast.tp.media" && msg.payload.find("STOPPED") != std::string::npos)
       {
          ofLog() << "CastModule: TV stopped playback";
          StopStream();
       }
    });

   // Build stream URL
   std::string localIP = GetLocalIP();
   std::string streamUrl = "http://" + localIP + ":" + ofToString(httpPort) + "/stream.m3u8";

   // Send Cast messages
   int appPort = 0;
   if (!mCastChannel->StartCast(streamUrl, appPort))
   {
      ofLog() << "CastModule: Cast protocol failed";
      StopStream();
      mCastState = kCastState_Error;
      mCastError = "Cast protocol failed";
      return;
   }

   mStreaming = true;
   mLastFrameTime = gTime;
   mCastState = kCastState_Streaming;

   ofLog() << "CastModule: streaming to TV at " << streamUrl;
}

void CastModule::StopStream()
{
   mStreaming = false;
   mCastState = kCastState_Idle;

   if (mCastChannel)
      mCastChannel->Disconnect();

   if (mHttpServer)
      mHttpServer->Stop();

   if (mFFmpegPipe)
   {
      _pclose(mFFmpegPipe);
      mFFmpegPipe = nullptr;
   }

   // Clean up temp directory
   if (!mStreamDir.empty())
   {
      // Remove .ts and .m3u8 files
      WIN32_FIND_DATAA findData;
      std::string pattern = mStreamDir + "\\*";
      HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
      if (hFind != INVALID_HANDLE_VALUE)
      {
         do {
            std::string file = mStreamDir + "\\" + findData.cFileName;
            DeleteFileA(file.c_str());
         } while (FindNextFileA(hFind, &findData));
         FindClose(hFind);
      }
      RemoveDirectoryA(mStreamDir.c_str());
      mStreamDir.clear();
   }

   ofLog() << "CastModule: streaming stopped";
}

void CastModule::GetOutputDimensions(int res, int& w, int& h)
{
   switch (res)
   {
   case kOutputRes_1080p: w = 1920; h = 1080; break;
   case kOutputRes_720p:  w = 1280; h = 720;  break;
   case kOutputRes_480p:  w = 854;  h = 480;  break;
   case kOutputRes_360p:  w = 640;  h = 360;  break;
   default:
      if (IVisualSource* s = FindVisualSource())
      {
         if (s->GetFBO())
         {
            w = s->GetFBO()->GetWidth();
            h = s->GetFBO()->GetHeight();
         }
      }
      break;
   }
}

void CastModule::DiscoverDevices()
{
   mDiscoveredDevices = SSDPDiscoverer::Discover(4000);
   PopulateDeviceDropdown();
}

void CastModule::PopulateDeviceDropdown()
{
   mDeviceDropdown->Clear();
   mDeviceDropdown->AddLabel("-- manual --", -1);
   for (int i = 0; i < (int)mDiscoveredDevices.size(); ++i)
   {
      const auto& dev = mDiscoveredDevices[i];
      std::string label = dev.mFriendlyName;
      if (label.length() > 35)
         label = label.substr(0, 32) + "...";
      label += " (" + dev.mIP + ")";
      mDeviceDropdown->AddLabel(label, i);
   }
   mSelectedDeviceIndex = -1;
}

void CastModule::DrawModule()
{
   if (Minimized() || IsVisible() == false) return;

   mTargetIPEntry->Draw();
   mTargetPortEntry->Draw();
   mDeviceDropdown->Draw();
   mDiscoverButton->Draw();
   mOutputResDropdown->Draw();
   mFramerateSlider->Draw();
   mStartButton->Draw();
   mStopButton->Draw();

   ofPushStyle();
   int y = mHeight - 15;
   if (mCastState == kCastState_Streaming)
   {
      ofSetColor(100, 255, 100);
      DrawTextRightJustify("CASTING to " + mTargetIP, mWidth - 4, y);
   }
   else if (mCastState == kCastState_Connecting)
   {
      ofSetColor(255, 255, 100);
      DrawTextRightJustify("connecting...", mWidth - 4, y);
   }
   else if (mCastState == kCastState_Error)
   {
      ofSetColor(255, 100, 100);
      DrawTextRightJustify("error: " + mCastError, mWidth - 4, y);
   }
   else
   {
      ofSetColor(180, 180, 180);
      DrawTextRightJustify("idle", mWidth - 4, y);
   }
   ofPopStyle();
}

void CastModule::GetModuleDimensions(float& w, float& h)
{
   w = mWidth;
   h = mHeight;
}

VisualFBO* CastModule::GetFBO()
{
   return nullptr;
}

void CastModule::Resize(float w, float h)
{
   mWidth = w;
   mHeight = h;
}

void CastModule::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["width"] = mWidth;
   moduleInfo["height"] = mHeight;
   moduleInfo["targetIP"] = mTargetIP;
   moduleInfo["targetPort"] = mTargetPort;
   moduleInfo["outputRes"] = mOutputRes;
   moduleInfo["framerate"] = mFramerate;
}

void CastModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
}

void CastModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
}

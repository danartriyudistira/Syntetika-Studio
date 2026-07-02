#pragma once

#include <string>
#include <vector>

struct SSDPDevice
{
   std::string mFriendlyName;
   std::string mIP;
   std::string mLocation;
   std::string mServer;
};

class SSDPDiscoverer
{
public:
   static std::vector<SSDPDevice> Discover(int timeoutMs = 4000);

private:
   struct DiscoverContext
   {
      std::vector<SSDPDevice> devices;
      int timeoutMs;
   };

   static void DiscoverSSDP(DiscoverContext& ctx);
   static void DiscoverMDNS(DiscoverContext& ctx);
   static void AddDevice(DiscoverContext& ctx, const std::string& name,
                         const std::string& ip);
   static bool HasIP(const DiscoverContext& ctx, const std::string& ip);
};

#pragma once

struct SpatialSourceInfo
{
   float x{ 0 };
   float y{ 0 };
   float z{ 0 };
   float volume{ 1 };
   int   colorHue{ 0 };
   float occlusion{ 0 };
   char  name[64]{};
};

struct SpatialRoomInfo
{
   float roomW{ 600 };
   float roomD{ 500 };
   float roomH{ 300 };
   float listenerX{ 0 };
   float listenerY{ -100 };
   float listenerZ{ 170 };
   int   numSpeakers{ 0 };
   bool  hrtfEnabled{ false };
   int   hrtfQuality{ 0 };
};

struct SpatialSpeakerInfo
{
   float x{ 0 };
   float y{ 0 };
   float z{ 0 };
   int   channel{ 0 };
};

#pragma once

class VisualFBO;

class IVisualSource
{
public:
   virtual ~IVisualSource() {}
   virtual VisualFBO* GetFBO() = 0;
};

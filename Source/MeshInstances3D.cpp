#include "MeshInstances3D.h"
#include "SynthGlobals.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include "ModularSynth.h"
#include "VisualFBO.h"

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#include <algorithm>
#include <cmath>
#include <set>

static const float kDegToRad = (float)(M_PI / 180.0);

MeshInstances3D::MeshInstances3D()
: IAudioProcessor(gBufferSize)
{
   GenerateCube();
}

MeshInstances3D::~MeshInstances3D()
{
   delete mFBO;
}

void MeshInstances3D::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   DROPDOWN(mShapeDropdown, "shape", &mShapeInt, 40);
   ENDUIBLOCK0();

   mShapeDropdown->AddLabel("cube", kCube);
   mShapeDropdown->AddLabel("sphere", kLowPolySphere);
   mShapeDropdown->AddLabel("pyramid", kPyramid);
   mShapeDropdown->AddLabel("custom OBJ", kCustomOBJ);
   mShapeDropdown->AddLabel("SVG", kSVG);

   UIBLOCK(80, 24, 260);
   FLOATSLIDER(mScanFreqSlider, "scan freq", &mScanFreq, 5, 500);
   FLOATSLIDER(mFmDepthSlider, "fm depth", &mFmDepth, 0, 1);
   FLOATSLIDER(mAmplifySlider, "amplify", &mAmplify, 0, 3);
   CHECKBOX(mFreqFollowCheckbox, "freq follow", &mFreqFollow);
   ENDUIBLOCK0();

   UIBLOCK(80, 112, 260);
   FLOATSLIDER(mRotXSlider, "rot X", &mRotX, 0, 360);
   FLOATSLIDER(mRotYSlider, "rot Y", &mRotY, 0, 360);
   FLOATSLIDER(mRotZSlider, "rot Z", &mRotZ, 0, 360);
   FLOATSLIDER(mPerspectiveSlider, "perspective", &mPerspective, 0, 2);
   ENDUIBLOCK0();

   UIBLOCK(80, 240, 260);
   TEXTENTRY(mModelPathEntry, "model path", 30, &mModelPath);
   BUTTON(mLoadModelButton, "load");
   ENDUIBLOCK(mHeight);
}

void MeshInstances3D::Init()
{
   IDrawableModule::Init();
}

void MeshInstances3D::Poll()
{
   IDrawableModule::Poll();
}

void MeshInstances3D::Process(double time)
{
   PROFILER(MeshInstances3D);

   if (!mEnabled || mPathLen < 2)
   {
      SyncBuffers(2);
      GetBuffer()->Reset();
      return;
   }

   IAudioReceiver* target = GetTarget();
   if (target == nullptr)
   {
      GetBuffer()->Reset();
      return;
   }

   SyncBuffers(2);

   int bufferSize = GetBuffer()->BufferSize();
   int numChannels = GetBuffer()->NumActiveChannels();
   float* inData = (numChannels > 0) ? GetBuffer()->GetChannel(0) : nullptr;

   // frequency detection via zero-crossing (when freq follow is on)
   if (mFreqFollow && inData)
   {
      int iAbsolute = mSampleCounter;
      for (int i = 0; i < bufferSize; ++i)
      {
         float s = inData[i];
         if (mLastInputSample < 0 && s >= 0)
         {
            int delta = iAbsolute - mLastZeroCrossSample;
            if (mLastZeroCrossSample > 0 && delta > 10)
            {
               mZeroCrossAccum += delta;
               mZeroCrossCount++;
               if (mZeroCrossCount >= 4)
               {
                  float period = (float)mZeroCrossAccum / mZeroCrossCount / gSampleRate;
                  mDetectedFreq = ofClamp(1.0f / period, 5.0f, 500.0f);
                  mZeroCrossAccum = 0;
                  mZeroCrossCount = 0;
               }
            }
            mLastZeroCrossSample = iAbsolute;
         }
         mLastInputSample = s;
         iAbsolute++;
      }
      mSampleCounter = iAbsolute;
   }

   // build rotation matrix from Euler angles (degrees)
   float rx = mRotX * kDegToRad;
   float ry = mRotY * kDegToRad;
   float rz = mRotZ * kDegToRad;

   float cx = cosf(rx), sx = sinf(rx);
   float cy = cosf(ry), sy = sinf(ry);
   float cz = cosf(rz), sz = sinf(rz);

   // R = Rz(roll) * Rx(pitch) * Ry(yaw)
   float m00 = cy * cz - sx * sy * sz;
   float m01 = -sz * cx;
   float m02 = cz * sy + sz * sx * cy;
   float m10 = sz * cy + cz * sx * sy;
   float m11 = cz * cx;
   float m12 = sz * sy - cz * sx * cy;
   float m20 = -cx * sy;
   float m21 = sx;
   float m22 = cx * cy;

    // pre-compute projected 2D positions for all vertices
    float* verts = mVertexPositions.data();
    int nv = mNumVertices;
    mAudioProjX.resize(nv);
    mAudioProjY.resize(nv);
    float* projX = mAudioProjX.data();
    float* projY = mAudioProjY.data();

    for (int i = 0; i < nv; ++i)
    {
       float vx = verts[i * 3];
       float vy = verts[i * 3 + 1];
       float vz = verts[i * 3 + 2];

       float rx_f = m00 * vx + m01 * vy + m02 * vz;
       float ry_f = m10 * vx + m11 * vy + m12 * vz;
       float rz_f = m20 * vx + m21 * vy + m22 * vz;

       float persp = 1.0f / (1.0f + rz_f * mPerspective);
       projX[i] = rx_f * persp;
       projY[i] = ry_f * persp;
    }

   // trace vertex path: phase [0,1) maps linearly along the cyclic path.
   // Each segment gets equal phase share = 1/segments.
   // Linear interpolation at sample-rate guarantees every vertex is exactly reached.
   int segments = mPathLen - 1;

   float sampleRate = gSampleRate;
   float scanFreq = mFreqFollow ? mDetectedFreq : mScanFreq;
    float* outL = target->GetBuffer()->GetChannel(0);
    float* outR = target->GetBuffer()->GetChannel(1);

    mVizL.resize(bufferSize);
    mVizR.resize(bufferSize);
    float* vizL = mVizL.data();
    float* vizR = mVizR.data();

   for (int i = 0; i < bufferSize; ++i)
   {
      // audio FM modulates scan frequency
      float mod = inData ? inData[i] * mFmDepth * scanFreq : 0;
      float inc = (scanFreq + mod) / sampleRate;
      mPhase = fmodf(mPhase + inc, 1.0f);
      if (mPhase < 0) mPhase += 1.0f;

      // map phase to segment index + fractional position within segment
      float segPhase = mPhase * segments;
      int segIdx = (int)segPhase;
      float frac = segPhase - segIdx;
      if (segIdx >= segments) segIdx = segments - 1;

      int idxA = mVertexPath[segIdx];
      int idxB = mVertexPath[segIdx + 1];

      float lx = projX[idxA] + (projX[idxB] - projX[idxA]) * frac;
      float ly = projY[idxA] + (projY[idxB] - projY[idxA]) * frac;

      outL[i] += lx * mAmplify;
      outR[i] += ly * mAmplify;
      vizL[i] = lx * mAmplify;
      vizR[i] = ly * mAmplify;
   }

   GetVizBuffer()->WriteChunk(vizL, bufferSize, 0);
   GetVizBuffer()->WriteChunk(vizR, bufferSize, 1);

    target->GetBuffer()->SetNumActiveChannels(2);
   GetBuffer()->Reset();
}

void MeshInstances3D::GenerateCube()
{
   mIsSVG = false;
   mVertexPositions = {
      -0.5f, -0.5f, -0.5f,  // 0: back-left-bottom
       0.5f, -0.5f, -0.5f,  // 1: back-right-bottom
       0.5f,  0.5f, -0.5f,  // 2: back-right-top
      -0.5f,  0.5f, -0.5f,  // 3: back-left-top
      -0.5f, -0.5f,  0.5f,  // 4: front-left-bottom
       0.5f, -0.5f,  0.5f,  // 5: front-right-bottom
       0.5f,  0.5f,  0.5f,  // 6: front-right-top
      -0.5f,  0.5f,  0.5f,  // 7: front-left-top
   };
   mNumVertices = 8;

   mEdges = {
      0,1, 1,2, 2,3, 3,0,  // back face
      4,5, 5,6, 6,7, 7,4,  // front face
      0,4, 1,5, 2,6, 3,7,  // connectors
   };
   mNumEdges = 12;

   // cyclic vertex path: covers all 12 edges, first == last (0)
   mVertexPath = { 0,1,2,3,0,4,5,6,7,4,0,3,7,6,2,1,5,4,0 };
   mPathLen = (int)mVertexPath.size();
}

void MeshInstances3D::GenerateLowPolySphere()
{
   mIsSVG = false;
   mVertexPositions.clear();
   mEdges.clear();
   mVertexPath.clear();

   int rings = 6;
   int sectors = 10;
   float r = 0.5f;

   // top pole
   mVertexPositions.push_back(0); mVertexPositions.push_back(r); mVertexPositions.push_back(0);

   for (int ring = 1; ring < rings; ++ring)
   {
      float lat = (float)ring / (float)rings * (float)M_PI;
      float yr = r * cosf(lat);
      float rad = r * sinf(lat);
      for (int sec = 0; sec < sectors; ++sec)
      {
         float lon = (float)sec / (float)sectors * 2 * (float)M_PI;
         mVertexPositions.push_back(rad * cosf(lon));
         mVertexPositions.push_back(yr);
         mVertexPositions.push_back(rad * sinf(lon));
      }
   }

   // bottom pole
   int bottomPole = 1 + (rings - 1) * sectors;
   mVertexPositions.push_back(0); mVertexPositions.push_back(-r); mVertexPositions.push_back(0);
   mNumVertices = 1 + (rings - 1) * sectors + 1;

   // build edges
   auto addEdge = [&](int a, int b) { mEdges.push_back(a); mEdges.push_back(b); };
   for (int s = 0; s < sectors; ++s)
      addEdge(0, 1 + s);
   for (int ring = 0; ring < rings - 1; ++ring)
   {
      int base = 1 + ring * sectors;
      for (int s = 0; s < sectors; ++s)
      {
         int cur = base + s;
         int next = base + (s + 1) % sectors;
         addEdge(cur, next);
         if (ring < rings - 2)
            addEdge(cur, cur + sectors);
      }
   }
   int lastRingBase = 1 + (rings - 2) * sectors;
   for (int s = 0; s < sectors; ++s)
      addEdge(lastRingBase + s, bottomPole);
   mNumEdges = (int)mEdges.size() / 2;

   // build cyclic vertex path: zigzag traversal covering all edges
   // top pole → ring 1 ring → back to top pole → ring 1 next sector → ...
   // Simplified: go around each ring, then down to next ring, etc.
   // For now: top pole → each ring sector sequentially → bottom pole → back up
   mVertexPath.push_back(0);
   for (int s = 0; s < sectors; ++s)
   {
      int cur = 1 + s;
      mVertexPath.push_back(cur);
   }
   for (int ring = 1; ring < rings - 1; ++ring)
   {
      int base = 1 + ring * sectors;
      for (int s = 0; s < sectors; ++s)
         mVertexPath.push_back(base + s);
   }
   mVertexPath.push_back(bottomPole);
   // return to top pole to complete cycle
   mVertexPath.push_back(0);
   mPathLen = (int)mVertexPath.size();
}

void MeshInstances3D::GeneratePyramid()
{
   mIsSVG = false;
   mVertexPositions = {
      -0.5f, -0.5f, -0.5f,  // 0
       0.5f, -0.5f, -0.5f,  // 1
       0.5f, -0.5f,  0.5f,  // 2
      -0.5f, -0.5f,  0.5f,  // 3
       0,     0.5f,  0,      // 4: apex
   };
   mNumVertices = 5;

   mEdges = {
      0,1, 1,2, 2,3, 3,0,  // base
      0,4, 1,4, 2,4, 3,4,  // sides
   };
   mNumEdges = 8;

   // cyclic vertex path: cover base then sides, return to start
   mVertexPath = { 0,1,2,3,0,4,1,4,2,4,3,4,0 };
   mPathLen = (int)mVertexPath.size();
}

void MeshInstances3D::SelectBuiltInShape(BuiltInShape shape)
{
   switch (shape)
   {
   case kCube: GenerateCube(); break;
   case kLowPolySphere: GenerateLowPolySphere(); break;
   case kPyramid: GeneratePyramid(); break;
   default: return;
   }
}

bool MeshInstances3D::LoadModelOBJ(const std::string& path)
{
   mIsSVG = false;

   tinyobj::attrib_t attrib;
   std::vector<tinyobj::shape_t> shapes;
   std::vector<tinyobj::material_t> materials;
   std::string warn, err;

   bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());
   if (!ok || shapes.empty() || attrib.vertices.empty())
      return false;

   struct EdgeKey { int a, b; };
   std::vector<EdgeKey> edges;
   std::vector<float> unique;
   float eps = 0.0001f;

   auto findOrAdd = [&](float x, float y, float z) -> int {
      for (int i = 0; i < (int)unique.size() / 3; ++i)
         if (fabs(x - unique[i * 3]) < eps &&
             fabs(y - unique[i * 3 + 1]) < eps &&
             fabs(z - unique[i * 3 + 2]) < eps) return i;
      unique.push_back(x); unique.push_back(y); unique.push_back(z);
      return (int)unique.size() / 3 - 1;
   };

   auto addEdge = [&](int a, int b) {
      if (a < 0 || b < 0) return;
      for (auto& e : edges) if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) return;
      edges.push_back({a, b});
   };

   for (auto& shape : shapes)
   {
      int idx = 0;
      size_t totalIndices = shape.mesh.indices.size();
      size_t numFaces = shape.mesh.num_face_vertices.size();
      for (size_t f = 0; f < numFaces; ++f)
      {
         int nv = shape.mesh.num_face_vertices[f];
         if (nv < 1 || idx + nv > (int)totalIndices)
         {
            idx += nv;
            continue;
         }
         std::vector<int> faceVerts;
         for (int v = 0; v < nv; ++v)
         {
            auto& ind = shape.mesh.indices[idx + v];
            int vi = ind.vertex_index;
            if (vi < 0) continue;
            int vi3 = vi * 3;
            if (vi3 + 2 >= (int)attrib.vertices.size()) continue;
            faceVerts.push_back(findOrAdd(attrib.vertices[vi3], attrib.vertices[vi3 + 1], attrib.vertices[vi3 + 2]));
         }
         if (faceVerts.size() >= 2)
         {
            for (int v = 0; v < (int)faceVerts.size(); ++v)
               addEdge(faceVerts[v], faceVerts[(v + 1) % faceVerts.size()]);
         }
         idx += nv;
      }
   }

   if (edges.empty())
      return false;

   mVertexPositions.swap(unique);
   mNumVertices = (int)mVertexPositions.size() / 3;

   mEdges.clear();
   for (auto& e : edges) { mEdges.push_back(e.a); mEdges.push_back(e.b); }
   mNumEdges = (int)mEdges.size() / 2;

   // build vertex path: visit every edge sequentially, one after another.
   mVertexPath.clear();
   for (int i = 0; i < mNumEdges; ++i)
   {
      int a = mEdges[i * 2];
      int b = mEdges[i * 2 + 1];
      if (i == 0)
      {
         mVertexPath.push_back(a);
         mVertexPath.push_back(b);
      }
      else
      {
         if (a == mVertexPath.back())
            mVertexPath.push_back(b);
         else if (b == mVertexPath.back())
            mVertexPath.push_back(a);
         else
         {
            mVertexPath.push_back(a);
            mVertexPath.push_back(b);
         }
      }
   }

   // make cyclic if last vertex connects back to first
   if (mVertexPath.size() > 1 && mVertexPath.back() != mVertexPath[0])
   {
      int last = mVertexPath.back();
      int first = mVertexPath[0];
      for (int i = 0; i < mNumEdges; ++i)
      {
         if ((mEdges[i*2] == last && mEdges[i*2+1] == first) ||
             (mEdges[i*2] == first && mEdges[i*2+1] == last))
         {
            mVertexPath.push_back(first);
            break;
         }
      }
   }

   mPathLen = (int)mVertexPath.size();
   return mPathLen >= 2;
}

bool MeshInstances3D::LoadModelSVG(const std::string& path)
{
   NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
   if (image == nullptr)
      return false;

   struct Point { float x, y; };
   std::vector<Point> samples;

   for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next)
   {
      for (NSVGpath* p = shape->paths; p != nullptr; p = p->next)
      {
         if (p->npts < 4)
            continue;

         int numSegs = (p->npts - 1) / 3;
         float* pts = p->pts;

         // first point of path
         samples.push_back({pts[0], pts[1]});

         for (int s = 0; s < numSegs; ++s)
         {
            int i = s * 3;
            float x0 = pts[i*2], y0 = pts[i*2+1];
            float x1 = pts[(i+1)*2], y1 = pts[(i+1)*2+1];
            float x2 = pts[(i+2)*2], y2 = pts[(i+2)*2+1];
            float x3 = pts[(i+3)*2], y3 = pts[(i+3)*2+1];

            for (int t = 1; t <= 10; ++t)
            {
               float u = t / 10.0f;
               float omu = 1.0f - u;
               float x = omu*omu*omu*x0 + 3.0f*omu*omu*u*x1 + 3.0f*omu*u*u*x2 + u*u*u*x3;
               float y = omu*omu*omu*y0 + 3.0f*omu*omu*u*y1 + 3.0f*omu*u*u*y2 + u*u*u*y3;
               samples.push_back({x, y});
            }
         }
      }
   }

   nsvgDelete(image);

   if (samples.size() < 3)
      return false;

   // find bounding box for centering
   float minX = samples[0].x, maxX = samples[0].x;
   float minY = samples[0].y, maxY = samples[0].y;
   for (auto& pt : samples)
   {
      if (pt.x < minX) minX = pt.x;
      if (pt.x > maxX) maxX = pt.x;
      if (pt.y < minY) minY = pt.y;
      if (pt.y > maxY) maxY = pt.y;
   }

   float cx = (minX + maxX) * 0.5f;
   float cy = (minY + maxY) * 0.5f;
   float range = fmaxf(maxX - minX, maxY - minY);
   if (range < 0.001f) range = 1.0f;
   float scale = 0.5f / range;

   // build vertex positions (x,y,0) and vertex path
   mVertexPositions.clear();
   mVertexPath.clear();
   mEdges.clear();

   int n = (int)samples.size();
   for (int i = 0; i < n; ++i)
   {
      float x = (samples[i].x - cx) * scale;
      float y = (samples[i].y - cy) * scale;
      mVertexPositions.push_back(x);
      mVertexPositions.push_back(y);
      mVertexPositions.push_back(0);
      mVertexPath.push_back(i);
   }

   // make cyclic
   mVertexPath.push_back(0);

   // build edges: consecutive vertices
   for (int i = 0; i < n; ++i)
   {
      mEdges.push_back(i);
      mEdges.push_back((i + 1) % n);
   }

   mNumVertices = n;
   mNumEdges = n;
   mPathLen = n + 1; // cyclic
   mIsSVG = true;

   return true;
}

void MeshInstances3D::ButtonClicked(ClickButton* button, double time)
{
   if (button == mLoadModelButton)
   {
      juce::FileChooser chooser("Load model",
                                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                "*.obj;*.svg", true, false, TheSynth->GetFileChooserParent());
      if (chooser.browseForFileToOpen())
      {
         mModelPath = chooser.getResult().getFullPathName().toStdString();
         std::string ext = mModelPath.substr(mModelPath.find_last_of('.'));
         if (ext == ".svg" || ext == ".SVG")
         {
            if (LoadModelSVG(mModelPath))
               mShapeDropdown->SetValue(kSVG, time);
         }
         else
         {
            if (LoadModelOBJ(mModelPath))
               mShapeDropdown->SetValue(kCustomOBJ, time);
         }
      }
   }
}

void MeshInstances3D::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mShapeDropdown)
   {
      BuiltInShape shape = (BuiltInShape)mShapeInt;
      if (shape == kSVG)
      {
         mIsSVG = true;
         if (!mModelPath.empty()) LoadModelSVG(mModelPath);
      }
      else if (shape == kCustomOBJ)
      {
         mIsSVG = false;
         if (!mModelPath.empty()) LoadModelOBJ(mModelPath);
      }
      else
      {
         SelectBuiltInShape(shape);
         mIsSVG = false;
      }
   }
}

void MeshInstances3D::DrawModule()
{
   if (Minimized() || IsVisible() == false) return;

   // Draw FBO (contains shape preview from PostRender)
   if (mFBO && mFBO->IsValid())
      mFBO->Draw(0, 0, mWidth, mHeight);

   // Background behind controls area
   ofPushStyle();
   ofSetColor(80, 80, 80, 100);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);
   ofPopStyle();

   // Controls on top
   mShapeDropdown->Draw();
   mScanFreqSlider->Draw();
   mFmDepthSlider->Draw();
   mAmplifySlider->Draw();
   mRotXSlider->Draw();
   mRotYSlider->Draw();
   mRotZSlider->Draw();
   mPerspectiveSlider->Draw();
   mFreqFollowCheckbox->Draw();
   mModelPathEntry->Draw();
   mLoadModelButton->Draw();

   // Stats at bottom
   ofPushStyle();
   ofSetColor(200, 200, 200);
   DrawTextRightJustify("edges: " + ofToString(mNumEdges) + " path: " + ofToString(mPathLen), mWidth - 4, mHeight - 18);
   ofPopStyle();
}

void MeshInstances3D::PostRender()
{
   if (!mEnabled || mNumVertices < 1 || mPathLen < 2 || mWidth < 10 || mHeight < 10)
      return;

   float monY = 272.0f;
   float monW = mWidth - 6.0f;
   float monH = mHeight - monY - 20.0f;
   if (monH <= 10.0f || monW <= 10.0f)
      return;

   if (!mFBO || !mFBO->IsValid() ||
       mFBO->GetWidth() != (int)mWidth ||
       mFBO->GetHeight() != (int)mHeight)
   {
      delete mFBO;
      mFBO = new VisualFBO();
      mFBO->Create(std::max(64, (int)mWidth), std::max(64, (int)mHeight));
   }

   if (!mFBO || !mFBO->IsValid())
      return;

   mFBO->Bind();

   // Project vertices with current rotation + perspective
   float rx = mRotX * kDegToRad;
   float ry = mRotY * kDegToRad;
   float rz = mRotZ * kDegToRad;
   float cx = cosf(rx), sx = sinf(rx);
   float cy = cosf(ry), sy = sinf(ry);
   float cz = cosf(rz), sz = sinf(rz);
   float m00 = cy*cz - sx*sy*sz, m01 = -sz*cx, m02 = cz*sy + sz*sx*cy;
   float m10 = sz*cy + cz*sx*sy, m11 = cz*cx, m12 = sz*sy - cz*sx*cy;
   float m20 = -cx*sy, m21 = sx, m22 = cx*cy;

    mRenderProjX.resize(mNumVertices);
    mRenderProjY.resize(mNumVertices);
    float* px = mRenderProjX.data();
    float* py = mRenderProjY.data();
    float bminX = 1e10f, bmaxX = -1e10f, bminY = 1e10f, bmaxY = -1e10f;
    for (int i = 0; i < mNumVertices; ++i)
    {
       float vx = mVertexPositions[i*3];
       float vy = mVertexPositions[i*3+1];
       float vz = mVertexPositions[i*3+2];
       float rxf = m00*vx + m01*vy + m02*vz;
       float ryf = m10*vx + m11*vy + m12*vz;
       float rzf = m20*vx + m21*vy + m22*vz;
       float p = 1.0f / (1.0f + rzf * mPerspective);
       px[i] = rxf * p;
       py[i] = ryf * p;
      if (px[i] < bminX) bminX = px[i];
      if (px[i] > bmaxX) bmaxX = px[i];
      if (py[i] < bminY) bminY = py[i];
      if (py[i] > bmaxY) bmaxY = py[i];
   }

   float rangeX = bmaxX - bminX;
   float rangeY = bmaxY - bminY;
   if (rangeX < 0.001f) rangeX = 1.0f;
   if (rangeY < 0.001f) rangeY = 1.0f;
   float s = fminf(monW / rangeX, monH / rangeY) * 0.85f;
   float centX = (bminX + bmaxX) * 0.5f;
   float centY = (bminY + bmaxY) * 0.5f;

   // Dark background for monitor area
   ofSetColor(35, 35, 45, 255);
   ofFill();
   ofRect(3, monY, monW, monH);

   // Wireframe
   ofSetLineWidth(1.5f);
   ofSetColor(100, 200, 255, 200);
   ofNoFill();
   ofBeginShape();
   for (int i = 0; i < mPathLen; ++i)
   {
      int idx = mVertexPath[i];
      float sx = 3.0f + monW*0.5f + (px[idx] - centX) * s;
      float sy = monY + monH*0.5f + (py[idx] - centY) * s;
      ofVertex(sx, sy);
   }
   ofEndShape(false);

   mFBO->Unbind();
}

VisualFBO* MeshInstances3D::GetFBO()
{
   return mFBO;
}

void MeshInstances3D::Resize(float w, float h)
{
   mWidth = w;
   mHeight = h;
}

void MeshInstances3D::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["width"] = mWidth;
   moduleInfo["height"] = mHeight;
   moduleInfo["modelPath"] = mModelPath;
   moduleInfo["shape"] = mShapeInt;
}

void MeshInstances3D::LoadLayout(const ofxJSONElement& moduleInfo)
{
   {
#pragma push_macro("LoadString")
#undef LoadString
      mModuleSaveData.LoadString("target", moduleInfo);
      mModuleSaveData.LoadFloat("width", moduleInfo, 360);
      mModuleSaveData.LoadFloat("height", moduleInfo, 280);
#pragma pop_macro("LoadString")
   }
   SetUpFromSaveData();
}

void MeshInstances3D::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mWidth = mModuleSaveData.GetFloat("width");
   mHeight = mModuleSaveData.GetFloat("height");
}

void MeshInstances3D::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mShapeInt;
   out << mModelPath;
   out << mScanFreq;
   out << mFmDepth;
   out << mAmplify;
   out << mRotX;
   out << mRotY;
   out << mRotZ;
   out << mPerspective;
   out << mFreqFollow;
   out << mIsSVG;
}

void MeshInstances3D::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev < 2) return;
   in >> mShapeInt;
   in >> mModelPath;
   in >> mScanFreq;
   in >> mFmDepth;
   in >> mAmplify;

   if (rev >= 3)
   {
      if (rev >= 6)
      {
         in >> mRotX;
         in >> mRotY;
         in >> mRotZ;
         in >> mPerspective;
      }
   }

   if (rev >= 7)
   {
      in >> mFreqFollow;
   }

   if (rev >= 8)
   {
      in >> mIsSVG;
   }

   BuiltInShape shape = (BuiltInShape)mShapeInt;
   if (shape != kCustomOBJ) SelectBuiltInShape(shape);
   else if (!mModelPath.empty()) LoadModelOBJ(mModelPath);
}

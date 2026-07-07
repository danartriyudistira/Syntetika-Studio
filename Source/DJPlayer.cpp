#include "DJPlayer.h"
#include "IAudioReceiver.h"
#include "Sample.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include "Transport.h"
#include "ofxJSONElement.h"
#include "keyfinder.h"

#include "juce_gui_basics/juce_gui_basics.h"
#include "juce_audio_formats/juce_audio_formats.h"

using namespace juce;

// ═══════════════════════════════════════════════════════════════════
//  Pitch range helpers
// ═══════════════════════════════════════════════════════════════════

float DJPlayer::PitchRangeToFloat(PitchRange r)
{
   switch (r)
   {
      case PitchRange::Six:    return 0.06f;
      case PitchRange::Ten:    return 0.10f;
      case PitchRange::Sixteen:return 0.16f;
      case PitchRange::Wide:   return 1.00f;
   }
   return 0.10f;
}

const char* DJPlayer::PitchRangeLabel(PitchRange r)
{
   switch (r)
   {
      case PitchRange::Six:    return "±6%";
      case PitchRange::Ten:    return "±10%";
      case PitchRange::Sixteen:return "±16%";
      case PitchRange::Wide:   return "WIDE";
   }
   return "±10%";
}

// ═══════════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════════

DJPlayer::DJPlayer()
: IAudioProcessor(gBufferSize)
, mNoteInputBuffer(this)
{
}

DJPlayer::~DJPlayer()
{
   if (mOwnsSample)
      delete mSample;
}

// ═══════════════════════════════════════════════════════════════════
//  UI Controls (CDJ-style layout)
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();

   // ── row 1: transport + volume ──
   BUTTON_STYLE(mPlayButton, "play", ButtonDisplayStyle::kPlay);
   UIBLOCK_SHIFTRIGHT();
   BUTTON_STYLE(mPauseButton, "pause", ButtonDisplayStyle::kPause);
   UIBLOCK_SHIFTRIGHT();
   BUTTON_STYLE(mStopButton, "stop", ButtonDisplayStyle::kStop);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mVolumeSlider, "vol", &mVolume, 0, 2);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mLoopCheckbox, "loop", &mLoop);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoadFileButton, "load");

   UIBLOCK_NEWLINE();

   // ── row 2: pitch fader + range + master tempo ──
   FLOATSLIDER(mPitchFader, "pitch", &mPitchPercent, -1, 1);
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mPitchRangeDropdown, "range", (int*)&mPitchRange, 50);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mMasterTempoCheckbox, "key lock", &mMasterTempo);

   UIBLOCK_NEWLINE();

   // ── row 3: BPM + zoom ──
   FLOATSLIDER(mSampleBPMSlider, "sample bpm", &mSampleBPM, 20, 300);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mScrollZoomSlider, "zoom", &mScrollZoomBeats, 4, 16);

   UIBLOCK_NEWLINE();

   // ── row 4: nudge + loop ──
   BUTTON(mNudgeBackwardButton, "<< nudge");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mNudgeForwardButton, "nudge >>");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopInButton, "loop in");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopOutButton, "loop out");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopAuto1, "1");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopAuto2, "2");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopAuto4, "4");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopAuto8, "8");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopAuto16, "16");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoopClearButton, "clr loop");

   UIBLOCK_NEWLINE();

   // ── row 5: cue mode + hot cue buttons ──
   DROPDOWN(mCueModeDropdown, "cue", (int*)&mCueMode, 60);
   UIBLOCK_SHIFTRIGHT();
   for (int i = 0; i < 8; ++i)
   {
      UIBLOCK_SHIFTRIGHT();
      BUTTON(mHotCueButtons[i], ("cue " + ofToString(i)).c_str());
   }
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mClearCuesButton, "clear");

   ENDUIBLOCK0();

   // pitch range dropdown labels
   mPitchRangeDropdown->AddLabel(PitchRangeLabel(PitchRange::Six), (int)PitchRange::Six);
   mPitchRangeDropdown->AddLabel(PitchRangeLabel(PitchRange::Ten), (int)PitchRange::Ten);
   mPitchRangeDropdown->AddLabel(PitchRangeLabel(PitchRange::Sixteen), (int)PitchRange::Sixteen);
   mPitchRangeDropdown->AddLabel(PitchRangeLabel(PitchRange::Wide), (int)PitchRange::Wide);

   mCueModeDropdown->AddLabel("jump", (int)CueMode::Jump);
   mCueModeDropdown->AddLabel("set", (int)CueMode::Set);
   mCueModeDropdown->AddLabel("edit", (int)CueMode::Edit);
   mCueModeDropdown->AddLabel("delete", (int)CueMode::Delete);

   for (int i = 0; i < 8; ++i)
      mCuePoints[i] = -1;
}

// ═══════════════════════════════════════════════════════════════════
//  Init / Poll
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::Init()
{
   IDrawableModule::Init();
}

void DJPlayer::Poll()
{
   IDrawableModule::Poll();

   if (mIsLoadingSample && mSample && !mSample->IsSampleLoading())
   {
      mIsLoadingSample = false;
      mDrawBuffer.Resize(mSample->LengthInSamples());
      mDrawBuffer.CopyFrom(mSample->Data());
   }
}

// ═══════════════════════════════════════════════════════════════════
//  Audio processing
// ═══════════════════════════════════════════════════════════════════

float DJPlayer::GetCurrentBPM() const
{
   if (mSampleBPM <= 0) return 0;
   float range = PitchRangeToFloat(mPitchRange);
   float pitchSpeed = 1.0f + mPitchPercent * range;   // 1.0 = original
   return mSampleBPM * pitchSpeed;
}

float DJPlayer::GetSamplesPerBeat() const
{
   if (mSampleBPM <= 0) return 0;
   return gSampleRate * 60.0f / mSampleBPM;
}

float DJPlayer::SampleToBeat(int sample) const
{
   if (mSample == nullptr || mSampleBPM <= 0) return 0;
   float samplesPerBeat = gSampleRate * 60.0f / mSampleBPM;
   int preRollSamples = (int)(kPreRollSeconds * gSampleRate);
   return (sample + preRollSamples) / samplesPerBeat;
}

int DJPlayer::BeatToSample(float beat) const
{
   if (mSample == nullptr || mSampleBPM <= 0) return 0;
   float samplesPerBeat = gSampleRate * 60.0f / mSampleBPM;
   int preRollSamples = (int)(kPreRollSeconds * gSampleRate);
   return (int)(beat * samplesPerBeat) - preRollSamples;
}

void DJPlayer::SetAutoLoop(int beats)
{
   if (mSample == nullptr || mSampleBPM <= 0) return;
   float samplesPerBeat = GetSamplesPerBeat();
   if (samplesPerBeat <= 0) return;
   mLoopIn = mSample->GetPlayPosition();
   mLoopOut = mLoopIn + (int)(samplesPerBeat * beats);
   if (mLoopOut > mSample->LengthInSamples())
      mLoopOut = mSample->LengthInSamples();
   mLoopActive = true;
   mLoopBeats = beats;
   SaveAnalysisFile();
}

void DJPlayer::AnalyzeBPM()
{
   if (mSample == nullptr || mSample->LengthInSamples() <= 0) return;

   int totalSamples = mSample->LengthInSamples();
   int analyzeSamples = juce::jmin(totalSamples, (int)(gSampleRate * 30)); // analyze first 30 seconds
   if (analyzeSamples < (int)(gSampleRate * 2)) return; // need at least 2 seconds

   const float* data = mSample->Data()->GetChannel(0);
   if (data == nullptr) return;

   // ═══════════════════════════════════════════════════
   //  Step 1: onset strength signal (half-wave rectified spectral flux)
   // ═══════════════════════════════════════════════════
   int windowSize = 1024;
   int hopSize = 512;
   int numFrames = (analyzeSamples - windowSize) / hopSize;
   if (numFrames <= 0) return;

   // compute energy per frame
   std::vector<float> energy(numFrames);
   for (int i = 0; i < numFrames; ++i)
   {
      float sum = 0;
      int offset = i * hopSize;
      for (int j = 0; j < windowSize && offset + j < analyzeSamples; ++j)
      {
         float s = data[offset + j];
         sum += s * s;
      }
      energy[i] = sqrtf(sum / windowSize); // RMS
   }

   // onset strength: half-wave rectified first derivative
   std::vector<float> onset(numFrames, 0);
   for (int i = 1; i < numFrames; ++i)
   {
      float diff = energy[i] - energy[i - 1];
      if (diff > 0) onset[i] = diff;
   }

   // adaptive threshold (moving average)
   int avgWindow = 16;
   std::vector<float> threshold(numFrames, 0);
   for (int i = avgWindow; i < numFrames; ++i)
   {
      float sum = 0;
      for (int j = i - avgWindow; j < i; ++j)
         sum += onset[j];
      threshold[i] = sum / avgWindow * 1.4f;
   }

   // ═══════════════════════════════════════════════════
   //  Step 2: autocorrelation of onset signal
   // ═══════════════════════════════════════════════════
   // BPM range: 60-200 => period range in frames
   float minBPM = 60.0f;
   float maxBPM = 200.0f;
   float framesPerSec = (float)gSampleRate / hopSize;
   int minLag = (int)(framesPerSec * 60.0f / maxBPM); // shortest period
   int maxLag = (int)(framesPerSec * 60.0f / minBPM); // longest period

   std::vector<float> acf(maxLag + 1, 0);
   for (int lag = minLag; lag <= maxLag; ++lag)
   {
      float sum = 0;
      int count = numFrames - lag;
      for (int i = 0; i < count; ++i)
         sum += onset[i] * onset[i + lag];
      acf[lag] = sum / count;
   }

   // normalize ACF
   float acfMax = 0;
   for (int i = minLag; i <= maxLag; ++i)
      if (acf[i] > acfMax) acfMax = acf[i];
   if (acfMax > 0)
      for (int i = minLag; i <= maxLag; ++i)
         acf[i] /= acfMax;

   // ═══════════════════════════════════════════════════
   //  Step 3: combine onset peak count with autocorrelation
   // ═══════════════════════════════════════════════════
   // find peaks in ACF
   struct BPMCandidate { float bpm; float score; int lag; };
   std::vector<BPMCandidate> candidates;

   for (int lag = minLag + 1; lag < maxLag; ++lag)
   {
      if (acf[lag] > acf[lag - 1] && acf[lag] > acf[lag + 1] && acf[lag] > 0.1f)
      {
         float bpm = framesPerSec * 60.0f / lag;
         candidates.push_back({ bpm, acf[lag], lag });
      }
   }

   if (candidates.empty()) return;

   // ═══════════════════════════════════════════════════
   //  Step 4: score candidates — prefer strong beats, penalize half/double
   // ═══════════════════════════════════════════════════
   // count actual onsets per candidate period
   std::vector<float> beatStrength(numFrames, 0);
   for (int i = 0; i < numFrames; ++i)
      beatStrength[i] = (onset[i] > threshold[i]) ? onset[i] : 0;

   for (auto& c : candidates)
   {
      int periodFrames = c.lag;
      // count how many onsets align with this period
      float alignmentScore = 0;
      int alignedBeats = 0;
      for (int pos = periodFrames; pos < numFrames; pos += periodFrames)
      {
         // check ±1 frame around expected beat
         float maxOnset = 0;
         for (int d = -1; d <= 1; ++d)
         {
            int idx = pos + d;
            if (idx >= 0 && idx < numFrames && beatStrength[idx] > maxOnset)
               maxOnset = beatStrength[idx];
         }
         if (maxOnset > 0)
         {
            alignmentScore += maxOnset;
            alignedBeats++;
         }
      }

      // normalize by number of expected beats
      int expectedBeats = numFrames / periodFrames;
      float alignmentRatio = (expectedBeats > 0) ? (float)alignedBeats / expectedBeats : 0;

      // combined score: ACF strength * alignment ratio
      c.score = c.score * 0.5f + alignmentRatio * 0.5f;
   }

   // sort by score
   std::sort(candidates.begin(), candidates.end(),
      [](const BPMCandidate& a, const BPMCandidate& b) { return a.score > b.score; });

   // ═══════════════════════════════════════════════════
   //  Step 5: pick best candidate, compute confidence
   // ═══════════════════════════════════════════════════
   float bestBPM = candidates[0].bpm;
   float bestScore = candidates[0].score;

   // check for half-time / double-time alternatives
   for (auto& c : candidates)
   {
      if (fabsf(c.bpm - bestBPM * 2.0f) < 2.0f && c.score > bestScore * 0.9f)
         bestBPM = c.bpm; // prefer the slower one
      if (fabsf(c.bpm * 2.0f - bestBPM) < 2.0f && c.score > bestScore * 0.95f)
         bestBPM = c.bpm * 2.0f; // prefer the faster one if very close
   }

   mDetectedBPM = bestBPM;
   mSampleBPM = bestBPM;
   mBPMConfidence = juce::jmin(bestScore, 1.0f);

   // ═══════════════════════════════════════════════════
   //  Step 6: detect first beat position
   // ═══════════════════════════════════════════════════
   float framesPerBeat = framesPerSec * 60.0f / mSampleBPM;
   int periodFrames = (int)(framesPerBeat + 0.5f);

   // scan first few beats to find strongest onset → align grid
   int searchWindow = periodFrames / 2;
   float strongestOnset = 0;
   int firstBeat = 0;
   for (int i = searchWindow; i < numFrames && i < periodFrames * 8; ++i)
   {
      if (beatStrength[i] > strongestOnset)
      {
         strongestOnset = beatStrength[i];
         firstBeat = i;
      }
   }
   mFirstBeatSample = firstBeat * hopSize;

   AnalyzeKey();
   SaveAnalysisFile();
}

// ═══════════════════════════════════════════════════════════════════
//  Key Detection (libkeyfinder)
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::AnalyzeKey()
{
   if (mSample == nullptr || mSample->LengthInSamples() <= 0) return;

   int totalSamples = mSample->LengthInSamples();
   int analyzeSamples = juce::jmin(totalSamples, (int)(gSampleRate * 30)); // analyze first 30 seconds
   if (analyzeSamples < (int)(gSampleRate * 2)) return; // need at least 2 seconds

   const float* rawData = mSample->Data()->GetChannel(0);
   if (rawData == nullptr) return;

   // convert to mono double for libkeyfinder
   std::vector<double> monoData(analyzeSamples);
   for (int i = 0; i < analyzeSamples; ++i)
      monoData[i] = (double)rawData[i];

   // run key detection
   KeyFinder::KeyFinder keyFinder;
   KeyFinder::AudioData audioData;
   audioData.setFrameRate((unsigned int)gSampleRate);
   audioData.setChannels(1);
   audioData.addToSampleCount((unsigned int)analyzeSamples);
   for (int i = 0; i < analyzeSamples; ++i)
      audioData.setSample(i, monoData[i]);

   KeyFinder::key_t key = keyFinder.keyOfAudio(audioData);
   mDetectedKey = (int)key;
   mKeyName = GetKeyName();
   SaveAnalysisFile();
}

std::string DJPlayer::GetKeyName() const
{
   switch (mDetectedKey)
   {
      case 0:  return "A Major";
      case 1:  return "A Minor";
      case 2:  return "Bb Major";
      case 3:  return "Bb Minor";
      case 4:  return "B Major";
      case 5:  return "B Minor";
      case 6:  return "C Major";
      case 7:  return "C Minor";
      case 8:  return "Db Major";
      case 9:  return "Db Minor";
      case 10: return "D Major";
      case 11: return "D Minor";
      case 12: return "Eb Major";
      case 13: return "Eb Minor";
      case 14: return "E Major";
      case 15: return "E Minor";
      case 16: return "F Major";
      case 17: return "F Minor";
      case 18: return "Gb Major";
      case 19: return "Gb Minor";
      case 20: return "G Major";
      case 21: return "G Minor";
      case 22: return "Ab Major";
      case 23: return "Ab Minor";
      case 24: return "Silence";
      default: return "Unknown";
   }
}

// ═══════════════════════════════════════════════════════════════════
//  Analysis File (.sjb) — Ableton-style sidecar
// ═══════════════════════════════════════════════════════════════════

std::string DJPlayer::GetAnalysisFilePath() const
{
   if (mSample == nullptr) return "";
   std::string path = mSample->GetReadPath();
   size_t dot = path.rfind('.');
   if (dot != std::string::npos)
      path = path.substr(0, dot);
   return path + ".sjb";
}

void DJPlayer::LoadAnalysisFile()
{
   if (mSample == nullptr) return;
   std::string path = GetAnalysisFilePath();
   if (path.empty()) return;

   ofxJSONElement json;
   if (json.open(path))
   {
      mSampleBPM = json["bpm"].asFloat();
      mDetectedBPM = mSampleBPM;
      for (int i = 0; i < 8; ++i)
         mCuePoints[i] = json["hotCues"][ofToString(i)].asInt();
      mLoopIn = json["loopIn"].asInt();
      mLoopOut = json["loopOut"].asInt();
      mLoopBeats = json["loopBeats"].asInt();
      mLoopActive = json["loopActive"].asBool();
      mDetectedKey = json["detectedKey"].asInt();
      mKeyName = json["keyName"].asString();
      mAnalysisFileLoaded = true;
   }
   else
   {
      AnalyzeBPM();
      SaveAnalysisFile();
   }
}

void DJPlayer::SaveAnalysisFile()
{
   if (mSample == nullptr) return;
   std::string path = GetAnalysisFilePath();
   if (path.empty()) return;

   ofxJSONElement json;
   json["version"] = 3;
   json["bpm"] = mSampleBPM;
   for (int i = 0; i < 8; ++i)
      json["hotCues"][ofToString(i)] = mCuePoints[i];
   json["loopIn"] = mLoopIn;
   json["loopOut"] = mLoopOut;
   json["loopBeats"] = mLoopBeats;
   json["loopActive"] = mLoopActive;
   json["detectedKey"] = mDetectedKey;
   json["keyName"] = mKeyName;
   json.save(path, true);
}

void DJPlayer::Process(double time)
{
   PROFILER(DJPlayer);

   IAudioReceiver* target = GetTarget();

   if (mEnabled && target != nullptr && mSample != nullptr)
   {
      mNoteInputBuffer.Process(time);
      ComputeSliders(0);
      SyncBuffers(mSample->NumChannels());

      int bufferSize = target->GetBuffer()->BufferSize();
      assert(bufferSize == gBufferSize);

      float volSq = mVolume * mVolume;

      // ── calculate final speed (CDJ-style) ──
      float range = PitchRangeToFloat(mPitchRange);
      float targetSpeed = 1.0f + mPitchPercent * range;

      // ── phase-aware nudge ──
      if (mNudgeSamplesRemaining > 0)
      {
         targetSpeed *= 1.2f;
         mNudgeSamplesRemaining -= bufferSize;
         if (mNudgeSamplesRemaining <= 0) mNudgeSamplesRemaining = 0;
      }
      else if (mNudgeSamplesRemaining < 0)
      {
         targetSpeed *= 0.8f;
         mNudgeSamplesRemaining += bufferSize;
         if (mNudgeSamplesRemaining > 0) mNudgeSamplesRemaining = 0;
      }

      // smooth blending
      {
         const float kBlendSpeed = 0.3f;
         mPlaySpeed = ofLerp(mPlaySpeed, targetSpeed, kBlendSpeed);
      }
      mPlaySpeed = ofClamp(mPlaySpeed, -5, 5);
      mSample->SetRate(mPlaySpeed);

      gWorkChannelBuffer.SetNumActiveChannels(mSample->NumChannels());

      if (mPlay)
      {
         // ── loop check (skip during pre-roll) ──
         if (mLoopActive && mLoopIn >= 0 && mLoopOut > mLoopIn && mSample != nullptr)
         {
            int pos = mSample->GetPlayPosition();
            if (pos >= 0 && (pos >= mLoopOut || pos < mLoopIn))
            {
               mSample->SetPlayPosition(mLoopIn);
               mSwitchAndRamp.StartSwitch();
            }
         }

         if (mSample->ConsumeData(time, &gWorkChannelBuffer, bufferSize, true))
         {
            for (int ch = 0; ch < gWorkChannelBuffer.NumActiveChannels(); ++ch)
               for (int i = 0; i < bufferSize; ++i)
                  gWorkChannelBuffer.GetChannel(ch)[i] *= volSq * mAdsr.Value(time + i * gInvSampleRateMs);
         }
         else
         {
            gWorkChannelBuffer.Clear();
            mPlay = false;
            mSample->SetPlayPosition(0);
            mAdsr.Stop(time);
         }
      }
      else
      {
         gWorkChannelBuffer.Clear();
      }

      for (int ch = 0; ch < gWorkChannelBuffer.NumActiveChannels(); ++ch)
      {
         for (int i = 0; i < bufferSize; ++i)
            gWorkChannelBuffer.GetChannel(ch)[i] = mSwitchAndRamp.Process(ch, gWorkChannelBuffer.GetChannel(ch)[i]);

         Add(target->GetBuffer()->GetChannel(ch), gWorkChannelBuffer.GetChannel(ch), bufferSize);
         GetVizBuffer()->WriteChunk(gWorkChannelBuffer.GetChannel(ch), bufferSize, ch);
      }
   }

   GetBuffer()->Reset();
}

// ═══════════════════════════════════════════════════════════════════
//  Note / Pulse
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (!mEnabled) return;

   if (!NoteInputBuffer::IsTimeWithinFrame(time) && GetTarget() && mSample)
   {
      mNoteInputBuffer.QueueNote(time, pitch, velocity, voiceIdx, modulation);
      return;
   }

   if (velocity > 0 && mSample != nullptr)
   {
      mSwitchAndRamp.StartSwitch();
      mPlay = true;
      mAdsr.Clear();
      mAdsr.Start(time, velocity / 127.0f);

      // hot cue 0-7
      if (pitch >= 0 && pitch < 8 && mCuePoints[pitch] >= 0)
         mSample->SetPlayPosition(mCuePoints[pitch]);
      else
         mSample->SetPlayPosition(0);
   }

   if (velocity == 0)
      mAdsr.Stop(time);
}

void DJPlayer::OnPulse(double time, float velocity, int flags)
{
   if (mSample != nullptr)
   {
      mPlay = true;
      mAdsr.Clear();
      mAdsr.Start(time, velocity);
      mSample->SetPlayPosition(0);
      mSwitchAndRamp.StartSwitch();
   }
}

// ═══════════════════════════════════════════════════════════════════
//  Button / Dropdown
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::ButtonClicked(ClickButton* button, double time)
{
   // ── transport ──
   if (button == mPlayButton && mSample != nullptr)
   {
      mSwitchAndRamp.StartSwitch();
      mPlay = true;
      mAdsr.Clear();
      mAdsr.Start(time, 1);
   }
   if (button == mPauseButton && mSample != nullptr)
   {
      mPlay = false;
      mSwitchAndRamp.StartSwitch();
   }
   if (button == mStopButton && mSample != nullptr)
   {
      mPlay = false;
      mSwitchAndRamp.StartSwitch();
   }
   if (button == mLoadFileButton)
      LoadFile();

   // ── nudge (phase-aware: shift 1/4 beat) ──
   if (button == mNudgeForwardButton)
   {
      mNudgeForwardHeld = (mNudgeForwardHeld > 0.5f) ? 0 : 1;
      if (mNudgeForwardHeld > 0.5f)
      {
         mNudgeBackwardHeld = 0;
         float bpm = mSampleBPM;
         if (bpm > 0 && mSample != nullptr)
         {
            int samplesPerBeat = (int)(gSampleRate * 60.0f / bpm + 0.5f);
            mNudgeSamplesRemaining = samplesPerBeat / 4;
         }
      }
      else
      {
         mNudgeSamplesRemaining = 0;
      }
   }
   if (button == mNudgeBackwardButton)
   {
      mNudgeBackwardHeld = (mNudgeBackwardHeld > 0.5f) ? 0 : 1;
      if (mNudgeBackwardHeld > 0.5f)
      {
         mNudgeForwardHeld = 0;
         float bpm = mSampleBPM;
         if (bpm > 0 && mSample != nullptr)
         {
            int samplesPerBeat = (int)(gSampleRate * 60.0f / bpm + 0.5f);
            mNudgeSamplesRemaining = -(samplesPerBeat / 4);
         }
      }
      else
      {
         mNudgeSamplesRemaining = 0;
      }
   }

   // ── hot cues ──
   for (int i = 0; i < 8; ++i)
   {
      if (button == mHotCueButtons[i] && mSample != nullptr)
      {
         switch (mCueMode)
         {
            case CueMode::Jump:
               if (mCuePoints[i] >= 0)
               {
                  mSample->SetPlayPosition(mCuePoints[i]);
                  mActiveHotCue = i;
               }
               break;
            case CueMode::Set:
               mCuePoints[i] = mSample->GetPlayPosition();
               mActiveHotCue = i;
               SaveAnalysisFile();
               break;
            case CueMode::Delete:
               mCuePoints[i] = -1;
               SaveAnalysisFile();
               break;
            default:
               break;
         }
      }
   }

   if (button == mClearCuesButton)
   {
      for (int i = 0; i < 8; ++i)
         mCuePoints[i] = -1;
      SaveAnalysisFile();
   }

   // ── loop controls ──
   if (button == mLoopInButton && mSample != nullptr)
   {
      mLoopIn = mSample->GetPlayPosition();
      if (mLoopOut > mLoopIn)
         mLoopActive = true;
      SaveAnalysisFile();
   }
   if (button == mLoopOutButton && mSample != nullptr)
   {
      mLoopOut = mSample->GetPlayPosition();
      if (mLoopIn >= 0 && mLoopOut > mLoopIn)
         mLoopActive = true;
      SaveAnalysisFile();
   }
   if (button == mLoopAuto1) SetAutoLoop(1);
   if (button == mLoopAuto2) SetAutoLoop(2);
   if (button == mLoopAuto4) SetAutoLoop(4);
   if (button == mLoopAuto8) SetAutoLoop(8);
   if (button == mLoopAuto16) SetAutoLoop(16);
   if (button == mLoopClearButton)
   {
      mLoopIn = -1;
      mLoopOut = -1;
      mLoopActive = false;
      SaveAnalysisFile();
   }
}

void DJPlayer::DropdownClicked(DropdownList* list) {}

void DJPlayer::DropdownUpdated(DropdownList* list, int oldVal, double time) {}

void DJPlayer::KeyPressed(int key, bool isRepeat)
{
   IDrawableModule::KeyPressed(key, isRepeat);
}

// ═══════════════════════════════════════════════════════════════════
//  File / Sample
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::FilesDropped(std::vector<std::string> files, int x, int y)
{
   Sample* sample = new Sample();
   sample->Read(files[0].c_str());
   UpdateSample(sample, true);
}

void DJPlayer::SampleDropped(int x, int y, Sample* sample)
{
   if (TheSynth->MouseMovedSignificantlySincePressed())
   {
      Sample* copy = new Sample();
      copy->CopyFrom(sample);
      UpdateSample(copy, true);
   }
}

void DJPlayer::UpdateSample(Sample* sample, bool ownsSample)
{
   Sample* oldSamplePtr = mSample;
   bool ownedOldSample = mOwnsSample;

   sample->SetPlayPosition(0);
   sample->SetLooping(mLoop);
   mSample = sample;
   mPlay = false;
   mOwnsSample = ownsSample;
   mErrorString = "";
   mAnalysisFileLoaded = false;

   if (ownedOldSample)
      delete oldSamplePtr;

   mIsLoadingSample = true;
   LoadAnalysisFile();
}

void DJPlayer::LoadFile()
{
   auto file_pattern = TheSynth->GetAudioFormatManager().getWildcardForAllFormats();
   if (File::areFileNamesCaseSensitive())
      file_pattern += ";" + file_pattern.toUpperCase();
   FileChooser chooser("Load sample", File(ofToSamplePath("")),
                       file_pattern, true, false, TheSynth->GetFileChooserParent());
   if (chooser.browseForFileToOpen())
   {
      auto file = chooser.getResult();
      Sample* sample = new Sample();
      if (file.existsAsFile())
         sample->Read(file.getFullPathName().toStdString().c_str());
      UpdateSample(sample, true);
   }
}

// ═══════════════════════════════════════════════════════════════════
//  Mouse / Waveform
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   mLastMouseX = x;
   mLastMouseY = y;

   if (mSample == nullptr || gHoveredUIControl != nullptr)
      return;

   // ── single click: check for cue point drag (edit mode) ──
   if (mCueMode == CueMode::Edit)
   {
      int totalSamples = mSample->LengthInSamples();
      int preRollSamples = (int)(kPreRollSeconds * gSampleRate);
      float sampleWidth = mWidth - 10;
      float clickSample = GetPlayPositionForMouse(x, y);
      float clickBeat = SampleToBeat((int)clickSample);
      float pixelTolerance = 10.0f;

      int closestIdx = -1;
      float closestDist = pixelTolerance;
      for (int i = 0; i < 8; ++i)
      {
         if (mCuePoints[i] >= 0)
         {
            float cueBeat = SampleToBeat(mCuePoints[i]);
            float pixelDist = fabs(cueBeat - clickBeat) / (SampleToBeat(totalSamples) - SampleToBeat(-preRollSamples)) * sampleWidth;
            if (pixelDist < closestDist)
            {
               closestDist = pixelDist;
               closestIdx = i;
            }
         }
      }
      if (closestIdx >= 0)
      {
         mDraggingCue = true;
         mDragCueIndex = closestIdx;
      }
   }
   else if (IsInOverviewRegion(y))
   {
      // overview waveform: jump on click, scrub on drag
      mScratchStartX = x;
      mScratchStartY = y;
      mScratchStartPlayPos = mSample->GetPlayPosition();
      mScrubbingSample = true;
      mOverviewDragged = false;
      mOverviewClickSample = (int)GetPlayPositionForMouse(x, y);
   }
   else
   {
      // zoom waveform: click = jump, drag = scrub playhead
      mScratchStartX = x;
      mScratchStartY = y;
      mScratchStartPlayPos = mSample->GetPlayPosition();
      mScrubbingSample = true;
      mOverviewDragged = false;
      mOverviewClickSample = (int)GetPlayPositionForMouse(x, y);
   }
}

bool DJPlayer::MouseMoved(float x, float y)
{
   IDrawableModule::MouseMoved(x, y);
   mLastMouseX = x;
   mLastMouseY = y;

   if (mSample == nullptr) return true;

   if (mDraggingCue)
   {
      int newPos = (int)ofClamp(GetPlayPositionForMouse(x, y), 0, mSample->LengthInSamples() - 1);
      mCuePoints[mDragCueIndex] = newPos;
   }
   else if (mScrubbingSample)
   {
      // check if dragged beyond click threshold
      if (fabs(x - mScratchStartX) > 3)
         mOverviewDragged = true;

      int totalSamples = mSample->LengthInSamples();
      int preRollSamples = (int)(kPreRollSeconds * gSampleRate);

      if (!IsInOverviewRegion(mScratchStartY))
      {
         // zoom region: linear mapping
         float sampleWidth = mWidth - 10;
         float frac = (x - 5) / sampleWidth;
         int zoomStart = GetZoomStartSample();
         int zoomEnd = GetZoomEndSample();
         int newPos = (int)(zoomStart + frac * (zoomEnd - zoomStart));
         newPos = ofClamp(newPos, -preRollSamples, totalSamples - 1);

         mSwitchAndRamp.StartSwitch();
         mSample->SetPlayPosition(newPos);
      }
      else
      {
         // overview region: linear mapping
         float sampleWidth = mWidth - 10;
         float samplesPerPixel = (float)(totalSamples + preRollSamples) / sampleWidth;
         float deltaX = x - mScratchStartX;
         int deltaSamples = (int)(deltaX * samplesPerPixel);
         int newPos = mScratchStartPlayPos + deltaSamples;
         newPos = (int)ofClamp(newPos, -preRollSamples, totalSamples - 1);

         mSwitchAndRamp.StartSwitch();
         mSample->SetPlayPosition(newPos);
      }
   }
   return true;
}

bool DJPlayer::MouseScrolled(float x, float y, float scrollX, float scrollY, bool isSmoothScroll, bool isInvertedScroll)
{
   if (mSample == nullptr) return false;

   if (GetKeyModifiers() & kModifier_Shift)
   {
      // ── Shift+scrollY: zoom in/out ──
      float zoomDelta = scrollY * 0.5f;
      if (GetKeyModifiers() & kModifier_Command)
         zoomDelta *= 2.0f;
      mScrollZoomBeats = ofClamp(mScrollZoomBeats - zoomDelta, 4, 16);
   }
   else
   {
      // ── scrollY: nudge (jogwheel-style) ──
      // each scroll tick adds ±1/16 beat to the nudge accumulator
      float bpm = mSampleBPM;
      if (bpm <= 0) return false;

      int samplesPerBeat = (int)(gSampleRate * 60.0f / bpm + 0.5f);
      int nudgePerTick = samplesPerBeat / 16;  // 1/16 beat per scroll tick

      if (GetKeyModifiers() & kModifier_Command)
         nudgePerTick = samplesPerBeat / 4;  // coarse: 1/4 beat per tick

      // accumulator: add to existing nudge (fast scroll = more nudge)
      mNudgeSamplesRemaining += (int)(scrollY * nudgePerTick);

      // clamp to prevent runaway accumulation (max ±1 beat)
      mNudgeSamplesRemaining = (int)ofClamp(mNudgeSamplesRemaining, -samplesPerBeat, samplesPerBeat);
   }

   // ── scrollX: zoom in/out ──
   if (scrollX != 0)
   {
      float zoomDelta = scrollX * 0.5f;
      if (GetKeyModifiers() & kModifier_Command)
         zoomDelta *= 2.0f;
      mScrollZoomBeats = ofClamp(mScrollZoomBeats - zoomDelta, 4, 16);
   }

   return true;
}

void DJPlayer::MouseReleased()
{
   IDrawableModule::MouseReleased();

   if (mScrubbingSample)
   {
      if (mOverviewDragged && mSample != nullptr)
      {
         // drag finished — playhead already at dragged position, keep it
      }
      else if (mSample != nullptr)
      {
         // no drag — click = jump playhead to clicked position (allow pre-roll)
         mSwitchAndRamp.StartSwitch();
         int preRollSamples = (int)(kPreRollSeconds * gSampleRate);
         int pos = ofClamp(mOverviewClickSample, -preRollSamples, mSample->LengthInSamples() - 1);
         mSample->SetPlayPosition(pos);
      }
      mScrubbingSample = false;
      mOverviewDragged = false;
   }

   if (mDraggingCue)
   {
      mDraggingCue = false;
      mDragCueIndex = -1;
      SaveAnalysisFile();
   }
}

float DJPlayer::GetPlayPositionForMouse(float mouseX, float mouseY) const
{
   if (mSample == nullptr) return 0;
   float sampleWidth = mWidth - 10;
   int totalSamples = mSample->LengthInSamples();
   int preRollSamples = (int)(kPreRollSeconds * gSampleRate);

   if (IsInOverviewRegion(mouseY))
   {
      // overview: linear sample mapping from -preRollSamples to totalSamples
      float frac = (mouseX - 5) / sampleWidth;
      return -preRollSamples + frac * (totalSamples + preRollSamples);
   }
   else
   {
      // zoom: linear sample mapping within zoom range
      float frac = (mouseX - 5) / sampleWidth;
      int zoomStart = GetZoomStartSample();
      int zoomEnd = GetZoomEndSample();
      return zoomStart + frac * (zoomEnd - zoomStart);
   }
}

bool DJPlayer::IsInOverviewRegion(float mouseY) const
{
   float totalHeight = mHeight - 137;
   float halfH = totalHeight / 2 - 2;
   return (mouseY < 127 + halfH);
}

bool DJPlayer::IsInZoomRegion(float mouseY) const
{
   return !IsInOverviewRegion(mouseY);
}

int DJPlayer::GetZoomStartSample() const
{
   if (mSample == nullptr) return 0;
   int totalSamples = mSample->LengthInSamples();

   float bpm = mSampleBPM;
   if (bpm <= 0) return 0;
   float samplesPerBeat = gSampleRate * 60.0f / bpm;
   int visibleSamples = (int)(samplesPerBeat * mScrollZoomBeats);

   // always center on playhead (allow negative for pre-roll)
   int center = mSample->GetPlayPosition();
   int start = center - visibleSamples / 2;
   if (start > totalSamples - visibleSamples) start = totalSamples - visibleSamples;
   return start;
}

int DJPlayer::GetZoomEndSample() const
{
   if (mSample == nullptr) return 0;
   int totalSamples = mSample->LengthInSamples();

   float bpm = mSampleBPM;
   if (bpm <= 0) return 1;
   float samplesPerBeat = gSampleRate * 60.0f / bpm;
   int visibleSamples = (int)(samplesPerBeat * mScrollZoomBeats);

   int start = GetZoomStartSample();
   int end = start + visibleSamples;
   if (end > totalSamples) end = totalSamples;
   return end;
}

float DJPlayer::GetZoomStartSeconds() const
{
   if (mSample == nullptr) return 0;
   return GetZoomStartSample() / (gSampleRate * mSample->GetSampleRateRatio());
}

float DJPlayer::GetZoomEndSeconds() const
{
   if (mSample == nullptr) return 1;
   return GetZoomEndSample() / (gSampleRate * mSample->GetSampleRateRatio());
}

// ═══════════════════════════════════════════════════════════════════
//  Checkbox / Slider
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (checkbox == mLoopCheckbox && mSample != nullptr)
      mSample->SetLooping(mLoop);
}

void DJPlayer::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (slider == mSampleBPMSlider)
      SaveAnalysisFile();
}

// ═══════════════════════════════════════════════════════════════════
//  RGB Waveform (frequency-band coloring)
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::DrawRGBWaveform(float x, float y, float w, float h, int startSample, int numSamples)
{
   if (mDrawBuffer.BufferSize() <= 0) return;

   int totalSamples = mDrawBuffer.BufferSize();
   if (startSample < 0) startSample = 0;
   if (numSamples < 0) numSamples = totalSamples - startSample;
   int endSample = startSample + numSamples;
   if (endSample > totalSamples) endSample = totalSamples;
   startSample = (int)ofClamp(startSample, 0, totalSamples - 1);
   if (endSample <= startSample) return;

   int numChannels = mDrawBuffer.NumActiveChannels();
   if (numChannels <= 0) return;

   const float* data = mDrawBuffer.GetChannel(0);
   if (data == nullptr) return;

   float samplesPerPixel = (float)(endSample - startSample) / w;
   if (samplesPerPixel <= 0) return;
   float cy = y + h / 2.0f;

   ofPushStyle();
   ofFill();

   // draw center line
   ofSetColor(255, 255, 255, 20);
   ofLine(x, cy, x + w, cy);

   for (float px = 0; px < w; px += 1)
   {
      int sampleIdx = startSample + (int)(px * samplesPerPixel);
      if (sampleIdx < startSample || sampleIdx >= endSample) continue;

      int nextSample = startSample + (int)((px + 1) * samplesPerPixel);
      if (nextSample >= endSample) nextSample = endSample - 1;
      int blockLen = juce::jmax(1, juce::jmin(nextSample - sampleIdx, 64));
      if (sampleIdx + blockLen > endSample) blockLen = endSample - sampleIdx;

      // ── find min/max for this pixel (shows actual waveform shape) ──
      float sampleMin = data[sampleIdx];
      float sampleMax = data[sampleIdx];
      float rmsSum = 0;

      // ── frequency band energy via simple filtering ──
      float lowE = 0, midE = 0, highE = 0;
      for (int j = 0; j < blockLen; ++j)
      {
         float s = data[sampleIdx + j];
         if (s < sampleMin) sampleMin = s;
         if (s > sampleMax) sampleMax = s;
         rmsSum += s * s;

         float prev = (sampleIdx + j > 0) ? data[sampleIdx + j - 1] : 0;
         float diff1 = s - prev;
         float prev2 = (sampleIdx + j > 1) ? data[sampleIdx + j - 2] : prev;
         float diff2 = s - 2 * prev + prev2;

         lowE += s * s;
         midE += diff1 * diff1;
         highE += diff2 * diff2;
      }

      lowE = sqrtf(lowE / blockLen);
      midE = sqrtf(midE / blockLen);
      highE = sqrtf(highE / blockLen);

      float maxBand = juce::jmax(lowE, midE, highE, 0.001f);
      lowE /= maxBand;
      midE /= maxBand;
      highE /= maxBand;

      float rms = sqrtf(rmsSum / blockLen);
      float amp = juce::jmin(rms * 2.5f, 1.0f);

      int r = (int)(lowE * 255 * juce::jmin(amp * 4.0f, 1.0f));
      int g = (int)(midE * 255 * juce::jmin(amp * 4.0f, 1.0f));
      int b = (int)(highE * 255 * juce::jmin(amp * 4.0f, 1.0f));

      float brightness = sqrtf(r * r + g * g + b * b) / 255.0f;
      if (brightness < 0.15f && rms > 0.001f)
      {
         int gray = (int)(amp * 120);
         r = gray; g = gray; b = gray;
      }

      int alpha = (int)juce::jmin(amp * 600.0f, 255.0f);

      float topH = sampleMax * h / 2.0f;
      float botH = sampleMin * h / 2.0f;

      float minH = 1.0f;
      if (topH - botH < minH)
      {
         float mid = (topH + botH) / 2.0f;
         topH = mid + minH / 2.0f;
         botH = mid - minH / 2.0f;
      }

      ofSetColor(r, g, b, alpha);
      ofRect(x + px, cy - topH, 1.0f, topH - botH);
   }

   ofPopStyle();
}

// ═══════════════════════════════════════════════════════════════════
//  Draw (CDJ-style display)
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::DrawModule()
{
   if (Minimized() || !IsVisible()) return;

   // ── draw all controls ──
   mPlayButton->Draw();
   mPauseButton->Draw();
   mStopButton->Draw();
   mVolumeSlider->Draw();
   mLoopCheckbox->Draw();
   mLoadFileButton->Draw();
   mPitchFader->Draw();
   mPitchRangeDropdown->Draw();
   mMasterTempoCheckbox->Draw();
   mSampleBPMSlider->Draw();
   mScrollZoomSlider->Draw();
   mNudgeBackwardButton->Draw();
   mNudgeForwardButton->Draw();
   mLoopInButton->Draw();
   mLoopOutButton->Draw();
   mLoopAuto1->Draw();
   mLoopAuto2->Draw();
   mLoopAuto4->Draw();
   mLoopAuto8->Draw();
   mLoopAuto16->Draw();
   mLoopClearButton->Draw();
   mCueModeDropdown->Draw();
   for (int i = 0; i < 8; ++i)
      mHotCueButtons[i]->Draw();
   mClearCuesButton->Draw();

   // ── CDJ info bar ──
   float infoY = 105;
   ofPushStyle();
   ofFill();
   ofSetColor(0, 0, 0, 200);
   ofRect(5, infoY, mWidth - 10, 20);

   float range = PitchRangeToFloat(mPitchRange);
   float pitchDisplay = mPitchPercent * range * 100.0f;
   float currentBPM = GetCurrentBPM();
   float masterBPM = TheTransport ? TheTransport->GetTempo() : 0;

   // line 1: BPM + pitch + key + confidence
   ofSetColor(200, 200, 200);
   std::string line1 = "BPM " + ofToString(currentBPM, 1);
   if (mDetectedBPM > 0 && mBPMConfidence > 0)
   {
      int confPct = (int)(mBPMConfidence * 100);
      if (confPct >= 80)
         ofSetColor(0, 255, 100);
      else if (confPct >= 50)
         ofSetColor(255, 200, 0);
      else
         ofSetColor(255, 100, 100);
      line1 += " [" + ofToString(confPct) + "%]";
      ofSetColor(200, 200, 200);
   }
   line1 += "  pitch " + ofToString(pitchDisplay, 1) + "%";
   if (mDetectedKey >= 0 && mDetectedKey < 24)
      line1 += "  KEY " + mKeyName;
   DrawTextNormal(line1, 10, infoY + 9, 8);

   // line 2: nudge
   ofSetColor(160, 160, 160);
   std::string line2;
   if (mMasterTempo) line2 += "[KEY LOCK]  ";
   if (mNudgeForwardHeld > 0.5f || mNudgeBackwardHeld > 0.5f)
      line2 += "[NUDGE]  ";
   else if (mNudgeSamplesRemaining > 0)
      line2 += "[NUDGE FWD]  ";
   else if (mNudgeSamplesRemaining < 0)
      line2 += "[NUDGE BWD]  ";
   if (!line2.empty())
      DrawTextNormal(line2, 10, infoY + 18, 7);
   ofPopStyle();

   // ── waveform display (dual: overview top, zoom bottom) ──
   ofPushMatrix();
   ofTranslate(5, 127);
   float sampleWidth = mWidth - 10;
   float totalHeight = mHeight - 137;
   float halfH = totalHeight / 2 - 2;

   if (mIsLoadingSample && mSample && mSample->IsSampleLoading())
   {
      ofPushStyle();
      ofFill();
      ofSetColor(255, 255, 255, 50);
      ofRect(0, 0, sampleWidth * mSample->GetSampleLoadProgress(), totalHeight);
      ofSetColor(40, 40, 40);
      DrawTextNormal("loading...", 10, 10, 8);
      ofPopStyle();
   }
   else if (mErrorString != "")
   {
      ofPushStyle();
      ofFill();
      ofSetColor(255, 255, 255, 50);
      ofRect(0, 0, sampleWidth, totalHeight);
      ofSetColor(220, 0, 0);
      DrawTextNormal(mErrorString, 10, 10, 8);
      ofPopStyle();
   }
   else if (mSample && mSample->LengthInSamples() > 0)
   {
      int playPosition = mSample->GetPlayPosition();
      bool isScrubbing = (mScrubbingSample || mOverviewDragged);
      if (mAdsr.Value(gTime) == 0 && !isScrubbing) playPosition = -1;

      int zoomStart = GetZoomStartSample();
      int zoomEnd = GetZoomEndSample();
      int totalSamples = mSample->LengthInSamples();
      int preRollSamples = (int)(kPreRollSeconds * gSampleRate);

      // ── top half: overview with pre-roll ──
      ofPushMatrix();
      ofPushStyle();
      ofFill();
      ofSetColor(0, 0, 0, 180);
      ofRect(0, 0, sampleWidth, halfH);

      // pre-roll region
      float preRollPixels = (float)preRollSamples / (preRollSamples + totalSamples) * sampleWidth;
      ofSetColor(20, 20, 30, 200);
      ofRect(0, 0, preRollPixels, halfH);

      // draw waveform after pre-roll (linear sample mapping)
      if (totalSamples > 0)
      {
         float overviewWidth = sampleWidth - preRollPixels;
         float samplesPerPixel = (float)totalSamples / overviewWidth;
         DrawRGBWaveform(preRollPixels, 0, overviewWidth, halfH);
      }

      // playhead on overview (linear)
      if (playPosition >= -preRollSamples)
      {
         float frac = (float)(playPosition + preRollSamples) / (totalSamples + preRollSamples);
         float px = frac * sampleWidth;
         ofSetColor(0, 255, 100, 200);
         ofSetLineWidth(2);
         ofLine(px, 0, px, halfH);
      }

      // overview tempo grid (linear — evenly spaced beats)
      {
         float masterBPM = TheTransport ? TheTransport->GetTempo() : mSampleBPM;
         if (masterBPM > 0 && totalSamples > 0)
         {
            float samplesPerBeat = gSampleRate * 60.0f / mSampleBPM;
            if (samplesPerBeat > 0)
            {
               float overviewStartBeat = SampleToBeat(-preRollSamples);
               float overviewEndBeat = SampleToBeat(totalSamples);
               float firstBeat = ceilf(overviewStartBeat);
               for (float beat = firstBeat; beat <= overviewEndBeat; beat += 1.0f)
               {
                  int beatNum = (int)roundf(beat);
                  float frac = (beat - overviewStartBeat) / (overviewEndBeat - overviewStartBeat);
                  float bx = frac * sampleWidth;
                  bool isDownbeat = (beatNum % 4 == 0);
                  if (isDownbeat)
                  {
                     ofSetColor(255, 80, 80, 140);
                     ofSetLineWidth(1);
                     ofLine(bx, 0, bx, halfH);
                     int barNum = (beatNum >= 0) ? (beatNum / 4 + 1) : -((-beatNum + 3) / 4);
                     ofSetColor(255, 80, 80, 100);
                     DrawTextNormal(ofToString(barNum), bx + 2, 10, 7);
                  }
                  else
                  {
                     ofSetColor(255, 255, 255, 30);
                     ofSetLineWidth(1);
                     ofLine(bx, 0, bx, halfH);
                  }
               }
            }
         }
         ofSetLineWidth(1);
      }

      // overview cue markers (linear)
      {
         float overviewStartBeat = SampleToBeat(-preRollSamples);
         float overviewEndBeat = SampleToBeat(totalSamples);
         for (int i = 0; i < 8; ++i)
         {
            if (mCuePoints[i] >= 0)
            {
               float cueBeat = SampleToBeat(mCuePoints[i]);
               float frac = (cueBeat - overviewStartBeat) / (overviewEndBeat - overviewStartBeat);
               float cx = frac * sampleWidth;
               ofSetColor(0, 200, 255, 150);
               ofSetLineWidth(1);
               ofLine(cx, 0, cx, halfH);
               ofSetColor(0, 200, 255, 180);
               DrawTextNormal(ofToString(i + 1), cx + 2, 10, 7);
            }
         }
      }

      // "FULL" label
      ofSetColor(255, 255, 255, 60);
      DrawTextNormal("FULL", sampleWidth - 25, 10, 7);
      ofPopStyle();
      ofPopMatrix();

      // ── bottom half: zoom (always centered on playhead) ──
      float zoomY = halfH + 4;
      ofPushMatrix();
      ofTranslate(0, zoomY);
      ofPushStyle();
      ofFill();
      ofSetColor(0, 0, 0, 200);
      ofRect(0, 0, sampleWidth, halfH);

      // pre-roll region in zoom (darker, striped, linear)
      if (zoomStart < 0)
      {
         float frac = (float)(-zoomStart) / (zoomEnd - zoomStart);
         float preRollEndPx = frac * sampleWidth;
         if (preRollEndPx > 0)
         {
            ofSetColor(20, 20, 30, 200);
            ofRect(0, 0, preRollEndPx, halfH);
            ofSetColor(255, 80, 80, 120);
            ofSetLineWidth(1);
            for (float sy = 0; sy < halfH; sy += 6)
               ofLine(preRollEndPx, sy, preRollEndPx, sy + 3);
         }
      }

      {
         DrawRGBWaveform(0, 0, sampleWidth, halfH, zoomStart, zoomEnd - zoomStart);
      }

      // center playhead (always visible)
      {
         float cx = sampleWidth / 2;
         ofSetColor(0, 255, 100, 220);
         ofSetLineWidth(2);
         ofLine(cx, 0, cx, halfH);
         ofFill();
         ofSetColor(0, 255, 100, 220);
         ofTriangle(cx - 4, 0, cx + 4, 0, cx, 5);
      }

      // loop region overlay (linear)
      if (mLoopActive && mLoopIn >= 0 && mLoopOut > mLoopIn)
      {
         float frac1 = (float)(mLoopIn - zoomStart) / (zoomEnd - zoomStart);
         float frac2 = (float)(mLoopOut - zoomStart) / (zoomEnd - zoomStart);
         float lx1 = frac1 * sampleWidth;
         float lx2 = frac2 * sampleWidth;
         if (lx2 >= 0 && lx1 <= sampleWidth)
         {
            ofFill();
            ofSetColor(0, 180, 255, 30);
            ofRect(lx1, 0, lx2 - lx1, halfH);
            ofNoFill();
            ofSetColor(0, 180, 255, 150);
            ofSetLineWidth(1);
            ofRect(lx1, 0, lx2 - lx1, halfH);
            ofSetColor(255, 255, 255, 80);
            DrawTextNormal("LOOP " + ofToString(mLoopBeats), lx1 + 3, halfH - 5, 7);
         }
      }

      // zoom tempo grid (linear — evenly spaced beats)
      {
         float masterBPM = TheTransport ? TheTransport->GetTempo() : mSampleBPM;
         if (masterBPM > 0)
         {
            float zoomStartBeat = SampleToBeat(zoomStart);
            float zoomEndBeat = SampleToBeat(zoomEnd);
            float firstBeat = ceilf(zoomStartBeat);
            for (float beat = firstBeat; beat <= zoomEndBeat; beat += 1.0f)
            {
               int beatNum = (int)roundf(beat);
               float frac = (beat - zoomStartBeat) / (zoomEndBeat - zoomStartBeat);
               float bx = frac * sampleWidth;
               bool isDownbeat = (beatNum % 4 == 0);
               if (isDownbeat)
               {
                  ofSetColor(255, 80, 80, 220);
                  ofSetLineWidth(1);
                  ofLine(bx, 0, bx, halfH);
                  int barNum = (beatNum >= 0) ? (beatNum / 4 + 1) : -((-beatNum + 3) / 4);
                  ofSetColor(255, 80, 80, 180);
                  DrawTextNormal(ofToString(barNum), bx + 3, 12, 8);
               }
               else
               {
                  ofSetColor(255, 255, 255, 50);
                  ofSetLineWidth(1);
                  ofLine(bx, 0, bx, halfH);
               }
            }
            ofSetLineWidth(1);
         }
      }

      // zoom cue markers (linear)
      {
         float zoomStartBeat = SampleToBeat(zoomStart);
         float zoomEndBeat = SampleToBeat(zoomEnd);
         for (int i = 0; i < 8; ++i)
         {
            if (mCuePoints[i] >= 0)
            {
               float cueBeat = SampleToBeat(mCuePoints[i]);
               float frac = (cueBeat - zoomStartBeat) / (zoomEndBeat - zoomStartBeat);
               float cx = frac * sampleWidth;
               if (cx >= 0 && cx <= sampleWidth)
               {
                  if (mCueMode == CueMode::Delete)
                     ofSetColor(255, 60, 60, 180);
                  else if (mCueMode == CueMode::Edit)
                     ofSetColor(255, 200, 0, 180);
                  else if (i == mActiveHotCue)
                     ofSetColor(0, 255, 100, 180);
                  else
                     ofSetColor(0, 200, 255, 180);
                  ofLine(cx, 0, cx, halfH);
                  ofSetColor(255);
                  DrawTextNormal(ofToString(i + 1), cx + 3, 24, 9);
               }
            }
         }
      }

      // "ZOOM" label
      ofSetColor(255, 255, 255, 60);
      DrawTextNormal("ZOOM", sampleWidth - 30, 10, 7);

      ofPopStyle();
      ofPopMatrix();

      // sample name
      ofPushStyle();
      ofSetColor(255, 255, 255);
      DrawTextNormal(mSample->Name(), 5, 12);
      ofPopStyle();
   }
   else
   {
      ofPushStyle();
      ofFill();
      ofSetColor(255, 255, 255, 50);
      ofRect(0, 0, sampleWidth, totalHeight);
      ofSetColor(40, 40, 40);
      DrawTextNormal("drag & drop a sample here...", 10, 10, 8);
      ofPopStyle();
   }

   ofPopMatrix();
}

void DJPlayer::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

// ═══════════════════════════════════════════════════════════════════
//  Save / Load
// ═══════════════════════════════════════════════════════════════════

void DJPlayer::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);

   bool hasSample = (mSample != nullptr);
   out << hasSample;
   if (hasSample)
      mSample->SaveState(out);

   out << mPitchPercent;
   out << (int)mPitchRange;
   out << mMasterTempo;
   out << mSampleBPM;
   out << mVolume;
   for (int i = 0; i < 8; ++i)
      out << mCuePoints[i];
   out << mScrollZoomBeats;
   out << mLoopIn;
   out << mLoopOut;
   out << mLoopActive;
   out << mLoopBeats;
   out << mDetectedBPM;
   out << mLoop;
   out << mDetectedKey;
   out << mBPMConfidence;
   out << mFirstBeatSample;
}

void DJPlayer::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);

   bool hasSample;
   in >> hasSample;
   if (hasSample)
   {
      Sample* sample = new Sample();
      sample->LoadState(in);
      UpdateSample(sample, true);
   }

   if (rev >= 2)
   {
      int rangeInt;
      in >> mPitchPercent;
      in >> rangeInt;
      mPitchRange = (PitchRange)rangeInt;
      in >> mMasterTempo;
      in >> mSampleBPM;
      in >> mVolume;
      for (int i = 0; i < 8; ++i)
         in >> mCuePoints[i];
      if (rev >= 3 && rev < 11)
      {
         bool dummyBool;
         in >> dummyBool;  // old mShowBeatGrid
         in >> dummyBool;  // old mSnapToGrid
      }
      if (rev >= 4 && rev < 6)
      {
         int modeInt;
         in >> modeInt;  // read and discard old waveform mode
         int zoomBeatsInt;
         in >> zoomBeatsInt;
         mScrollZoomBeats = (float)zoomBeatsInt;
      }
      if (rev >= 6 && rev < 11)
      {
         in >> mScrollZoomBeats;
      }
      if (rev >= 5 && rev < 11)
      {
         float dummyFloat;
         in >> dummyFloat;  // old mBeatGridOffset
         in >> mLoopIn;
         in >> mLoopOut;
         in >> mLoopActive;
         in >> mLoopBeats;
         in >> mDetectedBPM;
      }
      if (rev >= 11)
      {
         in >> mScrollZoomBeats;
         in >> mLoopIn;
         in >> mLoopOut;
         in >> mLoopActive;
         in >> mLoopBeats;
         in >> mDetectedBPM;
      }
      if (rev >= 7)
      {
         in >> mLoop;
         if (mSample != nullptr)
            mSample->SetLooping(mLoop);
      }
      if (rev >= 8)
      {
         in >> mDetectedKey;
         mKeyName = GetKeyName();
      }
      if (rev >= 9)
      {
         in >> mBPMConfidence;
         in >> mFirstBeatSample;
      }
   }
   else if (rev == 1)
   {
      // v1 had mMasterBPM between mSyncEnabled and mVolume — skip it
      int rangeInt;
      float dummyFloat;
      bool dummyBool;
      in >> mPitchPercent;
      in >> rangeInt;
      mPitchRange = (PitchRange)rangeInt;
      in >> mMasterTempo;
      in >> mSampleBPM;
      in >> dummyBool;  // old mSyncEnabled
      in >> dummyFloat;  // skip old mMasterBPM
      in >> mVolume;
      for (int i = 0; i < 8; ++i)
         in >> mCuePoints[i];
   }
}

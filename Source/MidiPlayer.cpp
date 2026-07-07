#include "MidiPlayer.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "SynthGlobals.h"
#include "PatchCableSource.h"
#include "UIControlMacros.h"
#include <juce_audio_basics/juce_audio_basics.h>

MidiPlayer::MidiPlayer()
{
}

MidiPlayer::~MidiPlayer()
{
   TheTransport->RemoveAudioPoller(this);
   ClearMidiData();
}

void MidiPlayer::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   BUTTON_STYLE(mPlayButton, "play", ButtonDisplayStyle::kPlay);
   UIBLOCK_SHIFTRIGHT();
   BUTTON_STYLE(mPauseButton, "pause", ButtonDisplayStyle::kPause);
   UIBLOCK_SHIFTRIGHT();
   BUTTON_STYLE(mStopButton, "stop", ButtonDisplayStyle::kStop);
   UIBLOCK_SHIFTRIGHT();
   BUTTON_STYLE(mLoadButton, "load", ButtonDisplayStyle::kFolderIcon);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mLoopCheckbox, "loop", &mLoop);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mSyncCheckbox, "sync", &mSync);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mTempoSlider, "tempo", &mTempo, 20, 400);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mVolumeSlider, "vol", &mVolume, 0, 2);
   UIBLOCK_NEWLINE();
   DROPDOWN(mChannelFilter, "channel", &mChannelFilterIndex, 100);
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mTrackSelector, "track", &mTrackSelect, 100);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mNudgeLeft, "<nudge");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mNudgeRight, "nudge>");
   ENDUIBLOCK0();

   mChannelFilter->AddLabel("Omni", 0);
   for (int ch = 1; ch <= 16; ++ch)
   {
      char lbl[8];
      snprintf(lbl, sizeof(lbl), "Ch %d", ch);
      mChannelFilter->AddLabel(lbl, ch);
   }

   PatchCableSource* noteOut = new PatchCableSource(this, kConnectionType_Note);
   noteOut->SetOverrideCableDir(ofVec2f(0, 1), PatchCableSource::Side::kBottom);
   AddPatchCableSource(noteOut);

   for (int c = 0; c < kNumCues; ++c)
   {
      char label[8];
      snprintf(label, sizeof(label), "C%d", c + 1);
      mCueButtons[c] = new ClickButton(this, label, 5 + c * 36, 0);
   }
}

void MidiPlayer::Init()
{
   IDrawableModule::Init();
   TheTransport->AddAudioPoller(this);
}

void MidiPlayer::OnTransportAdvanced(float amount)
{
   if (!mEnabled || !mIsPlaying || mEvents.empty())
      return;

   double prevPos = mPlayPosition;
   double prevPosNudged = mPlayPosition + mNudge;

   if (mSync)
   {
      double masterSec = TheTransport->GetMeasureTime(gTime) * TheTransport->MsPerBar() / 1000.0;
      mPlayPosition = fmod(masterSec, mLength);
      if (mPlayPosition < 0)
         mPlayPosition += mLength;

      if (mPlayPosition < prevPos || std::abs(mPlayPosition - prevPos) > amount * TheTransport->MsPerBar() / 1000.0 * 1.5)
      {
         mNoteOutput.Flush(gTime);
         mNextEventIndex = 0;
         while (mNextEventIndex < (int)mEvents.size() &&
                mEvents[mNextEventIndex].mTime < mPlayPosition + mNudge)
         {
            ++mNextEventIndex;
         }
         return;
      }
   }
   else
   {
      double dtSec = amount * TheTransport->MsPerBar() / 1000.0;
      dtSec *= mTempo / 120.0f;
      if (dtSec <= 0)
         return;
      mPlayPosition += dtSec;

      if (mPlayPosition >= mLength)
      {
         if (mLoop)
         {
            mNoteOutput.Flush(gTime);
            mPlayPosition = 0;
            mNextEventIndex = 0;
         }
         else
         {
            mPlayPosition = mLength;
            mIsPlaying = false;
            mNoteOutput.Flush(gTime);
            return;
         }
      }
   }

   double currentPosNudged = mPlayPosition + mNudge;

   while (mNextEventIndex < (int)mEvents.size() &&
          mEvents[mNextEventIndex].mTime < currentPosNudged &&
          mEvents[mNextEventIndex].mTime >= prevPosNudged)
   {
      const MidiEvent& ev = mEvents[mNextEventIndex];

      if (mChannelFilterIndex != 0 && mChannelFilterIndex != ev.mChannel + 1)
      {
         ++mNextEventIndex;
         continue;
      }

       int voiceIdx = ev.mChannel; // preserve original MIDI channel (0-based)

      if (ev.mMessage.isNoteOn())
      {
         int pitch = ev.mMessage.getNoteNumber();
         int vel = (int)(ev.mMessage.getVelocity() * mVolume + 0.5f);
         PlayNoteOutput(gTime, pitch, ofClamp(vel, 1, 127), voiceIdx);
      }
      else if (ev.mMessage.isNoteOff())
      {
         int pitch = ev.mMessage.getNoteNumber();
         PlayNoteOutput(gTime, pitch, 0, voiceIdx);
      }
      else if (ev.mMessage.isController())
      {
         SendCCOutput(ev.mMessage.getControllerNumber(), ev.mMessage.getControllerValue());
      }
      ++mNextEventIndex;
   }
}

void MidiPlayer::ButtonClicked(ClickButton* button, double time)
{
   if (button == mLoadButton)
      LoadFile();
   else if (button == mPlayButton)
      Play();
   else if (button == mPauseButton)
      Pause();
   else if (button == mStopButton)
      Stop();
   else if (button == mNudgeLeft)
   {
      mNudge -= 0.02f;
      mNudge = std::max(-0.5f, mNudge);
      mNoteOutput.Flush(gTime);
      mNextEventIndex = 0;
      while (mNextEventIndex < (int)mEvents.size() &&
             mEvents[mNextEventIndex].mTime < mPlayPosition + mNudge)
      {
         ++mNextEventIndex;
      }
   }
   else if (button == mNudgeRight)
   {
      mNudge += 0.02f;
      mNudge = std::min(0.5f, mNudge);
      mNoteOutput.Flush(gTime);
      mNextEventIndex = 0;
      while (mNextEventIndex < (int)mEvents.size() &&
             mEvents[mNextEventIndex].mTime < mPlayPosition + mNudge)
      {
         ++mNextEventIndex;
      }
   }
   else
   {
      for (int c = 0; c < kNumCues; ++c)
      {
         if (button == mCueButtons[c])
         {
            if (mCueSet[c])
               Seek(mCuePositions[c]);
            else
            {
               mCuePositions[c] = mPlayPosition;
               mCueSet[c] = true;
            }
            return;
         }
      }
   }
}

void MidiPlayer::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
}

void MidiPlayer::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mChannelFilter)
      mChannelFilterIndex = mChannelFilterIndex;
}

void MidiPlayer::LoadFile()
{
   using namespace juce;
   String filePattern = "*.mid;*.midi";
   if (File::areFileNamesCaseSensitive())
      filePattern += ";" + filePattern.toUpperCase();

   FileChooser chooser("Load MIDI file",
                       File(ofToDataPath("")),
                       filePattern, true, false,
                       TheSynth->GetFileChooserParent());

   if (chooser.browseForFileToOpen())
   {
      File file = chooser.getResult();
      FileInputStream inputStream(file);
      if (inputStream.openedOk())
      {
         juce::MidiFile newFile;
         if (newFile.readFrom(inputStream))
         {
            newFile.convertTimestampTicksToSeconds();
            ClearMidiData();

            mFileName = file.getFileName().toStdString();

            mNumTracks = newFile.getNumTracks();
            for (int t = 0; t < mNumTracks; ++t)
            {
               const MidiMessageSequence* track = newFile.getTrack(t);
               if (!track) continue;
               for (int e = 0; e < track->getNumEvents(); ++e)
               {
                  MidiMessageSequence::MidiEventHolder* ev = track->getEventPointer(e);
                  if (!ev) continue;
                  MidiEvent me;
                  me.mTime = ev->message.getTimeStamp();
                  me.mChannel = ev->message.getChannel() - 1;
                  me.mTrack = t;
                  me.mMessage = ev->message;
                  mEvents.push_back(me);
               }
            }

            mTrackSelector->Clear();
            mTrackSelector->AddLabel("All", -1);
            for (int t = 0; t < mNumTracks; ++t)
            {
               char lbl[16];
               snprintf(lbl, sizeof(lbl), "Track %d", t + 1);
               mTrackSelector->AddLabel(lbl, t);
            }
            mTrackSelect = -1;

            std::sort(mEvents.begin(), mEvents.end(),
                      [](const MidiEvent& a, const MidiEvent& b)
                      { return a.mTime < b.mTime; });

            mLength = mEvents.empty() ? 0 : mEvents.back().mTime + 1.0;
            mNextEventIndex = 0;
            mPlayPosition = 0;

            // Precompute simple density waveform
            mWaveform.assign(kWaveSlices, 0);
            if (mLength > 0)
            {
               for (const auto& ev : mEvents)
               {
                  if (ev.mMessage.isNoteOn())
                  {
                     int slice = (int)((ev.mTime + mNudge) / mLength * kWaveSlices);
                     slice = ofClamp(slice, 0, kWaveSlices - 1);
                     mWaveform[slice] = std::max(mWaveform[slice], ev.mMessage.getVelocity() / 127.0f);
                  }
               }
               // Light smooth to fill gaps
               for (int i = 1; i < kWaveSlices; ++i)
                  mWaveform[i] = std::max(mWaveform[i], mWaveform[i - 1] * 0.15f);
               for (int i = kWaveSlices - 2; i >= 0; --i)
                  mWaveform[i] = std::max(mWaveform[i], mWaveform[i + 1] * 0.15f);
            }

            for (int c = 0; c < kNumCues; ++c)
               mCueSet[c] = false;
         }
      }
   }
}

void MidiPlayer::ClearMidiData()
{
   mNoteOutput.Flush(gTime);
   mEvents.clear();
   mWaveform.clear();
   mNextEventIndex = 0;
   mPlayPosition = 0;
   mLength = 0;
   mIsPlaying = false;
   mFileName.clear();
}

void MidiPlayer::Play()
{
   if (mEvents.empty()) return;
   if (mPlayPosition >= mLength)
   {
      mPlayPosition = 0;
      mNextEventIndex = 0;
   }
   mIsPlaying = true;
}

void MidiPlayer::Pause()
{
   mIsPlaying = false;
}

void MidiPlayer::Stop()
{
   mIsPlaying = false;
   mNoteOutput.Flush(gTime);
   mPlayPosition = 0;
   mNextEventIndex = 0;
}

void MidiPlayer::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (checkbox == mSyncCheckbox && mSync)
   {
      mNoteOutput.Flush(gTime);
      double masterSec = TheTransport->GetMeasureTime(gTime) * TheTransport->MsPerBar() / 1000.0;
      mPlayPosition = fmod(masterSec, mLength);
      if (mPlayPosition < 0)
         mPlayPosition += mLength;
      mNextEventIndex = 0;
      while (mNextEventIndex < (int)mEvents.size() &&
             mEvents[mNextEventIndex].mTime < mPlayPosition + mNudge)
      {
         ++mNextEventIndex;
      }
   }
}

void MidiPlayer::Seek(double time)
{
   time = std::max(0.0, std::min(time, mLength));
   mNoteOutput.Flush(gTime);

   if (mSync)
   {
      // Seek the master transport so all synced modules follow
      double measureTime = time * 1000.0 / TheTransport->MsPerBar();
      TheTransport->SetMeasureTime(measureTime);
      // mPlayPosition will update on next OnTransportAdvanced
   }
   else
   {
      mPlayPosition = time;
      mNextEventIndex = 0;
      while (mNextEventIndex < (int)mEvents.size() &&
             mEvents[mNextEventIndex].mTime < mPlayPosition + mNudge)
      {
         ++mNextEventIndex;
      }
   }
}

int MidiPlayer::CueSlotFromClick(float x, float y) const
{
   float cueY = mHeight - kBottomAreaHeight + 8;
   if (y < cueY || y > cueY + 22) return -1;
   if (x < 5 || x > 5 + kNumCues * 36) return -1;
   int slot = (int)((x - 5) / 36);
   return (slot >= 0 && slot < kNumCues) ? slot : -1;
}

void MidiPlayer::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);
   if (right)
   {
      int slot = CueSlotFromClick(x, y);
      if (slot >= 0)
         mCueSet[slot] = false;
      return;
   }

   float waveY = kControlsTop + kControlsHeight + 4;
   float waveH = mHeight - waveY - kBottomAreaHeight - 4;
   if (mLength > 0 && y >= waveY && y <= waveY + waveH)
   {
      float relX = x - 8;
      float waveW = mWidth - 16;
      if (relX >= 0 && relX <= waveW)
      {
         double seekTime = (relX / waveW) * mLength;
         Seek(seekTime);
      }
   }
}

void MidiPlayer::Resize(float w, float h)
{
   mWidth = std::max(440.0f, w);
   mHeight = std::max(240.0f, h);
}

void MidiPlayer::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mPlayButton->Draw();
   mPauseButton->Draw();
   mStopButton->Draw();
   mLoadButton->Draw();
   mLoopCheckbox->Draw();
   mSyncCheckbox->Draw();
   mTempoSlider->Draw();
   mVolumeSlider->Draw();
   mChannelFilter->Draw();
   mTrackSelector->Draw();
   mNudgeLeft->Draw();
   mNudgeRight->Draw();

   float cueY = mHeight - kBottomAreaHeight + 8;
   for (int c = 0; c < kNumCues; ++c)
   {
      mCueButtons[c]->SetPosition(5 + c * 36, cueY);
      mCueButtons[c]->Draw();
   }

   // Lightweight waveform area
   float waveX = 8;
   float waveY = kControlsTop + kControlsHeight + 4;
   float waveW = mWidth - 16;
   float waveH = mHeight - waveY - kBottomAreaHeight - 4;
   if (waveH < 20) waveH = 20;

   ofSetColor(18, 18, 28);
   ofFill();
   ofRect(waveX, waveY, waveW, waveH);

   // Simple time grid
   ofSetColor(35, 38, 50);
   ofSetLineWidth(1);
   for (int i = 1; i < 16; ++i)
   {
      float gx = waveX + (float)i / 16.0f * waveW;
      ofLine(gx, waveY, gx, waveY + waveH);
   }

   // Piano roll for selected track, density waveform otherwise
   if (mTrackSelect >= 0 && mLength > 0)
   {
      // Find pitch range for selected track
      int minPitch = 128, maxPitch = 0;
      for (const auto& ev : mEvents)
      {
         if (ev.mTrack != mTrackSelect) continue;
         if (ev.mMessage.isNoteOn() || ev.mMessage.isNoteOff())
         {
            int p = ev.mMessage.getNoteNumber();
            if (p < minPitch) minPitch = p;
            if (p > maxPitch) maxPitch = p;
         }
      }
      if (maxPitch < minPitch) { minPitch = 0; maxPitch = 127; }
      int pitchRange = std::max(1, maxPitch - minPitch);
      float pitchH = waveH / (float)pitchRange;

      // Horizontal pitch grid lines (octaves)
      ofSetColor(35, 40, 55);
      ofSetLineWidth(1);
      int firstOctave = minPitch / 12;
      int lastOctave = maxPitch / 12;
      for (int o = firstOctave; o <= lastOctave; ++o)
      {
         int octPitch = o * 12;
         if (octPitch < minPitch || octPitch > maxPitch) continue;
         float oy = waveY + waveH - (octPitch - minPitch) * pitchH;
         ofLine(waveX, oy, waveX + waveW, oy);
      }

      // Draw note bars
      for (const auto& ev : mEvents)
      {
         if (!ev.mMessage.isNoteOn()) continue;
         if (ev.mTrack != mTrackSelect) continue;

         float nx = waveX + (float)((ev.mTime + mNudge) / mLength) * waveW;
         int pitch = ev.mMessage.getNoteNumber();
         float ny = waveY + waveH - (pitch - minPitch) * pitchH;
         float vel = ev.mMessage.getVelocity() / 127.0f;

         ofSetColor(0, 200, 255, (int)(120 + 100 * vel));
         ofFill();
         ofRect(nx, ny - pitchH * 0.4f, waveW / mLength * 0.08f, pitchH * 0.8f);
      }
   }
   else if (!mWaveform.empty() && mLength > 0)
   {
      // Lightweight density waveform for "All" view
      ofSetColor(0, 160, 240, 120);
      ofFill();
      ofBeginShape();
      ofVertex(waveX, waveY + waveH);
      for (int i = 0; i < kWaveSlices; ++i)
      {
         float x = waveX + (float)i / kWaveSlices * waveW;
         float a = std::min(mWaveform[i] * 1.5f, 1.0f);
         ofVertex(x, waveY + waveH * (1.0f - a));
      }
      ofVertex(waveX + waveW, waveY + waveH);
      ofEndShape();

      ofSetColor(0, 200, 255, 220);
      ofSetLineWidth(1.5f);
      ofBeginShape();
      for (int i = 0; i < kWaveSlices; ++i)
      {
         float x = waveX + (float)i / kWaveSlices * waveW;
         float a = std::min(mWaveform[i] * 1.5f, 1.0f);
         ofVertex(x, waveY + waveH * (1.0f - a));
      }
      ofEndShape();

      ofSetColor(0, 220, 255, 160);
      ofFill();
      for (int i = 0; i < kWaveSlices; ++i)
      {
         if (mWaveform[i] > 0.05f)
         {
            float x = waveX + (float)i / kWaveSlices * waveW;
            float py = waveY + waveH * (1.0f - std::min(mWaveform[i] * 1.5f, 1.0f));
            ofCircle(x, py, 2);
         }
      }
   }

   // Cue markers
   for (int c = 0; c < kNumCues; ++c)
   {
      if (mCueSet[c])
      {
         float cx = waveX + (float)(mCuePositions[c] / mLength) * waveW;
         ofSetColor(255, 200, 0, 200);
         ofSetLineWidth(2);
         ofLine(cx, waveY, cx, waveY + waveH);
      }
   }

   // Playhead
   if (mLength > 0)
   {
      float px = waveX + (float)(mPlayPosition / mLength) * waveW;
      ofSetColor(255, 80, 80);
      ofSetLineWidth(2);
      ofLine(px, waveY, px, waveY + waveH);
      ofFill();
      ofTriangle(px - 4, waveY, px + 4, waveY, px, waveY + 8);
   }

   // Info bar
   ofSetColor(160, 160, 180);
   char buf[256];
   if (!mFileName.empty())
   {
      const char* state = mIsPlaying ? (mSync ? ">>" : ">") : mPlayPosition > 0 ? "||" : "";
      snprintf(buf, sizeof(buf), "%s  %s  %d events  %s%.1fs/%.1fs  nudge:%+.0fms",
               mFileName.c_str(), state, (int)mEvents.size(),
               mSync ? "sync " : "", mPlayPosition, mLength, mNudge * 1000);
      if (mTrackSelect >= 0)
      {
         char trackLabel[32];
         snprintf(trackLabel, sizeof(trackLabel), "  track %d/%d", mTrackSelect + 1, mNumTracks);
         strncat(buf, trackLabel, sizeof(buf) - strlen(buf) - 1);
      }
   }
   else
   {
      snprintf(buf, sizeof(buf), "no file loaded");
   }
   gFont.DrawString(buf, 10, 8, mHeight - 8);

   if (mSync)
   {
      ofSetColor(80, 80, 90, 100);
      ofFill();
      ofRect(mTempoSlider->GetPosition(true).x, mTempoSlider->GetPosition(true).y,
             mTempoSlider->IClickable::GetRect().width, mTempoSlider->IClickable::GetRect().height);
   }

   for (int c = 0; c < kNumCues; ++c)
   {
      if (mCueSet[c])
      {
         char lbl[32];
         snprintf(lbl, sizeof(lbl), "%.1fs", mCuePositions[c]);
         gFont.DrawString(lbl, 7, 5 + c * 36 + 4, cueY - 10);
      }
   }
}

void MidiPlayer::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mTempo;
   out << mLoop;
   out << mVolume;
   out << mChannelFilterIndex;
   out << (int)kNumCues;
   for (int c = 0; c < kNumCues; ++c)
   {
      out << mCueSet[c];
      out << mCuePositions[c];
   }
   out << mSync;
   out << mNudge;
   out << mWidth;
   out << mHeight;
}

void MidiPlayer::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev < 1) return;
   in >> mTempo;
   in >> mLoop;
   if (rev >= 2)
   {
      in >> mVolume;
      int numCues;
      in >> numCues;
      for (int c = 0; c < numCues && c < kNumCues; ++c)
      {
         in >> mCueSet[c];
         in >> mCuePositions[c];
      }
   }
   if (rev >= 3)
      in >> mChannelFilterIndex;
   if (rev >= 4)
      in >> mSync;
   if (rev >= 5)
   {
      in >> mNudge;
      in >> mWidth;
      in >> mHeight;
   }
}

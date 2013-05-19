

#include "looper.hxx"

#include "jack.hxx"
#include "eventhandler.hxx"

extern Jack* jack;

Looper::Looper(int t) :
      track(t),
      state(STATE_STOPPED),
      fpb(120),
      gain(1.f),
      numBeats   (4),
      playedBeats(0),
      stopRecordOnBar(false),
      endPoint   (0),
      playPoint  (0),
      lastWrittenSampleIndex(0)
{
  // init faust pitch shift variables
  fSamplingFreq = 44100;
  IOTA = 0;
  
  int bufferSize = 1024;
  
  for ( int i = 0; i < bufferSize; i++)
    tmpBuffer.push_back(0.f);
  
  for (int i=0; i<65536; i++)
    fVec0[i] = 0;
  
  semitoneShift = 0.0f;
  windowSize = 1000;
  crossfadeSize = 1000;
  
  for (int i=0; i<2; i++)
    fRec0[i] = 0;
}

void Looper::midi(unsigned char* data)
{
  if ( data[0] - 144 == track )
  {
    switch ( data[1] )
    {
      case 48: setState( STATE_RECORD_QUEUED );     break;
      case 53: setState( STATE_PLAY_QUEUED );       break;
      case 52: setState( STATE_STOPPED );           break;
    }
  }
  else if ( data[0] - 128 == track )
  {
    switch ( data[1] )
    {
      case 48: setState( STATE_STOP_QUEUED );
    }
  }
  else if ( data[0] - 176 == track )
  {
    switch ( data[1] )
    {
      case 7: gain = int(data[2])/127.f; break;
    }
  }
  
  
}

void Looper::setState(State s)
{
  if ( state == STATE_RECORDING )
  {
    stopRecordOnBar = true;
  }
  
  state = s;
  
  if (state == STATE_RECORD_QUEUED )
  {
    numBeats = 0;
    jack->getControllerUpdator()->record(track, true);
  }
  else
  {
    jack->getControllerUpdator()->record(track, false);
  }
  
  if (state == STATE_PLAY_QUEUED )
  {
}

void Looper::process(int nframes, Buffers* buffers)
{
  float* in  = buffers->audio[Buffers::MASTER_INPUT];
  float* out = buffers->audio[Buffers::MASTER_OUTPUT];
  
  float playbackSpeed = endPoint / ( float(numBeats) * fpb );
  // invert the change due to speed, and pitch-shift it back to normal :D
  float deltaPitch = 12 * log ( playbackSpeed ) / log (2);
  semitoneShift = -deltaPitch;
  
  if (track == 0)
  {
    /*
    // log pitch-shift rates
    char buffer [50];
    sprintf (buffer, "Looper, pbs=%f, dP=%f", playbackSpeed, deltaPitch );
    EventGuiPrint e( buffer );
    writeToGuiRingbuffer( &e );
    */
  }
  
  if ( state == STATE_PLAYING )
  {
    for(int i = 0; i < nframes; i++)
    {
      if ( playPoint < endPoint )
      {
        tmpBuffer[i] = sample[playPoint] * gain;
      }
      // always update playPoint, even when not playing sound.
      // it updates the UI of progress
      playPoint++;
    }
    
    // now pitch-shift the audio in the buffer
    pitchShift( nframes, &tmpBuffer[0], out);
    
    float prog = (float(playPoint) / (fpb*numBeats));
    
    /*
    if ( track == 0 )
      cout << prog << "  playPoint " << playPoint << "  endPoint " << endPoint
           << "  fpb*numBeats " << fpb * numBeats <<  endl;
    */
    EventLooperProgress e(track, prog );
    writeToGuiRingbuffer( &e );
  }
  // stopRecordOnBar ensures we record right up to the bar measure
  else if ( state == STATE_RECORDING || stopRecordOnBar )
  {
    for(int i = 0; i < nframes; i++)
    {
      if ( lastWrittenSampleIndex < 44100 * 60 )
      {
        sample[lastWrittenSampleIndex++] = in[i];
      }
    }
  }
  
}


void Looper::bar()
{
  // queue stop recording -> stop recording, now calculate beats in loop
  if ( stopRecordOnBar )
  {
    stopRecordOnBar = false;
  }
  
  if ( playedBeats >= numBeats )
  {
    playPoint = 0;
    playedBeats = 0;
  }
  
  if ( state == STATE_PLAY_QUEUED )
  {
    EventGuiPrint e( "Looper Q->Playing" );
    writeToGuiRingbuffer( &e );
    state = STATE_PLAYING;
    playPoint = 0;
    endPoint = lastWrittenSampleIndex;
  }
  if ( state == STATE_RECORD_QUEUED )
  {
    EventGuiPrint e( "Looper Q->Recording" );
    writeToGuiRingbuffer( &e );
    
    state = STATE_RECORDING;
    playPoint = 0;
    endPoint = 0;
    lastWrittenSampleIndex = 0;
  }
  if ( state == STATE_PLAY_QUEUED )
  {
    EventGuiPrint e( "Looper Q->Stopped" );
    writeToGuiRingbuffer( &e );
    
    state = STATE_STOPPED;
    endPoint = lastWrittenSampleIndex;
  }
}

void Looper::beat()
{
  if (state == STATE_RECORDING || stopRecordOnBar )
  {
    numBeats++;
  }
  playedBeats++;
}

void Looper::setLoopLength(float l)
{
  numBeats *= l;
  
  // smallest loop = 4 beats
  if ( numBeats < 4 )
    numBeats = 4;
  
  char buffer [50];
  sprintf (buffer, "Looper loop lenght = %i", numBeats );
  EventGuiPrint e( buffer );
  writeToGuiRingbuffer( &e );
}


void Looper::pitchShift(int count, float* input, float* output)
{
  float   fSlow0 = windowSize;
  float   fSlow1 = ((1 + fSlow0) - powf(2,(0.08333333333333333f * semitoneShift)));
  float   fSlow2 = (1.0f / crossfadeSize);
  float   fSlow3 = (fSlow0 - 1);
  float* input0 = &input[0];
  float* output0 = &output[0];
  
  for (int i=0; i<count; i++)
  {
    float fTemp0 = (float)input0[i];
    fVec0[IOTA&65535] = fTemp0;
    fRec0[0] = fmodf((fRec0[1] + fSlow1),fSlow0);
    int iTemp1 = int(fRec0[0]);
    int iTemp2 = (1 + iTemp1);
    float fTemp3 = min((fSlow2 * fRec0[0]), 1.f );
    float fTemp4 = (fSlow0 + fRec0[0]);
    int iTemp5 = int(fTemp4);
    
    output0[i] += (float)(((1 - fTemp3) * (((fTemp4 - iTemp5) * 
    fVec0[(IOTA-int((int((1 + iTemp5)) & 65535)))&65535]) + ((0 - ((
    fRec0[0] + fSlow3) - iTemp5)) * fVec0[(IOTA-int((iTemp5 & 65535)))
    &65535]))) + (fTemp3 * (((fRec0[0] - iTemp1) * fVec0[(IOTA-int((int(
    iTemp2) & 65535)))&65535]) + ((iTemp2 - fRec0[0]) * fVec0[(IOTA-int((
    iTemp1 & 65535)))&65535]))));
    
    fRec0[1] = fRec0[0];
    IOTA = IOTA+1;
  }
}

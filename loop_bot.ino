/*
  NAME: loop_bot
 
  DESCRIPTION:
    Record a pattern of button presses and play it back.
*/

// TODO: Rename "LED" variables to something more generic.


//----- Constants ------------------------------------------

// To change the number of channnels, update the `NUM_CHANNELS` variable below
// and add/remove pin numbers from the `LED_PINS` and `BUTTON_PINS` arrays.
const int NUM_CHANNELS  = 2;

const int LED_PINS[NUM_CHANNELS]    = { 12, 13 };  // LEDs, solenoids, etc.
const int BUTTON_PINS[NUM_CHANNELS] = {  2,  3 };

const unsigned int STRIKE_DURATION = 200;  // output signal time (in ms)
const unsigned int LOOP_DURATION   = 7000; // recording and playback time (in ms)

// The maximum number of events on a single channel for a given recording window.
// Since arrays are static we need to pre-allocate space to store the timestamps.
// The only reason to keep this number low is to save space, if the length of the
// recording window is increased this may need to be adjusted as well.
const unsigned int MAX_LENGTH = 15;


//----- Global Variables -----------------------------------

// For each channel we store a series of timestamps corresponding to button press
// events in the `Events` array. The total number of events in a particular window
// are stored in the `TotalEvents` array. For example, a two-channel configuration
// could have the following data:
//
//   Events[0] = { 500, 1000, 2000, 2500, 3000 }
//   Events[1] = { 500, 2500, 5000 }
//
//   TotalEvents[0] = 5
//   TotalEvents[1] = 3
//
// The maximmum number of events is `MAX_LENGTH`. If the actual number of events
// on a channel exceeds this limit the program may not behave as expected.
unsigned long Events[NUM_CHANNELS][MAX_LENGTH];
int TotalEvents[NUM_CHANNELS];

// During playback, the program loops continuously over the `Events` data. The 
// index for each channel is stored in the `POSITION` array.
int Position[NUM_CHANNELS];

// The `NextEvent` array stores the timestamp for the next upcoming action for
// a given channel.
unsigned long NextEvent[NUM_CHANNELS];

unsigned long TimeDiff[NUM_CHANNELS];

int LedState[NUM_CHANNELS];         // current state of the 
int PrevButtonState[NUM_CHANNELS];  // used to check for changes in button state

unsigned long RecordTimeout;  // the end of the current recording window (timestamp)

bool Recording = false;
bool Playback  = false;


//----- Functions -----------------------------------------

void setup() {
  Serial.begin(9600);
  Serial.println("loop_bot started...");

  // For each channel initialize the input and output pins and set the state.
  for (int i=0; i<NUM_CHANNELS; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    pinMode(BUTTON_PINS[i], INPUT);

    LedState[i]        = LOW;
    PrevButtonState[i] = LOW;
  }
}


void startRecording(unsigned long ts) {
  Serial.println("recording start...");

  // Stop playback while recording.
  Recording = true;
  Playback  = false;

  // Initialize/reset each channel. To synchronize the recording the start time
  // is set as the first event for each channel. When the recording is finished,
  // the stop time will be set as the final event for each channel (see below).
  for (int i=0; i<NUM_CHANNELS; i++) {
    Events[i][0] = ts;
    Position[i]  = 1;
  }
  
  // Set the stop time for the recording window.
  RecordTimeout = ts + LOOP_DURATION;
}


void stopRecording(unsigned long ts) {
  Serial.println("recording stop...");

  // Stop recording and resume playback.
  Recording = false;
  Playback  = true;

  // Set the stop time as the final event for each channel and initialize the
  // the playback variables.
  for (int i=0; i<NUM_CHANNELS; i++) {
    // Add the stop time at the end of the array.
    int pos = Position[i];
    Events[i][pos] = ts;
    
    // Set the total event count for the channel for this recording window.
    TotalEvents[i] = pos + 1;

    // Compute the time of the first (next) event for the channel. The first
    // event time is the elapsed time from the start of recording to the first
    // event on the channel. If the channel button initiated the recording these
    // times will be the same.
    //
    // An arbitrary 1 second padding is added to this first event time.
    NextEvent[i] = ts + 1000 + (Events[i][1] - Events[i][0]);
    
    // Initialize the channel position for playback. Since the first event has
    // already been calculated the position should be set to 2;
    //
    // TODO: If there were no events on a particular channel, this position will
    //       be invalid. It might be better to set the position to 1 here and 
    //       increment the position in `getNextDelay` prior to computing the time
    //       difference.
    Position[i] = 2;
  }
}


void addEvent(int button, unsigned long ts) {
  /// Add the event time to the end of the channel array.
  int pos = Position[button];
  Events[button][pos] = ts;
  
  // Increment the index position for the channel.
  Position[button] = pos + 1;
}


unsigned long getNextDelay(int button) {
  unsigned long nextDelay;
  
  // Get the current index position for the channel.
  int index = Position[button];

  // Computed the elaspsed time since the previous event.
  nextDelay = Events[button][index] - Events[button][index - 1];

  // The final value for all channels is the same and corresponds to the end of
  // the recording window. If we are at the last event for this channel, we need
  // to add the time to the first event. Note that if the channel button
  // initiated playback the first two values will be the same and the `nextDelay`
  // value will be unchanged.
  index++;
  if (index == TotalEvents[button]) {
    nextDelay += (Events[button][1] - Events[button][0]);
    
    // Since the first event time has just been added the next index position is 2.
    index = 2;
  }
  
  // Update the channel index position.
  Position[button] = index;

  return nextDelay;
}


void loop() {
  // NOTE: Any calculations involving timestamps must use `unsigned long` types.
  unsigned long currentTime = millis();

  // Check to see if we need to stop recording. 
  if (Recording && currentTime > RecordTimeout) {
    stopRecording(currentTime);
  }

  // Check for any button press events.
  for (int i=0; i<NUM_CHANNELS; i++) {
    // Read the button state for the channel.
    int buttonPin = BUTTON_PINS[i];
    int buttonState = digitalRead(buttonPin);

    // If the button state has changed check to see if it pressed or released.
    // If it was pressed then a recording session should be started and/or an
    // event added to a current recording session.
    if (buttonState != PrevButtonState[i]) {
      if (buttonState == HIGH) {
        // Start a recording session if one is not already in progress.
        if (!Recording) {
          startRecording(currentTime);
        }

        // Add the current timestamp to the channel's events.
        if (Recording) {
          addEvent(i, currentTime);
        }
      }

      // Save the button state for this channel.
      PrevButtonState[i] = buttonState;
      delay(50);  // add a slight delay for debouncing
    }
  }

  if (Playback) {
    for (int i=0; i<NUM_CHANNELS; i++) {
      if (currentTime > NextEvent[i]) {
        // The difference between the `currentTime` and the `NextEvent` time for
        // the channel will continue to accumulte and will result in a loss of 
        // synchronization. To compensate for this we subtract the difference from
        // the inter-event delay (see below).
        unsigned long t_diff = currentTime - NextEvent[i];

        if (LedState[i] == LOW) {
          // Turn on the channel output pin for the `STRIKE_DURATION`.
          LedState[i]  = HIGH;
          NextEvent[i] = currentTime + STRIKE_DURATION;

          // Because the `STRIKE_DURATION` will potentially be very short (and needs
          // to be consistent) we store the current time difference and subtract it
          // from the inter-event delay between strikes.
          TimeDiff[i] = t_diff;
        } else {
          // Turn off the channel output and delay until the next event for this 
          // channel. Adjust the delay time to account for the accumulated time
          // difference.
          LedState[i]  = LOW;
          unsigned long t_adjust = TimeDiff[i] + t_diff;
          NextEvent[i] = currentTime + getNextDelay(i) - STRIKE_DURATION - t_adjust;
        }

        // The output pin is actually set here.
        int ledPin = LED_PINS[i];
        digitalWrite(ledPin, LedState[i]);
      }
    }
  }
}

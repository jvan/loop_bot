/*
  NAME: loop_bot
 
  DESCRIPTION:
    Record a pattern of button presses and play it back.
*/

// TODO: Rename "LED" variables to something more generic.


//----- Constants ------------------------------------------

// To change the number of channnels, update the `NUM_CHANNELS` variable below
// and add/remove pin numbers from the `LED_PINS` and `BUTTON_PINS` arrays.
const int NUM_CHANNELS  = 3;

const int LED_PINS[NUM_CHANNELS]    = { 11, 12, 13 };  // LEDs, solenoids, etc.
const int BUTTON_PINS[NUM_CHANNELS] = {  2,  3,  4 };

const int INDICATOR_PIN = 7;

const unsigned int STRIKE_DURATION = 200;  // output signal time (in ms)

// Recording will stop once `RECORD_TIMEOUT` milliseconds have elapsed since
// the last event.
const unsigned int RECORD_TIMEOUT  = 2000;

// The maximum number of events on a single channel for a given recording window.
// Since arrays are static we need to pre-allocate space to store the timestamps.
// The only reason to keep this number low is to save space, if the length of the
// recording window is increased this may need to be adjusted as well.
const unsigned int MAX_LENGTH = 15;

// Playback will stop after the pattern has been played `MAX_LOOPS` times.
const unsigned int MAX_LOOPS = 3;

// Add a distortion phase after `MAX_LOOPS` has been exceeded.
const bool DISTORTION = true;

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

bool IsActive[NUM_CHANNELS];

// During playback, the program loops continuously over the `Events` data. The 
// index for each channel is stored in the `POSITION` array.
int Position[NUM_CHANNELS];

// The `NextEvent` array stores the timestamp for the next upcoming action for
// a given channel.
unsigned long NextEvent[NUM_CHANNELS];

unsigned long TimeDiff[NUM_CHANNELS];

int LedState[NUM_CHANNELS];         // current state of the 
int PrevButtonState[NUM_CHANNELS];  // used to check for changes in button state

unsigned long RecordStart;    // timestamp for the start of the recording window
unsigned long LoopDuration;   // total recording/playback time
unsigned long LastEventTime;  // used to stop recording session
unsigned long PlaybackStart;  // used to stop playback

unsigned int LoopCount;

bool Recording = false;
bool Playback  = false;


//----- Functions -----------------------------------------

void channelMsg(unsigned int channel, char* msg, bool flush=false) {
  Serial.print("[channel ");
  Serial.print(channel);
  Serial.print("]: ");
  Serial.print(msg);
  Serial.print("\n");
  if (flush) {
    Serial.flush();
  }
}

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

  // Initialize the recordin indicator LED pin.
  pinMode(INDICATOR_PIN, OUTPUT);
}


void startRecording(unsigned long ts) {
  Serial.println("recording start...");

  // Stop playback while recording.
  Recording = true;
  Playback  = false;

  // Turn on the recording indicator LED.
  digitalWrite(INDICATOR_PIN, HIGH);

  // Initialize/reset each channel. To synchronize the recording the start time
  // is set as the first event for each channel. When the recording is finished,
  // the stop time will be set as the final event for each channel (see below).
  for (int i=0; i<NUM_CHANNELS; i++) {
    Events[i][0] = ts;
    Position[i]  = 1;

    // Activate all channels.
    IsActive[i] = true;

    // Make sure all output pins are off.
    LedState[i] = LOW;

    int ledPin = LED_PINS[i];
    digitalWrite(ledPin, LedState[i]);
  }
  
  // Store the start time so we can compute the loop duration.
  RecordStart = ts;
}


void stopRecording(unsigned long ts) {
  Serial.println("recording stop...");

  // Stop recording and resume playback.
  Recording = false;
  Playback  = true;

  // Turn off the recording indicator LED.
  digitalWrite(INDICATOR_PIN, LOW);

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
    Position[i] = 2;

    // Disable channels without any events.
    if (TotalEvents[i] == 2) {
      IsActive[i] = false;
    }
  }

  // Calculate the loop duration and the playback start.
  LoopDuration = ts - RecordStart;
  PlaybackStart = ts + 1000;  // add the same padding as above

  LoopCount = 0;
}


void addEvent(int button, unsigned long ts) {
  /// Add the event time to the end of the channel array.
  int pos = Position[button];
  Events[button][pos] = ts;

  // Increment the index position for the channel.
  Position[button] = pos + 1;

  // Store the last event time so we can automatically stop the recording.
  LastEventTime = ts;
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

unsigned int activeChannelCount() {
  unsigned int activeChannels = 0;
  for (int i=0; i<NUM_CHANNELS; i++) {
    if (IsActive[i]) {
      activeChannels += 1;
    }
  }
  return activeChannels;
}

void distortPattern() {
  // Distort the original pattern by removing the last event.
  for (int i=0; i<NUM_CHANNELS; i++) {
    // Turn off all output pins.
    digitalWrite(LED_PINS[i], LOW);

    // Ignore channels that are not active.
    if (!IsActive[i]) continue;

    // Remove the last event for the channel.
    // NOTE: This will destroy the loop synchronization.
    channelMsg(i, "removing event");

    unsigned int numEvents = TotalEvents[i];
    TotalEvents[i] = numEvents - 1;
    Position[i]    = 1;

    // Disable the channel if there are only two events remaining.
    if (TotalEvents[i] == 2) {
      channelMsg(i, "de-activating channel");
      IsActive[i] = false;
    }
  }
  Serial.flush();
}

void loop() {
  // NOTE: Any calculations involving timestamps must use `unsigned long` types.
  unsigned long currentTime = millis();

  // Check to see if we need to stop recording. 
  if (Recording) {
    // If the elapsed time since the last event is greater than `RECORD_TIMEOUT`
    // automatically stop the recording session.
    unsigned long elapsed = currentTime - LastEventTime;

    if (elapsed > RECORD_TIMEOUT) {
      stopRecording(currentTime);
    }
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

  // Check to see if we need to stop playback
  if (Playback && currentTime > PlaybackStart) {
    unsigned int currentLoop = int((currentTime - PlaybackStart) / LoopDuration);
    bool startNewLoop = false;

    if (currentLoop > LoopCount) {
      Serial.print("loop ");
      Serial.println(currentLoop);

      LoopCount    = currentLoop;
      startNewLoop = true;  // signal that a new loop has started
    }

    // If the pattern has been played `MAX_LOOPS` times either stop the playback
    // immediately or begin distorting the pattern.
    if (LoopCount >= MAX_LOOPS) {
      // If pattern distortion has not been enabled, simply stop playback as soon
      // as the maximum number of loop iterations has completed. Otherwise,
      // continue to distort the pattern.
      if (!DISTORTION) {
        Serial.println("stopping playback...");
        Playback = false;
      } else if (startNewLoop){
        distortPattern();

        // Stop playback once the pattern has been completely erased.
        if (activeChannelCount() == 0) {
          Serial.println("all channel inactive; stopping playback");
          Playback = false;
        }
      }
    }
  }

  if (Playback) {
    for (int i=0; i<NUM_CHANNELS; i++) {
      if (!IsActive[i]) {
        continue;
      }

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

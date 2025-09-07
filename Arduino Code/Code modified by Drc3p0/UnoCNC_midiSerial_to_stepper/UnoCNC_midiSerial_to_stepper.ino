/*
 * MIDI STEPPER V1 - Modified by Drc3p0 for Polyphonic Serial Input from Teensy
 * 
 * Modified from original by Jonathan Kayne / jzkmath
 * Implements voice allocation for up to 4 simultaneous notes

 Disables motors as soon as note is done playing to help reduce EMF emissions
 */

//ARDUINO PINS - configured for CNC Shield V3
#define stepPin_M1 2
#define stepPin_M2 3
#define stepPin_M3 4
#define stepPin_M4 12

//Direction Pins (optional)
#define dirPin_M1 5 
#define dirPin_M2 6
#define dirPin_M3 7
#define dirPin_M4 13

#define enPin 8 //Steppers are enabled when EN pin is pulled LOW

#define TIMEOUT 10000 //Number of milliseconds for watchdog timer
#define MAX_VOICES 4  //Maximum number of simultaneous notes

// Voice allocation structure
struct Voice {
  byte midiNote;           // MIDI note number (0 = voice free)
  unsigned long stepInterval; // Microseconds between steps
  unsigned long lastStepTime;  // Last step timestamp
  bool active;             // Is this voice currently playing?
};

Voice voices[MAX_VOICES + 1]; // Index 0 unused, voices 1-4 correspond to steppers

const bool motorDirection = LOW;
bool disableSteppers = HIGH;
unsigned long WDT; // Watchdog timer

// Step pin array for easier access
const byte stepPins[] = {0, stepPin_M1, stepPin_M2, stepPin_M3, stepPin_M4};

void setup() 
{
  pinMode(stepPin_M1, OUTPUT);
  pinMode(stepPin_M2, OUTPUT);
  pinMode(stepPin_M3, OUTPUT);
  pinMode(stepPin_M4, OUTPUT);

  pinMode(dirPin_M1, OUTPUT);
  pinMode(dirPin_M2, OUTPUT);
  pinMode(dirPin_M3, OUTPUT);
  pinMode(dirPin_M4, OUTPUT);
  digitalWrite(dirPin_M1, motorDirection);
  digitalWrite(dirPin_M2, motorDirection);
  digitalWrite(dirPin_M3, motorDirection);
  digitalWrite(dirPin_M4, motorDirection);
  
  pinMode(enPin, OUTPUT);

  // Initialize voices
  for(int i = 1; i <= MAX_VOICES; i++) {
    voices[i].midiNote = 0;
    voices[i].stepInterval = 0;
    voices[i].lastStepTime = 0;
    voices[i].active = false;
  }

  Serial.begin(115200);
  Serial.println("Arduino Polyphonic Stepper Controller Ready");
}

void loop() 
{
  // Handle serial commands from Teensy
  if(Serial.available()) {
    String command = Serial.readStringUntil('\n');
    parseCommand(command);
  }
  
  digitalWrite(enPin, disableSteppers);
  
  // Run all active voices
  for(int i = 1; i <= MAX_VOICES; i++) {
    if(voices[i].active) {
      singleStep(i);
    }
  }

  // Check if any voices are still active
  bool anyActive = false;
  for(int i = 1; i <= MAX_VOICES; i++) {
    if(voices[i].active) {
      anyActive = true;
      break;
    }
  }
  
  // Enable steppers only when notes are active, disable immediately when not
  if (anyActive) {
    disableSteppers = LOW;  // Enable steppers
    WDT = millis(); // Reset watchdog if any voice is active
  } else {
    disableSteppers = HIGH; // Disable steppers immediately when no voices active
  }
}

void parseCommand(String cmd) {
  if(cmd.startsWith("N")) {
    // Note on: N<channel>,<frequency>,<velocity>
    int firstComma = cmd.indexOf(',');
    int secondComma = cmd.indexOf(',', firstComma + 1);
    
    if(firstComma > 0 && secondComma > 0) {
      int channel = cmd.substring(1, firstComma).toInt();
      float freq = cmd.substring(firstComma + 1, secondComma).toFloat();
      int velocity = cmd.substring(secondComma + 1).toInt();
      
      // Extract MIDI note from frequency (approximate)
      // Using log(freq/440)/log(2) instead of log2(freq/440)
      byte midiNote = (byte)(69 + 12 * (log(freq / 440.0) / log(2.0)));
      
      handleNoteOn(midiNote, freq, velocity);
    }
  }
  else if(cmd.startsWith("F")) {
    // Note off: F<channel>
    // Since we're doing voice allocation, we need to modify Teensy code
    // to send the actual MIDI note number instead of just channel
    // For now, this will turn off the most recent note
    handleNoteOff(0); // Will be improved when Teensy sends note number
  }
}

void handleNoteOn(byte midiNote, float frequency, byte velocity)
{
  // Find an available voice
  int voiceIndex = findAvailableVoice();
  
  if(voiceIndex > 0) {
    voices[voiceIndex].midiNote = midiNote;
    voices[voiceIndex].active = true;
    voices[voiceIndex].lastStepTime = micros();
    
    // Convert frequency to step interval
    if(frequency > 0) {
      voices[voiceIndex].stepInterval = (unsigned long)(1000000.0 / (frequency * 2.0));
    } else {
      voices[voiceIndex].stepInterval = 0;
    }
    
    Serial.print("Note ON - Voice: ");
    Serial.print(voiceIndex);
    Serial.print(", MIDI Note: ");
    Serial.print(midiNote);
    Serial.print(", Freq: ");
    Serial.print(frequency);
    Serial.println("Hz");
    
    disableSteppers = LOW; // Enable steppers
  } else {
    Serial.println("No available voices!");
  }
}

void handleNoteOff(byte midiNote)
{
  // If midiNote is 0, turn off the most recently activated voice
  if(midiNote == 0) {
    for(int i = MAX_VOICES; i >= 1; i--) {
      if(voices[i].active) {
        voices[i].active = false;
        voices[i].midiNote = 0;
        Serial.print("Note OFF - Voice: ");
        Serial.println(i);
        break;
      }
    }
  } else {
    // Find and turn off the specific note
    for(int i = 1; i <= MAX_VOICES; i++) {
      if(voices[i].active && voices[i].midiNote == midiNote) {
        voices[i].active = false;
        voices[i].midiNote = 0;
        Serial.print("Note OFF - Voice: ");
        Serial.print(i);
        Serial.print(", MIDI Note: ");
        Serial.println(midiNote);
        break;
      }
    }
  }
}

int findAvailableVoice()
{
  // First, try to find a completely free voice
  for(int i = 1; i <= MAX_VOICES; i++) {
    if(!voices[i].active) {
      return i;
    }
  }
  
  // If no free voices, steal the oldest one (voice 1)
  Serial.println("Voice stealing - reusing voice 1");
  return 1;
}

void singleStep(int voiceIndex)
{
  if(voices[voiceIndex].stepInterval > 0) {
    unsigned long currentTime = micros();
    
    if(currentTime - voices[voiceIndex].lastStepTime >= voices[voiceIndex].stepInterval) {
      voices[voiceIndex].lastStepTime += voices[voiceIndex].stepInterval;
      
      digitalWrite(stepPins[voiceIndex], HIGH);
      digitalWrite(stepPins[voiceIndex], LOW);
    }
  }
}
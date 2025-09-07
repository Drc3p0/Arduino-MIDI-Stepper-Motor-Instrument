/*
 MIDI STEPPER V2 - Independent Enables for CNC Shield V3
 
Modified from original by Jonathan Kayne / jzkmath
by Drc3p0 for the EMF Explorer booth at bay area maker faire

Implements voice allocation for up to 4 simultaneous notes

 To reduce EMF noise when sounds arent being played, we must enable the motors independently. 
 by default, enable pins are tied together on the CNC shield. 
 To isolate them, bend up enable pin on motor drivers. Connect each EN pin with 10K to 5V.
 We will connect them to pins 8-11 so they can be enabled independently. 
 Those pins are available on the shield for easier connection:

 |en pin|CNC shield |Uno | 
 |  X   |    EN     |  8 |
 |  Y   |X end stops|  9 |
 |  Z   |Y end stops| 10 |
 |  A   |Z end stops| 11 |
 */

#define stepPin_M1 2
#define stepPin_M2 3
#define stepPin_M3 4
#define stepPin_M4 12

#define dirPin_M1 5 
#define dirPin_M2 6
#define dirPin_M3 7
#define dirPin_M4 13

// Independent enable pins (after hardware mod)
#define enPin_M1 8
#define enPin_M2 9
#define enPin_M3 10
#define enPin_M4 11

#define MAX_VOICES 4

struct Voice {
  byte midiNote;
  unsigned long stepInterval;
  unsigned long lastStepTime;
  bool active;
};

Voice voices[MAX_VOICES + 1]; 

const bool motorDirection = LOW;
const byte stepPins[] = {0, stepPin_M1, stepPin_M2, stepPin_M3, stepPin_M4};
const byte dirPins[]  = {0, dirPin_M1, dirPin_M2, dirPin_M3, dirPin_M4};
const byte enPins[]   = {0, enPin_M1, enPin_M2, enPin_M3, enPin_M4};

void setup() 
{
  for (int i = 1; i <= MAX_VOICES; i++) {
    pinMode(stepPins[i], OUTPUT);
    pinMode(dirPins[i], OUTPUT);
    digitalWrite(dirPins[i], motorDirection);

    pinMode(enPins[i], OUTPUT);
    digitalWrite(enPins[i], HIGH); // Disabled by default

    voices[i].midiNote = 0;
    voices[i].stepInterval = 0;
    voices[i].lastStepTime = 0;
    voices[i].active = false;
  }

  Serial.begin(115200);
  Serial.println("Arduino Polyphonic Stepper Controller w/ Independent EN Ready");
}

void loop() 
{
  if(Serial.available()) {
    String command = Serial.readStringUntil('\n');
    parseCommand(command);
  }
  
  // Step active voices
  for(int i = 1; i <= MAX_VOICES; i++) {
    if(voices[i].active) {
      singleStep(i);
    }
  }
}

void parseCommand(String cmd) {
  if(cmd.startsWith("N")) {
    int firstComma = cmd.indexOf(',');
    int secondComma = cmd.indexOf(',', firstComma + 1);
    
    if(firstComma > 0 && secondComma > 0) {
      int channel = cmd.substring(1, firstComma).toInt();
      float freq = cmd.substring(firstComma + 1, secondComma).toFloat();
      int velocity = cmd.substring(secondComma + 1).toInt();
      
      byte midiNote = (byte)(69 + 12 * (log(freq / 440.0) / log(2.0)));
      handleNoteOn(midiNote, freq, velocity);
    }
  }
  else if(cmd.startsWith("F")) {
    handleNoteOff(0);
  }
}

void handleNoteOn(byte midiNote, float frequency, byte velocity)
{
  int voiceIndex = findAvailableVoice();
  
  if(voiceIndex > 0) {
    voices[voiceIndex].midiNote = midiNote;
    voices[voiceIndex].active = true;
    voices[voiceIndex].lastStepTime = micros();
    
    if(frequency > 0) {
      voices[voiceIndex].stepInterval = (unsigned long)(1000000.0 / (frequency * 2.0));
    } else {
      voices[voiceIndex].stepInterval = 0;
    }

    digitalWrite(enPins[voiceIndex], LOW); // Enable motor

    Serial.print("Note ON - Voice: ");
    Serial.print(voiceIndex);
    Serial.print(", MIDI Note: ");
    Serial.print(midiNote);
    Serial.print(", Freq: ");
    Serial.print(frequency);
    Serial.println("Hz");
  } else {
    Serial.println("No available voices!");
  }
}

void handleNoteOff(byte midiNote)
{
  if(midiNote == 0) {
    for(int i = MAX_VOICES; i >= 1; i--) {
      if(voices[i].active) {
        voices[i].active = false;
        voices[i].midiNote = 0;
        voices[i].stepInterval = 0;
        digitalWrite(enPins[i], HIGH); // Disable motor

        Serial.print("Note OFF - Voice: ");
        Serial.println(i);
        break;
      }
    }
  } else {
    for(int i = 1; i <= MAX_VOICES; i++) {
      if(voices[i].active && voices[i].midiNote == midiNote) {
        voices[i].active = false;
        voices[i].midiNote = 0;
        voices[i].stepInterval = 0;
        digitalWrite(enPins[i], HIGH); // Disable motor

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
  for(int i = 1; i <= MAX_VOICES; i++) {
    if(!voices[i].active) {
      return i;
    }
  }
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

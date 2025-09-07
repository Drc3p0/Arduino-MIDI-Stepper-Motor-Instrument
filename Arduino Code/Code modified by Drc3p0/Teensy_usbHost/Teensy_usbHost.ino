/* Uses the Teensy 4.1 Takes MIDI from usbMIDI devices and sends those signals via pin 8/TX to the CNCshield RX. */
 

#include <USBHost_t36.h>

USBHost myusb;
MIDIDevice midi1(myusb);

const int LED_PIN = 13;  // Built-in LED on Teensy 4.1
unsigned long ledOffTime = 0;
const unsigned long LED_ON_DURATION = 100;  // LED stays on for 100ms per note

void setup() {
  Serial.begin(115200);   // Debug
  Serial2.begin(115200);  // To Arduino
  myusb.begin();
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Teensy MIDI to Stepper Motor Controller Ready");
}

void loop() {
  myusb.Task();
  
  // Handle LED timing
  if (ledOffTime > 0 && millis() >= ledOffTime) {
    digitalWrite(LED_PIN, LOW);
    ledOffTime = 0;
  }
  
  if (midi1.read()) {
    uint8_t type = midi1.getType();
    uint8_t channel = midi1.getChannel();
    uint8_t data1 = midi1.getData1();
    uint8_t data2 = midi1.getData2();
    
    if (type == usbMIDI.NoteOn && data2 > 0) {
      handleNoteOn(channel, data1, data2);
    } else if (type == usbMIDI.NoteOff || (type == usbMIDI.NoteOn && data2 == 0)) {
      handleNoteOff(channel, data1, data2);
    }
  }
}

void handleNoteOn(byte channel, byte note, byte velocity) {
  // Turn on LED and set timer
  digitalWrite(LED_PIN, HIGH);
  ledOffTime = millis() + LED_ON_DURATION;
  
  // Convert MIDI note to frequency
  float frequency = 440.0 * pow(2.0, (note - 69) / 12.0);
  
  // Send to Arduino: format "N<channel><frequency><velocity>"
  Serial2.print("N");
  Serial2.print(channel);
  Serial2.print(",");
  Serial2.print(frequency, 2);
  Serial2.print(",");
  Serial2.println(velocity);
  
  // Debug output
  Serial.print("Note ON - Channel: ");
  Serial.print(channel);
  Serial.print(", Note: ");
  Serial.print(note);
  Serial.print(", Frequency: ");
  Serial.print(frequency, 2);
  Serial.print("Hz, Velocity: ");
  Serial.println(velocity);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  // Brief LED flash for note off (optional)
  digitalWrite(LED_PIN, HIGH);
  ledOffTime = millis() + 50;  // Shorter flash for note off
  
  // Send note off command with MIDI note number for proper voice allocation
  Serial2.print("F");
  Serial2.println(note);  // Send MIDI note number instead of channel
  
  // Debug output
  Serial.print("Note OFF - Channel: ");
  Serial.print(channel);
  Serial.print(", Note: ");
  Serial.println(note);
}
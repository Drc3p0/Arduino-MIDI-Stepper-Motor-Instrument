/* SD card code currently not working.*/

#include <USBHost_t36.h>
#include <SD.h>
#include <SPI.h>

USBHost myusb;
MIDIDevice midi1(myusb);

const int LED_PIN = 13;
const int SONG_BUTTON_PIN = 14;  // Button to cycle through songs and auto-play
const int MIDI_BUTTON_PIN = 15;  // Button to stop songs and switch to live MIDI

// SD Card pin (Teensy 4.1 uses built-in SD)
const int SD_CS_PIN = BUILTIN_SDCARD;

unsigned long ledOffTime = 0;
const unsigned long LED_ON_DURATION = 100;

// Playback mode
enum PlayMode {
  LIVE_MIDI,
  PLAYBACK
};

PlayMode currentMode = LIVE_MIDI;
int currentSong = 0;
bool isPlaying = false;
unsigned long songStartTime = 0;
int currentNoteIndex = 0;

// Button debouncing
unsigned long lastButtonPress[] = {0, 0};
const unsigned long DEBOUNCE_DELAY = 300;

// MIDI file handling
String midiFiles[50];  // Array to store MIDI filenames
int numMidiFiles = 0;
File currentMidiFile;

// MIDI parsing structures
struct MIDIEvent {
  unsigned long deltaTime;
  byte type;
  byte channel;
  byte data1;
  byte data2;
};

// Simple MIDI playback variables
unsigned long ticksPerQuarter = 480;  // Default, will be read from file
unsigned long microsecondsPerQuarter = 500000;  // 120 BPM default
unsigned long currentTick = 0;
unsigned long nextEventTime = 0;
bool songLoaded = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  myusb.begin();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(SONG_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MIDI_BUTTON_PIN, INPUT_PULLUP);
  
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Teensy MIDI Controller with SD Card Playback");
  Serial.println("Controls:");
  Serial.println("- Song Button (Pin 14): Cycle through songs and auto-play");
  Serial.println("- MIDI Button (Pin 15): Stop playback and switch to live MIDI");
  
  // Initialize SD card
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card initialization failed!");
    Serial.println("Insert SD card and reset");
    while (1) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  }
  
  Serial.println("SD card initialized successfully");
  loadMidiFileList();
  
  Serial.println("\nMode: Live MIDI (waiting for keyboard input)");
  if (numMidiFiles > 0) {
    Serial.println("Press Song Button to start playing MIDI files");
  }
}

void loop() {
  myusb.Task();
  
  // Handle buttons
  checkButtons();
  
  // Handle LED timing
  if (ledOffTime > 0 && millis() >= ledOffTime) {
    digitalWrite(LED_PIN, LOW);
    ledOffTime = 0;
  }
  
  if (currentMode == LIVE_MIDI) {
    // Handle live MIDI input
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
  } else {
    // Handle song playback
    if (isPlaying && songLoaded) {
      playMidiFile();
    }
  }
}

void loadMidiFileList() {
  numMidiFiles = 0;
  File root = SD.open("/");
  
  Serial.println("Scanning for MIDI files...");
  
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    
    if (!entry.isDirectory()) {
      String filename = entry.name();
      filename.toUpperCase();
      
      if (filename.endsWith(".MID") && numMidiFiles < 50) {
        midiFiles[numMidiFiles] = entry.name();
        Serial.print("Found: ");
        Serial.println(midiFiles[numMidiFiles]);
        numMidiFiles++;
      }
    }
    entry.close();
  }
  root.close();
  
  Serial.print("Found ");
  Serial.print(numMidiFiles);
  Serial.println(" MIDI files");
}

void checkButtons() {
  unsigned long currentTime = millis();
  
  // Song button - cycle through songs and auto-play
  if (digitalRead(SONG_BUTTON_PIN) == LOW && 
      currentTime - lastButtonPress[0] > DEBOUNCE_DELAY) {
    lastButtonPress[0] = currentTime;
    
    if (numMidiFiles > 0) {
      // Move to next song
      currentSong = (currentSong + 1) % numMidiFiles;
      
      // Stop current playback
      stopAllNotes();
      if (currentMidiFile) currentMidiFile.close();
      
      // Switch to playback mode and start playing
      currentMode = PLAYBACK;
      songLoaded = false;
      loadCurrentSong();
      
      if (songLoaded) {
        startSong();
        Serial.println("Mode: Playback");
      }
    }
  }
  
  // MIDI button - stop playback and switch to live MIDI
  if (digitalRead(MIDI_BUTTON_PIN) == LOW && 
      currentTime - lastButtonPress[1] > DEBOUNCE_DELAY) {
    lastButtonPress[1] = currentTime;
    
    // Stop playback
    isPlaying = false;
    stopAllNotes();
    if (currentMidiFile) currentMidiFile.close();
    
    // Switch to live MIDI mode
    currentMode = LIVE_MIDI;
    Serial.println("Mode: Live MIDI (listening for keyboard input)");
  }
}

void loadCurrentSong() {
  if (numMidiFiles == 0) return;
  
  String filepath = "/" + midiFiles[currentSong];
  currentMidiFile = SD.open(filepath.c_str());
  
  if (!currentMidiFile) {
    Serial.print("Error opening: ");
    Serial.println(filepath);
    return;
  }
  
  Serial.print("Loading: ");
  Serial.println(midiFiles[currentSong]);
  
  // Simple MIDI header parsing
  if (parseMidiHeader()) {
    songLoaded = true;
    currentNoteIndex = 0;
    Serial.println("Song loaded successfully");
  } else {
    Serial.println("Error parsing MIDI file");
    currentMidiFile.close();
  }
}

bool parseMidiHeader() {
  // Read MIDI header chunk
  char header[4];
  currentMidiFile.read(header, 4);
  
  if (strncmp(header, "MThd", 4) != 0) {
    Serial.println("Not a valid MIDI file");
    return false;
  }
  
  // Skip header length (4 bytes)
  currentMidiFile.seek(currentMidiFile.position() + 4);
  
  // Read format type (2 bytes)
  byte formatBytes[2];
  currentMidiFile.read(formatBytes, 2);
  
  // Read number of tracks (2 bytes)
  currentMidiFile.read(formatBytes, 2);
  
  // Read ticks per quarter note (2 bytes)
  currentMidiFile.read(formatBytes, 2);
  ticksPerQuarter = (formatBytes[0] << 8) | formatBytes[1];
  
  Serial.print("Ticks per quarter: ");
  Serial.println(ticksPerQuarter);
  
  // Find first track
  return findNextTrack();
}

bool findNextTrack() {
  char trackHeader[4];
  
  while (currentMidiFile.available()) {
    if (currentMidiFile.read(trackHeader, 4) == 4) {
      if (strncmp(trackHeader, "MTrk", 4) == 0) {
        // Skip track length (4 bytes)
        currentMidiFile.seek(currentMidiFile.position() + 4);
        return true;
      }
    }
  }
  return false;
}

void startSong() {
  songStartTime = millis();
  currentTick = 0;
  nextEventTime = 0;
  isPlaying = true;
  
  Serial.print("Now Playing: ");
  Serial.print(currentSong + 1);
  Serial.print("/");
  Serial.print(numMidiFiles);
  Serial.print(" - ");
  Serial.println(midiFiles[currentSong]);
}

void playMidiFile() {
  if (!currentMidiFile.available()) {
    // Song finished - auto-advance to next song
    Serial.println("Song finished - loading next song");
    currentMidiFile.close();
    songLoaded = false;
    
    // Move to next song
    currentSong = (currentSong + 1) % numMidiFiles;
    loadCurrentSong();
    
    if (songLoaded) {
      startSong();
    } else {
      isPlaying = false;
    }
    return;
  }
  
  unsigned long elapsed = millis() - songStartTime;
  
  // Simple MIDI event parsing (very basic implementation)
  if (elapsed >= nextEventTime) {
    MIDIEvent event;
    if (readNextMidiEvent(event)) {
      processMidiEvent(event);
    }
  }
}

bool readNextMidiEvent(MIDIEvent &event) {
  // This is a very simplified MIDI parser
  // Real MIDI files have complex delta-time encoding and running status
  
  if (!currentMidiFile.available()) return false;
  
  // Read delta time (simplified - assuming single byte)
  event.deltaTime = currentMidiFile.read();
  
  // Calculate next event time
  unsigned long deltaMs = (event.deltaTime * microsecondsPerQuarter) / (ticksPerQuarter * 1000);
  nextEventTime += deltaMs;
  
  // Read status byte
  byte status = currentMidiFile.read();
  event.type = status & 0xF0;
  event.channel = (status & 0x0F) + 1;  // Convert to 1-4 for our steppers
  event.channel = ((event.channel - 1) % 4) + 1;  // Ensure 1-4 range
  
  // Read data bytes
  if (event.type == 0x90 || event.type == 0x80) {  // Note on/off
    event.data1 = currentMidiFile.read();  // Note number
    event.data2 = currentMidiFile.read();  // Velocity
    return true;
  } else {
    // Skip other events for now
    currentMidiFile.read();  // Skip data byte 1
    currentMidiFile.read();  // Skip data byte 2
    return false;
  }
}

void processMidiEvent(MIDIEvent &event) {
  if (event.type == 0x90 && event.data2 > 0) {  // Note on
    handleNoteOn(event.channel, event.data1, event.data2);
  } else if (event.type == 0x80 || (event.type == 0x90 && event.data2 == 0)) {  // Note off
    handleNoteOff(event.channel, event.data1, 0);
  }
}

void handleNoteOn(byte channel, byte note, byte velocity) {
  digitalWrite(LED_PIN, HIGH);
  ledOffTime = millis() + LED_ON_DURATION;
  
  float frequency = 440.0 * pow(2.0, (note - 69) / 12.0);
  
  Serial2.print("N");
  Serial2.print(channel);
  Serial2.print(",");
  Serial2.print(frequency, 2);
  Serial2.print(",");
  Serial2.println(velocity);
  
  if (currentMode == LIVE_MIDI) {  // Only print debug in live mode
    Serial.print("Note ON - Ch: ");
    Serial.print(channel);
    Serial.print(", Note: ");
    Serial.print(note);
    Serial.print(", Freq: ");
    Serial.print(frequency, 2);
    Serial.println("Hz");
  }
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  digitalWrite(LED_PIN, HIGH);
  ledOffTime = millis() + 50;
  
  Serial2.print("F");
  Serial2.println(note);
}

void stopAllNotes() {
  // Send note off for all possible notes
  for (int i = 0; i < 128; i++) {
    Serial2.print("F");
    Serial2.println(i);
  }
}
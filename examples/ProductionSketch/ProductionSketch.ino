// This is the example sketch that gets loaded on every BlinkyTape during production!
#include <EEPROM.h>
#include <FastLED.h>

#include "BlinkyTape.h"
#include "ColorLoop.h"
#include "SerialLoop.h"
#include "Shimmer.h"
#include "Scanner.h"
#include "Flashlight.h"

// LED data array
struct CRGB leds[MAX_LEDS];   // Space to hold the current LED data
CLEDController* controller;   // LED controller

const uint8_t brightnessCount = 8;
const uint8_t brightnesSteps[brightnessCount] = {93, 70, 40, 15, 7, 15, 40, 70};

uint8_t currentBrightness;
uint8_t lastBrightness;


// switch pattern after 10 seconds
static uint32_t prevTime;
uint32_t curTime = millis();

// For fading in a new sketch
long lastTime;

float fadeIndex;
const uint8_t fadeSteps = 50;

// Button interrupt variables and Interrupt Service Routine
uint8_t buttonState = 0;
bool buttonDebounced;
long buttonDownTime = 0;
long buttonPressTime = 0;

#define BUTTON_BRIGHTNESS_SWITCH_TIME  1     // Time to hold the button down to switch brightness
#define BUTTON_PATTERN_SWITCH_TIME    1000   // Time to hold the button down to switch patterns


#define EEPROM_START_ADDRESS  0
#define EEPROM_MAGIG_BYTE_0   0x12
#define EEPROM_MAGIC_BYTE_1   0x34
#define PATTERN_EEPROM_ADDRESS EEPROM_START_ADDRESS + 2
#define BRIGHTNESS_EEPROM_ADDRESS EEPROM_START_ADDRESS + 3

uint8_t currentPattern = 0;
uint8_t patternCount = 0;
const uint8_t maxPatternCount = 10;
Pattern* patterns[maxPatternCount];

// Our patterns
ColorLoop originalRainbow(1,1,1);
ColorLoop blueRainbow(.2,1,1);
Scanner scanner(4, CRGB(255,0,0));
Flashlight flashlight(CRGB(255,255,255));
Shimmer shimmer(1,1,1);


// Register a pattern
void registerPattern(Pattern* newPattern) {
  // Only add the pattern if there is space for it.
  if(patternCount >= maxPatternCount) {
    return;
  }
  
  patterns[patternCount] = newPattern;
  patternCount++;
}

// Change the current pattern
void setPattern(uint8_t newPattern) {
  currentPattern = newPattern%patternCount;

  if(EEPROM.read(PATTERN_EEPROM_ADDRESS) != currentPattern) {
    EEPROM.write(PATTERN_EEPROM_ADDRESS, currentPattern);
  }
  
  patterns[currentPattern]->reset();
  
  lastTime = millis();
  fadeIndex = 0;
}

void setBrightness(uint8_t newBrightness) {
  currentBrightness = newBrightness%brightnessCount;

  if(EEPROM.read(BRIGHTNESS_EEPROM_ADDRESS) != currentBrightness) {
    EEPROM.write(BRIGHTNESS_EEPROM_ADDRESS, currentBrightness);
  }
  
  LEDS.setBrightness(brightnesSteps[currentBrightness]);
}



// Called when the button is both pressed and released.
ISR(PCINT0_vect) {
  buttonState = !(PINB & (1 << PINB6)); // Reading state of the PB6 (remember that HIGH == released)
  bool dis = false;
  if(dis){
    if (buttonState ) {
      // On button down, record the time so we can convert this into a gesture later
      buttonDownTime = millis();
      buttonDebounced = false;
  
      // And configure and start timer4 interrupt.
      TCCR4B = 0x0F; // Slowest prescaler
      TCCR4D = _BV(WGM41) | _BV(WGM40);  // Fast PWM mode
      OCR4C = 0x10;        // some random percentage of the clock
      TCNT4 = 0;  // Reset the counter
      TIMSK4 = _BV(TOV4);  // turn on the interrupt
  
    }
    else {
      TIMSK4 = 0;  // turn off the interrupt
    }
    
  }

}

// This is called every xx ms while the button is being held down; it counts down then displays a
// visual cue and changes the pattern.
ISR(TIMER4_OVF_vect) {
  // If the user is still holding down the button after the first cycle, they were serious about it.
  if(buttonDebounced == false) {
    buttonDebounced = true;
    lastBrightness = currentBrightness;
    
    setBrightness(currentBrightness + 1);
  }
  
  // If we've waited long enough, switch the pattern
  // TODO: visual indicator
  buttonPressTime = millis() - buttonDownTime;
  if(buttonPressTime > BUTTON_PATTERN_SWITCH_TIME) {
    // first unroll the brightness!
    setBrightness(lastBrightness);
    
    setPattern(currentPattern+1);
    
    // Finally, reset the button down time, so we don't advance again too quickly
    buttonDownTime = millis();
  }
}

void setup()
{  
  Serial.begin(57600);

  pinMode(BUTTON_IN, INPUT_PULLUP);
  pinMode(ANALOG_INPUT, INPUT_PULLUP);
  pinMode(EXTRA_PIN_A, INPUT_PULLUP);
  pinMode(EXTRA_PIN_B, INPUT_PULLUP);
  
  // Interrupt set-up; see Atmega32u4 datasheet section 11
  PCIFR  |= (1 << PCIF0);  // Just in case, clear interrupt flag
  PCMSK0 |= (1 << PCINT6); // Set interrupt mask to the button pin (PCINT6)
  PCICR  |= (1 << PCIE0);  // Enable interrupt
  
  registerPattern(&originalRainbow);
  registerPattern(&blueRainbow);
  registerPattern(&scanner);
  registerPattern(&shimmer);
  registerPattern(&flashlight);
  

  // If the EEPROM hasn't been initialized, do so now
  if((EEPROM.read(EEPROM_START_ADDRESS) != EEPROM_MAGIG_BYTE_0)
     || (EEPROM.read(EEPROM_START_ADDRESS + 1) != EEPROM_MAGIC_BYTE_1)) {
    EEPROM.write(EEPROM_START_ADDRESS, EEPROM_MAGIG_BYTE_0);
    EEPROM.write(EEPROM_START_ADDRESS + 1, EEPROM_MAGIC_BYTE_1);
    EEPROM.write(PATTERN_EEPROM_ADDRESS, 0);
    EEPROM.write(BRIGHTNESS_EEPROM_ADDRESS, 0);
  }

  // Read in the last-used pattern and brightness
  currentPattern = EEPROM.read(PATTERN_EEPROM_ADDRESS);
  currentBrightness = EEPROM.read(BRIGHTNESS_EEPROM_ADDRESS); 

  setPattern(currentPattern);
  setBrightness(currentBrightness);

  controller = &(LEDS.addLeds<WS2811, LED_OUT, GRB>(leds, DEFAULT_LED_COUNT));
  LEDS.show();
}

void loop()
{
  // If'n we get some data, switch to passthrough mode
  if(Serial.available() > 0) {
    // Make sure w're fully on the new brightness
    LEDS.setBrightness(brightnesSteps[currentBrightness]);
    serialLoop(leds);
  }
  
  
  if ( curTime - prevTime >= 10*1000UL )
  {
     prevTime = curTime; 
     setPattern(currentPattern + 1);
  }
  
  // Draw the current pattern
  patterns[currentPattern]->draw(leds);

  if((fadeIndex < fadeSteps) && (millis() - lastTime > 15)) {
    lastTime = millis();
    fadeIndex++;
    
    LEDS.setBrightness(brightnesSteps[currentBrightness]*(fadeIndex/fadeSteps));
  }

  LEDS.show();
}


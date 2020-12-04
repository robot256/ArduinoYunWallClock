// A basic everyday NeoPixel strip test program.

// NEOPIXEL BEST PRACTICES for most reliable operation:
// - Add 1000 uF CAPACITOR between NeoPixel strip's + and - connections.
// - MINIMIZE WIRING LENGTH between microcontroller board and first pixel.
// - NeoPixel strip's DATA-IN should pass through a 300-500 OHM RESISTOR.
// - AVOID connecting NeoPixels on a LIVE CIRCUIT. If you must, ALWAYS
//   connect GROUND (-) first, then +, then data.
// - When using a 3.3V microcontroller with a 5V-powered NeoPixel strip,
//   a LOGIC-LEVEL CONVERTER on the data line is STRONGLY RECOMMENDED.
// (Skipping these may work OK on your workbench but can fail in the field)

#include <FastLED.h>

#include <Mailbox.h>
#include <Process.h>

#include <AceTime.h>
using namespace ace_time;
using namespace ace_time::clock;
// ZoneProcessor instance should be created statically at initialization time.
static BasicZoneProcessor tzProcessor;
static SystemClockLoop systemClock(nullptr /*reference*/, nullptr /*backup*/);
auto localTz = TimeZone::forZoneInfo(&zonedb::kZoneAmerica_New_York, &tzProcessor);

#define TIME_SYNC_PERIOD (1000L*60L*30L)


// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1:
#define LED_PIN    3


#define LEDS_PER_SEGMENT 3
#define LEDS_PER_DIGIT (LEDS_PER_SEGMENT*7)
#define LEDS_PER_DOT 1
#define LEDS_PER_TWODOTS (LEDS_PER_DOT*2)
#define NUM_DIGITS 7
#define NUM_TWODOTS 2


// How many NeoPixels are attached to the Arduino?
#define NUM_LEDS (NUM_DIGITS*LEDS_PER_DIGIT + NUM_TWODOTS*LEDS_PER_TWODOTS)

CRGB leds[NUM_LEDS];


// DIGIT:
//    12 13 14
// 11          15
// 10          16
//  9          17
//    20 19 18
//  8           0
//  7           1
//  6           2
//     5  4  3

// TWODOTS:
//
//    0
//
//    1
//

#define DIGIT_CHARSET_SIZE 38
const PROGMEM char digit_charset[DIGIT_CHARSET_SIZE] = {
  ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8',
  '9', '-', '_', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
  'H', 'J', 'L', 'O', 'P', 'R', 'T', 'U', 'Y', '|',
  '[', ']', '~', 'I', 'S', '=', '#', '%'
};
const PROGMEM byte digit_typeface[DIGIT_CHARSET_SIZE] = {
  0x00, 0x3F, 0x21, 0x76, 0x73, 0x69, 0x5B, 0x5F, 0x31, 0x7F,
  0xFB, 0x40, 0x02, 0xFD, 0x4F, 0x1E, 0x67, 0x5E, 0x5C, 0x1F,
  0x6D, 0x27, 0x0E, 0x47, 0x7C, 0x44, 0x4E, 0x2F, 0x69, 0x0C,
  0x1E, 0x33, 0x10, 0x21, 0x5B, 0x50, 0x52, 0x12
};


//#define TWODOTS_CHARSET_SIZE 4
//char twodots_charset[TWODOTS_CHARSET_SIZE] = {' ',':','.','*'};


#define CHARBUF_SIZE (NUM_DIGITS+NUM_TWODOTS+1)
char charbuf[CHARBUF_SIZE];


#define MATRIX_WIDTH 37
#define MATRIX_HEIGHT 9



Process date;

CRGB color = CRGB::Red;
CRGB bgcolor = CRGB::Black;


bool dotstate = 1;
long lastTimePoll = 0;
long lastPrint = 0;

// Remember what state the display was commanded to
enum DISPLAY_STATE_t {
  BLANK,
  CLOCK,
  TEXT,
  COUNTDOWN,
  COUNTDOWN_XMAS,
  COUNTDOWN_BIDEN,
  TEMPERATURE
};

enum DISPLAY_STATE_t state = CLOCK;

byte text_state = 8;

byte hueBase = 0;
float hueAngle = 0.0;

// printBuffer options
enum PRINT_TYPE_t {
  NEITHER,
  FOREGROUND,
  BACKGROUND,
  BOTH
};

acetime_t target_epoch;
acetime_t xmas_epoch;
acetime_t biden_epoch;

int temperature_c = -1000;
int temperature_f = 32;
bool temp_useF = false;
// Don't show temperature measurements that are >1 hour stale
acetime_t temp_clear_epoch = 0;
#define TEMP_TIMEOUT 3600

bool autoCycle = true;
int autoCycleState = 0;
int autoCycleTimer = 0;
int autoCycleDelay = 10*40;


// setup() function -- runs once at startup --------------------------------

void setup() {
  // Turn off indicator LED
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);

  // Initialize the LED strip
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is typical
  FastLED.setTemperature(0xD4EBFF);

  // Initialize Bridge and Mailbox
  Bridge.begin();
  Mailbox.begin();

  runDateProcess();

  // Turn on LED indicating setup is complete
  //digitalWrite(13, HIGH);

  // Creating timezones is cheap, so we can create them on the fly as needed.

  // Set the SystemClock using these components.
  auto startTime = ZonedDateTime::forComponents(
                     2020, 11, 26, 14, 00, 0, localTz);
  systemClock.setNow(startTime.toEpochSeconds());

  auto targetTime = ZonedDateTime::forComponents(
                      2021, 1, 1, 0, 0, 0, localTz);
  target_epoch = targetTime.toEpochSeconds();

  auto xmasTime = ZonedDateTime::forComponents(
                    2020, 12, 25, 0, 0, 0, localTz);
  xmas_epoch = xmasTime.toEpochSeconds();

  auto bidenTime = ZonedDateTime::forComponents(
                     2021, 1, 20, 12, 0, 0, localTz);
  biden_epoch = bidenTime.toEpochSeconds();

  lastTimePoll = millis() - (TIME_SYNC_PERIOD + 1000L * 60L * 5L);

  // Clear the display
  clearDisplay();
  printBufferEffect();

}


void runDateProcess()
{
  // run an initial date process. Should return:
  // hh:mm:ss :
  if (!date.running())  {
    date.begin("date");
    date.addParameter("+%s");
    date.run();
  }
}



// loop() function -- runs repeatedly as long as board is on ---------------
void loop() {

  String message;

  // Update the local clock
  systemClock.loop();

  // if there is a message in the Mailbox
  if (Mailbox.messageAvailable()) {
    // read all the messages present in the queue
    while (Mailbox.messageAvailable()) {
      Mailbox.readMessage(message);
      
      // Look for different types of messages
      if (message.startsWith("text=")) {
        clearDisplay();
        message.substring(5).toCharArray(charbuf, CHARBUF_SIZE);
        state = TEXT;
        autoCycle = false;
      }

      else if (message.startsWith("color=")) {
        color = strtol(message.substring(6).c_str(), NULL, 16);
      }

      else if (message.startsWith("bgcolor=")) {
        bgcolor = strtol(message.substring(8).c_str(), NULL, 16);
      }

      else if (message.startsWith("clock")) {
        state = CLOCK;
        autoCycle = false;
      }

      else if (message.startsWith("countdown")) {
        state = COUNTDOWN;
        autoCycle = false;
        
        if (message.charAt(9) == '=') {
          auto temp = LocalDateTime::forDateString(message.substring(10).c_str());
          ZonedDateTime targetTime = ZonedDateTime::forComponents(temp.year(), temp.month(), temp.day(),
                       temp.hour(), temp.minute(), temp.second(), localTz);
          target_epoch = targetTime.toEpochSeconds();
        }
        
      }

      else if (message.startsWith("xmas")) {
        state = COUNTDOWN_XMAS;
        autoCycle = false;
      }

      else if (message.startsWith("biden")) {
        state = COUNTDOWN_BIDEN;
        autoCycle = false;
      }

      else if (message.startsWith("temp")) {
        if(message.charAt(4) == '=') {
          temperature_c = message.substring(5).toInt();
          temperature_f = (int)round((float)temperature_c * 1.8) + 32;
          temp_clear_epoch = systemClock.getNow() + TEMP_TIMEOUT;
        }
        else {
          state = TEMPERATURE;
          autoCycle = false;
          temp_useF = (message.charAt(4) == 'f');
        }
      }
      
      else if (message.startsWith("effect=")) {
        text_state = message.substring(11).toInt();
      }

      else if (message.startsWith("auto")) {
        if(message.charAt(9) == '=') {
          autoCycleDelay = message.substring(10).toInt();
        }
        autoCycle = true;
      }

      else if (message.startsWith("reset")) {
        color = CRGB::Red;
        bgcolor = CRGB::Black;
        text_state = 0;
        autoCycle = false;
        clearDisplay();
        state = CLOCK;
        lastTimePoll -= TIME_SYNC_PERIOD;  // Force time update
      }
    }
  }

  // Synchronize time every 30 minutes
  long now = millis();
  if (now - lastTimePoll >= TIME_SYNC_PERIOD) {
    runDateProcess();
    lastTimePoll = now;
  }

  // Read updated Unix seconds and set internal clock
  if (date.available() > 0) {
    // Returned as ssssssssssss
    String timeString = date.readString();
    long current_unix = timeString.toInt();
    auto temp = LocalDateTime::forUnixSeconds(current_unix);
    systemClock.setNow(temp.toEpochSeconds());
  }


  now = millis();
  if (now - lastPrint > 25L) {
    lastPrint = now;

    if (systemClock.getNow() > temp_clear_epoch) {
      temperature_c = -1000;
    }

    if (autoCycle) {
      UpdateAutoCycle();
    }

    if (state == CLOCK) {
      UpdateClock();
    }
    else if (state == COUNTDOWN) {
      UpdateCountdown();
    }
    else if (state == COUNTDOWN_XMAS) {
      UpdateXmas();
    }
    else if (state == COUNTDOWN_BIDEN) {
      UpdateBiden();
    }
    else if (state == TEMPERATURE) {
      UpdateTemperature(temp_useF);
    }

    UpdateEffects();

    printBufferEffect();
  }
}


void UpdateAutoCycle() {
  if (autoCycleTimer == 0) {
    autoCycleTimer = autoCycleDelay;
    autoCycleState++;
    switch(autoCycleState) {
      case 1:
        text_state = 0;
        color = CRGB::Red;
        bgcolor = CRGB::Black;
        state = CLOCK;
        break;
      case 2:
        state = TEMPERATURE;
        temp_useF = true;
        if(temperature_c != -1000)
          break;
      case 3:
        state = TEMPERATURE;
        temp_useF = false;
        if(temperature_c != -1000)
          break;
        else
          autoCycleState = 4;
      case 4:
        text_state = 11;
        state = CLOCK;
        break;
      case 5:
        state = COUNTDOWN_XMAS;
        break;
      case 6:
        state = COUNTDOWN_BIDEN;
      default:
        autoCycleState = 0;
    }
  }
  else {
    autoCycleTimer--;
  }
}


void UpdateEffects()
{
  switch (text_state) {
    //case 7:
    //  hueAngle = hueAngle + 3.14 * 0.01;
    case 6:
      hueBase -= 5;
      break;
    case 8:
   // case 9:
    case 10:
    case 11:
      hueBase -= 2;
  }
}


void effectRainbow(float scale, float angle, byte hue, byte saturation, byte value)
{
  byte x, y, i;
  float s = scale * 255;
  int cf = (int)(cos(angle) * s);
  int sf = (int)(sin(angle) * s);

  CRGB c;
  for (i = 0; i < NUM_LEDS; i++) {
    x = matrixMapX(i);
    y = matrixMapY(i);
    c.setHSV(0xFF & (hue + (x * cf + y * sf) / MATRIX_WIDTH), saturation, value);
    leds[i] = c;
  }
}

void effectStripes(float scale, float angle, byte frac, CRGB color1, CRGB color2, CRGB color3, CRGB color4)
{
  // Fade from color1 to color2 by a fraction of frac/256

  CRGB c;
  byte f, x, y;
  float s = scale * 255;
  int cf = (int)(cos(angle) * s);
  int sf = (int)(sin(angle) * s);

  for (byte i = 0; i < NUM_LEDS; i++) {
    x = matrixMapX(i);
    y = matrixMapY(i);
    f = 0xC0 & (frac + (x * cf + y * sf) / MATRIX_WIDTH);
    if (f == 0x00) {
      c = color1;
    }
    else if (f == 0x40) {
      c = color2;
    }
    else if (f == 0x80) {
      c = color3;
    }
    else if (f == 0xC0) {
      c = color4;
    }
    leds[i] = c;
  }
}

void effectDualColor(float scale, float angle, byte frac, CRGB color1, CRGB color2)
{
  effectStripes(scale, angle, frac, color1, color1, color2, color2);
}

void effectTriColor(float scale, float angle, byte frac, CRGB color1, CRGB color2, CRGB color3)
{
  effectStripes(scale, angle, frac, color1, color2, color1, color3);
}



// Update Clock output on rightmost 4 digits
void UpdateClock()
{
  acetime_t now = systemClock.getNow();
  auto now_time = ZonedDateTime::forEpochSeconds(now, localTz);
  byte sec = now_time.second();
  sprintf(charbuf, " %02d:%02d:%02d", now_time.hour(), now_time.minute(), sec);
  if (sec & 0x01) {
    charbuf[3] = ' ';
    charbuf[6] = ' ';
  } else {
    charbuf[3] = ':';
    charbuf[6] = ':';
  }

}


void printDelta(acetime_t delta)
{
  byte seconds = delta % 60;
  delta = delta / 60;
  byte minutes = delta % 60;
  delta = delta / 60;
  byte hours = delta % 24;
  int days = delta / 24;

  if (days == 0) {
    // If zero days, show -hh:mm:ss
    sprintf(charbuf, "-%02d:%02d:%02d", hours, minutes, seconds);
    // Blink the colons
    if (seconds & 0x0001) {
      charbuf[3] = ' ';
      charbuf[6] = ' ';
    }
  } else if (days < 100) {
    sprintf(charbuf, "%2d.%02d:%02d", -1 * days, hours, minutes);
    // Blink the colon
    if (seconds & 0x0001) {
      charbuf[6] = ' ';
    }
  } else {
    sprintf(charbuf, "%3d.%02d:%02d", days, hours, minutes);
    // Blink the colon
    if (seconds & 0x0001) {
      charbuf[6] = ' ';
    }
  }
}


// Update Clock output on rightmost 4 digits
void UpdateCountdown()
{
  acetime_t now = systemClock.getNow();
  acetime_t delta = target_epoch - now;

  if (delta < 0) {
    // Timer expired!
    strcpy(charbuf, " YI PP EE");
  } else {
    printDelta(delta);
  }
}

// Update Clock output on rightmost 4 digits
void UpdateXmas()
{
  acetime_t now = systemClock.getNow();
  acetime_t delta = xmas_epoch - now;
  text_state = 8;
  if (delta < 0) {
    // Timer expired!
    strcpy(charbuf, " HO HO HO");
  } else {
    printDelta(delta);
  }
}

// Update Clock output on rightmost 4 digits
void UpdateBiden()
{
  acetime_t now = systemClock.getNow();
  acetime_t delta = biden_epoch - now;
  text_state = 10;
  if (delta < 0) {
    // Timer expired!
    strcpy(charbuf, "Pot US 46");
  } else {
    printDelta(delta);
  }
}

// Update Temperature Display
void UpdateTemperature(bool useF)
{
  if(useF) {
    sprintf(charbuf, "  %4dF", temperature_f);
  }
  else {
    sprintf(charbuf, "  %4dC", temperature_c);
  }

  // Make space for the righthand colon
  if(charbuf[3] == ' ') {
    charbuf[8] = charbuf[7];
    charbuf[7] = charbuf[6];
  }
  else {
    charbuf[8] = charbuf[6];
    charbuf[7] = charbuf[5];
    charbuf[5] = charbuf[4];
    charbuf[4] = charbuf[3];
  }
  charbuf[3] = ' ';
  charbuf[6] = ' ';
  
  text_state = 0;
  bgcolor = CRGB::Black;
  if (temperature_c <= 0) {
    color = 0x50D0FF;  // Freezing
  }
  else if (temperature_c <= 10) {
    color = 0x2040FF;  // Cold
  }
  else if (temperature_c <= 27) {
    color = 0x80FF10;  // Normal
  }
  else {
    color = 0xFF1020;  // Hot
  }
}


void clearDisplay()
{
  memset(charbuf, ' ', CHARBUF_SIZE);
}


// Print characters contained in buffer to the digits
// Digits are ordered 012:34:56
// N = valid digit character, D = valid twodots character
// Valid string formats:
// NNNDNNDNN
// NNNNNNN

// Characters are connected right to left.
// 654:32:10

void printBufferEffect()
{
  if (text_state == 0) {
    printBuffer(BOTH);
  }
  else {
    switch (text_state) {
      /*case 1:
        effectRainbow(1, 0, 0, 255, 255);
        break;
        case 2:
        effectRainbow(1, 3.14*0.25, 0, 255, 255);
        break;
        case 3:
        effectRainbow(2, 0, 0, 255, 255);
        break;
      case 4:
        effectRainbow(2, 3.14 * 0.25, 0, 255, 255);
        break;
      case 5:
        effectRainbow(4, 3.14/2, 0, 255, 255);
        break;*/
      case 6:
        effectRainbow(4, 3.14 / 4, hueBase, 255, 255);
        break;
      /*case 7:
        effectRainbow(2, hueAngle, hueBase, 255, 255);
        break;*/
      case 8:
        effectDualColor(0.75, 3.14 / 4, hueBase, CRGB::Green, CRGB::Red);
        break;
      /*case 9:
        effectDualColor(3, 3.14/4, hueBase, CRGB::Blue, CRGB::Red);
        break;*/
      case 10:
        effectTriColor(0.5, 3.14 / 4, hueBase, CRGB::Red, CRGB::White, CRGB::Blue);
        break;
      case 11:
        effectRainbow(1.3, 3.14 / 4, hueBase, 255, 255);
        break;
    }
    printBuffer(BACKGROUND);
  }
}


void blackBefore(int endindex, CRGB bgcolor, enum PRINT_TYPE_t type)
{
  if (type & BACKGROUND) {
    fill_solid(leds, endindex, bgcolor);
  }
  FastLED.show();
}

void printBuffer(enum PRINT_TYPE_t type)
{
  // Print first digit, exit if null found
  if ( digit(charbuf[0], 6 * LEDS_PER_DIGIT + 2 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print second digit
  if ( digit(charbuf[1], 5 * LEDS_PER_DIGIT + 2 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print third digit
  if ( digit(charbuf[2], 4 * LEDS_PER_DIGIT + 2 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print the first colon
  if ( twodots(charbuf[3], 4 * LEDS_PER_DIGIT + 1 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print fourth digit
  if ( digit(charbuf[4], 3 * LEDS_PER_DIGIT + 1 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print fifth digit
  if ( digit(charbuf[5], 2 * LEDS_PER_DIGIT + 1 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print the second colon
  if ( twodots(charbuf[6], 2 * LEDS_PER_DIGIT + 0 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print sixth digit
  if ( digit(charbuf[7], 1 * LEDS_PER_DIGIT + 0 * LEDS_PER_TWODOTS, color, bgcolor, type) ) return;

  // Print seventh digit
  if ( digit(charbuf[8], 0, color, bgcolor, type) ) return;

  FastLED.show();
}


bool twodots(char character, int shift, CRGB color, CRGB bgcolor, enum PRINT_TYPE_t type)
{
  if (character == 0) {
    blackBefore(shift + LEDS_PER_TWODOTS, bgcolor, type);
    return true;
  }

  switch (character) {
    case ':':
      if ( type & FOREGROUND ) fill_solid(&leds[shift + 0 * LEDS_PER_DOT], 2 * LEDS_PER_DOT, color);
      break;
    case '.':
      if ( type & BACKGROUND ) fill_solid(&leds[shift + 0 * LEDS_PER_DOT], 1 * LEDS_PER_DOT, bgcolor);
      if ( type & FOREGROUND ) fill_solid(&leds[shift + 1 * LEDS_PER_DOT], 1 * LEDS_PER_DOT, color);
      break;
    case '*':
      if ( type & FOREGROUND ) fill_solid(&leds[shift + 0 * LEDS_PER_DOT], 1 * LEDS_PER_DOT, color);
      if ( type & BACKGROUND ) fill_solid(&leds[shift + 1 * LEDS_PER_DOT], 1 * LEDS_PER_DOT, bgcolor);
      break;
    case ' ':
    default:
      if ( type & BACKGROUND ) fill_solid(&leds[shift + 0 * LEDS_PER_DOT], 2 * LEDS_PER_DOT, bgcolor);
      break;
  }

  return false;
}

// Returns true if end of buffer reached
bool digit(char character, int shift, CRGB color, CRGB bgcolor, enum PRINT_TYPE_t type)
{
  if (character == 0) {
    // End of string, blank this digit and later
    blackBefore(shift + LEDS_PER_DIGIT, bgcolor, type);
    return true;
  }

  byte typeface = 0;
  character = toupper(character);
  for (int i = 0; i < DIGIT_CHARSET_SIZE; i++) {
    if (pgm_read_byte(&digit_charset[i]) == character) {
      typeface = pgm_read_byte(&digit_typeface[i]);
      break;
    }
  }

  for (int i = 0; i < 7; i++) {
    if (typeface & 0x01) {
      if ( type & FOREGROUND ) {
        fill_solid(&leds[shift + i * LEDS_PER_SEGMENT], LEDS_PER_SEGMENT, color);
      }
    } else {
      if ( type & BACKGROUND ) {
        fill_solid(&leds[shift + i * LEDS_PER_SEGMENT], LEDS_PER_SEGMENT, bgcolor);
      }
    }
    typeface >>= 1;
  }

  return false;
}


byte matrixMapY(byte index)
{
  // Y mapping for a single digit followed by a colon
  static const PROGMEM byte digitMapY[] = {5, 6, 7, 8, 8, 8, 7, 6, 5, 3, 2, 1, 0, 0, 0, 1, 2, 3, 4, 4, 4, 2, 6};

  byte offset = 0;
  if (index >= 130) offset = 130;
  else if (index >= 109) offset = 109;
  else if (index >= 88) offset = 88;
  else if (index >= 65) offset = 65;
  else if (index >= 44) offset = 44;
  else if (index >= 21) offset = 21;

  return pgm_read_byte(&digitMapY[index - offset]);
}


byte matrixMapX(byte index)
{

  // X mapping for a single digit followed by a colon
  static const PROGMEM char digitMapX[] = {4, 4, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 4, 4, 3, 2, 1, -1, -1};

  byte offset = 0;
  byte x_offset = 32;

  if (index >= 130) {
    offset = 130;
    x_offset = 0;
  } else if (index >= 109) {
    offset = 109;
    x_offset = 5;
  } else if (index >= 88) {
    offset = 88;
    x_offset = 10;
  } else if (index >= 65) {
    offset = 65;
    x_offset = 16;
  } else if (index >= 44) {
    offset = 44;
    x_offset = 21;
  } else if (index >= 21) {
    offset = 21;
    x_offset = 27;
  }

  return pgm_read_byte(&digitMapX[index - offset]) + x_offset;
}

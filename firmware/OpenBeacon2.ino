// Hardware Requirements
// ---------------------
// This firmware must be run on an Arduino Zero capable microcontroller
//
// Required Libraries
// ------------------
// Flash Storage (https://github.com/cmaglie/FlashStorage)
// TinyGPS++ (http://arduiniana.org/libraries/tinygpsplus/)
// Etherkit SSD1306 (https://github.com/etherkit/SSD1306-Arduino)
// Etherkit Si5351 (Library Manager)
// Etherkit JTEncode (Library Manager)
// Scheduler (Library Manager)
// ArduinoJson (Library Manager)
// SPI (Arduino Standard Library)
// Wire (Arduino Standard Library)

#include <ArduinoJson.h>
#include <FlashStorage.h>
#include <Scheduler.h>
#include <JTEncode.h>
#include <TinyGPS++.h>
#include <SPI.h>
#include <Wire.h>
#include <SSD1306.h>
#include <si5351.h>
#include <stdint.h>
#include <string.h>

#include "morsechar.h"
#include "modes.h"
#include "font.h"
#include "bands.h"

// Class instantiation
SSD1306 ssd1306;
Si5351 si5351;
TinyGPSPlus gps;
JTEncode jtencode;

// Hardware defines
#define GPS_ANT                  2
#define ENC_A                    7
#define ENC_B                    3
#define ENC_BTN                  5
#define BTN_S1                   8
#define BTN_S2                   9
#define BTN_S3                   4
#define REFCLK                   10
#define TX_KEY                   13
#define TXI                      A0
#define VCC                      A1

// Default defines
#define DEFAULT_DFCW_OFFSET       5
#define DEFAULT_WPM               35000
#define DEFAULT_MSG_DELAY         10            // in minutes
#define DEFAULT_MODE              MODE_WSPR
#define DEFAULT_BAND              3             // 30 meters
#define DEFAULT_CALLSIGN          "NT7S"
#define DEFAULT_GRID              "CN85"
#define DEFAULT_MSG_1             "MSG1"
#define DEFAULT_MSG_2             "MSG2"
#define DEFAULT_MSG_3             "MSG3"
#define DEFAULT_MSG_4             "MSG4"
#define DEFAULT_SI5351_INT_CORR   0
#define DEFAULT_SI5351_EXT_CORR   0
#define DEFAULT_EXT_GPS_ANT       false
#define DEFAULT_EXT_PLL_REF       false
#define DEFAULT_EXT_PLL_REF_FREQ  25000000UL
#define DEFAULT_DBM               27

// Mode defines
#define MULT_DAH                 3           // DAH is 3x a DIT
#define MULT_WORDDELAY           7           // Space between words is 7 dits
#define MULT_HELL_WORDDELAY      200
#define MULT_HELL_CHARDELAY      20
#define MULT_HELL_GLYPHDELAY     60
#define HELL_ROW_RPT             3
//#define WSPR_OFFSET              146

// Other defines
#define MCP4725A1_BUS_BASE_ADDR  0x62
#define MCP4725A1_VREF           3300UL
#define ANALOG_REF               3300UL
#define PA_BIAS_FULL             2750UL
#define PA_MAX_CURRENT           200
#define GPS_DEBUG                0


// Enumerations
enum BUFFER {BUFFER_0, BUFFER_1, BUFFER_2, BUFFER_3, BUFFER_4};
enum STATE {STATE_IDLE, STATE_DIT, STATE_DAH, STATE_DITDELAY, STATE_DAHDELAY, STATE_WORDDELAY, 
  STATE_MSGDELAY, STATE_EOMDELAY, STATE_CHARDELAY, STATE_HELLCOL, STATE_HELLROW, STATE_CAL, STATE_FSK, 
  STATE_WSPR_INIT, STATE_PREAMBLE, STATE_HELLIDLE};
enum MENU {MENU_MAIN, MENU_BAND, MENU_MODE};

// Structs
typedef struct
{
  boolean valid;
  uint8_t version_major;
  uint8_t version_minor;
  enum MODE mode;
  uint8_t band;
  uint16_t wpm;
  uint8_t msg_delay;
  uint8_t dfcw_offset;
  enum BUFFER buffer;
  char callsign[20];
  char grid[10];
  char msg_mem_1[MSG_BUFFER_SIZE];
  char msg_mem_2[MSG_BUFFER_SIZE];
  char msg_mem_3[MSG_BUFFER_SIZE];
  char msg_mem_4[MSG_BUFFER_SIZE];
  int32_t si5351_int_corr;
  int32_t si5351_ext_corr;
  boolean ext_gps_ant;
  boolean ext_pll_ref;
  uint32_t ext_pll_ref_freq;
  uint8_t dbm;
} Config;

// Instantiate flash storage
FlashStorage(flash_store, Config);
Config flash_config;

// Interrupt service routine variables
volatile boolean A_set = true;
volatile boolean B_set = false;
//volatile unsigned long frequency = 10000000UL; // Startup frequency
//volatile unsigned long frequency = 7040057UL; // 40m WSPR
//volatile unsigned long frequency = 14097057UL; // 20m WSPR
//volatile unsigned long frequency = 21096073UL; // 20m WSPR
//volatile unsigned long frequency = 14078600UL; // 20m JT9
//volatile unsigned long frequency = 14077500UL; // 20m JT65
//volatile unsigned long frequency = 14077500UL; // 20m JT4
//volatile unsigned long frequency = 50294444UL; // Startup frequency
//volatile unsigned long frequency = 10139973UL; // 30m QRSS
volatile unsigned long frequency = 10140123UL; // 30m WSPR
volatile unsigned long last_reported_pos;   // change management
volatile static boolean rotating = false;    // debounce management
volatile uint32_t timer, cur_timer;
volatile boolean toggle = false;

// Global variables
int32_t corr; // this is the correction factor for the Si5351, use calibration sketch to find value.
unsigned int tune_step = 0; // This is in 10^tune_step Hz
uint8_t tx_current;
uint16_t supply_voltage;
uint32_t dit_length;
enum MODE cur_mode;
enum STATE cur_state, next_state;
uint32_t cur_state_end, msg_delay_end;
char msg_buffer[220];
char display_buffer[22];
//char symbol_buffer[220];
char * cur_msg_p;
char * cur_glyph_p;
uint8_t cur_character = '\0';
uint8_t cur_hell_char = '\0';
uint8_t cur_hell_col = 0;
uint8_t cur_hell_row = 0;
uint16_t wpm;
uint8_t msg_delay;
enum BUFFER cur_buffer;
uint8_t dfcw_offset;
boolean tx;
boolean pa_enable;
boolean tx_lock = false;
boolean tx_enable = false;
boolean ext_gps_ant = false;
boolean ext_pll_ref = false;
char callsign[20];
char grid[10];
uint8_t dbm;
enum MENU cur_menu;
uint8_t cur_band;

// EEPROM variables
/*
uint8_t ee_osc;
uint8_t	ee_mode = DEFAULT_MODE;
uint16_t ee_wpm = DEFAULT_WPM;
uint8_t ee_msg_delay = DEFAULT_MSG_DELAY;
uint8_t ee_dfcw_offset = DFCW_DEFAULT_OFFSET;
uint8_t ee_msg_mem_1[MSG_BUFFER_SIZE] = "NT7S";
uint8_t ee_msg_mem_2[MSG_BUFFER_SIZE] = "MSG2";
uint8_t ee_msg_mem_3[MSG_BUFFER_SIZE] = "MSG3";
uint8_t ee_msg_mem_4[MSG_BUFFER_SIZE] = "MSG4";
uint8_t ee_wspr_symbols[WSPR_BUFFER_SIZE] = "";
uint8_t ee_buffer = BUFFER_1;
uint8_t ee_glyph_1[GLYPH_SIZE] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x80};
uint8_t ee_glyph_2[GLYPH_SIZE] = {0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x80};
uint8_t ee_glyph_3[GLYPH_SIZE] = "";
uint8_t ee_glyph_4[GLYPH_SIZE] = "";
*/

// Interrupt on A changing state
void do_encoder_A(void)
{
  // debounce
  if(rotating) 
  {
    //delay(1);  // wait a little until the bouncing is done
  }

  // Test transition, did things really change?
  if(digitalRead(ENC_A) != A_set)
  { // debounce once more
    A_set = !A_set;
    // adjust counter + if A leads B
    if(A_set && !B_set)
    {
      switch(cur_menu)
      {
      case MENU_MAIN:
        if(!tx_lock)
        {
          frequency += power_10(tune_step);
        }
        break;
      case MENU_BAND:
        cur_band++;
        if(cur_band >= BAND_COUNT)
        {
          cur_band = 0;
        }
        // Tune to new band
        frequency = band_table[cur_band].wspr;
        break;
      case MENU_MODE:
        cur_mode = (enum MODE)(cur_mode + 1);
        if(cur_mode >= MODE_COUNT)
        {
          cur_mode = (enum MODE)0;
        }
        break;
      }
      rotating = false;  // no more debouncing until loop() hits again
    }
  }
}

// Interrupt on B changing state, same as A above
void do_encoder_B(void)
{
  if(rotating)
  {
    //delay(1);
  }

  if(digitalRead(ENC_B) != B_set)
  {
    B_set = !B_set;
    //  adjust counter - 1 if B leads A
    if(B_set && !A_set)
    {
      switch(cur_menu)
      {
      case MENU_MAIN:
        if(!tx_lock)
        {
          frequency -= power_10(tune_step);
        }
        break;
      case MENU_BAND:
        if(cur_band == 0)
        {
          cur_band = BAND_COUNT - 1;
        }
        else
        {
          cur_band--;
        }
        // Tune to new band
        frequency = band_table[cur_band].wspr;
        break;
      case MENU_MODE:
        if(cur_mode == 0)
        {
          cur_mode = (enum MODE)(MODE_COUNT - 1);
          
        }
        else
        {
          cur_mode = (enum MODE)(cur_mode - 1);
        }
        break;
      }
      rotating = false;
    }
  }
}

// TODO: probably delete
void update_timer(void)
{
  // Latch the current time
  // MUST disable interrupts during this read or there will be an occasional corruption of cur_timer
  noInterrupts();
  cur_timer = millis();
  interrupts();
  yield();
}

unsigned long power_10(unsigned long exponent)
{
  // bounds checking pls
  if(exponent == 0)
  {
    return 1;
  }
  else
  {
    return 10 * power_10(exponent - 1);
  }
}

void print_freq(uint8_t y, unsigned long freq)
{
  // We will print out the frequency as a fixed length string and pad if less than 100s of MHz
  char temp_str[6];
  int zero_pad = 0;
  uint8_t underline;
  
  // MHz
  yield();
  if(freq / 1000000UL > 0)
  {
    sprintf(temp_str, "%3lu", freq / 1000000UL);
    zero_pad = 1;
  }
  else
  {
    sprintf(temp_str, "   ");
  }
  ssd1306.text(0, y, TEXT_FONT_11X21, false, temp_str);
  freq %= 1000000UL;
  
  // kHz
  yield();
  if(zero_pad == 1)
  {
    sprintf(temp_str, "%03lu", freq / 1000UL);
  }
  else if(freq / 1000UL > 0)
  {
    sprintf(temp_str, "%3lu", freq / 1000UL);
    zero_pad = 1;
  }
  else
  {
    sprintf(temp_str, "   ");
  }
  ssd1306.text(41, y, TEXT_FONT_11X21, false, temp_str);
  freq %= 1000UL;
  
  // Hz
  yield();
  if(zero_pad == 1)
  {
    sprintf(temp_str, "%03lu", freq);
  }
  else
  {
    sprintf(temp_str, "%3lu", freq);
  }
  ssd1306.text(83, y, TEXT_FONT_11X21, false, temp_str);
  
  // Indicate step size
  yield();
  switch(tune_step)
  {
  case 5:
    underline = 41;
    break;
  case 4:
    underline = 53;
    break;
  case 3:
    underline = 65;
    break;
  case 2:
    underline = 83;
    break;
  case 1:
    underline = 95;
    break;
  case 0:
    underline = 107;
    break;
  }

  yield();
  ssd1306.hline(y + 21, underline, underline + 11, PIXEL_WHITE);
  ssd1306.hline(y + 22, underline, underline + 11, PIXEL_WHITE);
  
  // Underline it all
  ssd1306.hline(y + 24, 0, 127, PIXEL_WHITE);
}

// Voltage specified in millivolts
void set_pa_bias(uint16_t voltage)
{
  uint32_t reg;
  uint8_t reg1, reg2;
  
  // Bounds checking
  if(voltage > MCP4725A1_VREF)
  {
    voltage = MCP4725A1_VREF;
  }
  
  // Convert millivolts to the correct register value
  reg = ((uint32_t)voltage * 4096UL) / MCP4725A1_VREF;
  reg1 = (uint8_t)((reg >> 8) & 0xFF);
  reg2 = (uint8_t)(reg & 0xFF); 
  
  // Write the register to the MCP4725A1
  Wire.beginTransmission(MCP4725A1_BUS_BASE_ADDR);
  Wire.write(reg1);
  Wire.write(reg2);
  Wire.endTransmission();
}

// Return value in milliamps
uint8_t get_tx_current(void)
{
  uint32_t txi_adc;
  
  // Get raw ADC reading from the current shunt
  txi_adc = analogRead(TXI);
  
  // Convert 10-bit ADC reading to milliamps
  //return (txi_adc * ANALOG_REF / 1024UL) / 10UL; // 10-bit ADC, shunt res. is 0.5 ohm, amp is x20

  // Convert 12-bit ADC reading to milliamps
  return (txi_adc * ANALOG_REF / 4096UL) / 10UL; // 10-bit ADC, shunt res. is 0.5 ohm, amp is x20
}

// Return value in millivolts
uint16_t get_supply_voltage(void)
{
  uint32_t vcc_adc;
  
  // Get raw VCC reading (through voltage divider of 22k/4.7k)
  vcc_adc = analogRead(VCC);
  
  // Convert 10-bit ADC reading to tenths of volts
  // Voltage divider is 4.7k and 22k
  //return ((vcc_adc * ANALOG_REF / 1024UL) * 267UL) / 47UL;

  // Convert 12-bit ADC reading to tenths of volts
  // Voltage divider is 4.7k and 22k
  return ((vcc_adc * ANALOG_REF / 4096UL) * 267UL) / 47UL;
}

void set_wpm(uint32_t new_wpm)
{
  // This is WPM * 1000 due to need for fractional WPM for slow modes
  //
  // Dit length in milliseconds is 1200 ms / WPM
  dit_length = (1200000UL / new_wpm);
}

uint32_t get_msg_delay(uint8_t delay_minutes)
{
  // The single function parameter is the message delay time in minutes
  if(delay_minutes > MAX_MSG_DELAY)
  {
    delay_minutes = MAX_MSG_DELAY;
  }

  // Number of clock ticks is the number of minutes * 60000 ticks/per min
  return delay_minutes * 60000UL;
}

void init_tx(void)
{
  if(tx_enable)
  {
    tx_reset();

    if(cur_mode == MODE_WSPR || cur_mode == MODE_JT65 || cur_mode == MODE_JT9 || cur_mode == MODE_JT4)
    {
      cur_state_end = cur_timer;
      cur_state = STATE_FSK;
    }
  }
}

void tx_reset(void)
{
  if(cur_mode == MODE_WSPR || cur_mode == MODE_JT65 || cur_mode == MODE_JT9 || cur_mode == MODE_JT4)
  {
    char cur_grid[8];
    
    // Get the current grid square, otherwise use default
    if(gps.location.isValid())
    {
      get_grid_square(gps.location.lat(), gps.location.lng(), cur_grid);
    }
    else
    {
      strcpy(cur_grid, flash_config.grid);
    }

    // Truncate grid to 4 characters
    char grid_4[5];
    memset(grid_4, 0, 5);
    strncpy(grid_4, cur_grid, 4);

    // Build JT message
    // TODO: allow JT modes to use first 13 chars of any message buffer
    char jt_message[14];
    memset(jt_message, 0, 14);
    sprintf(jt_message, "%s %s HI", callsign, grid_4);
    
    // Reset the WSPR symbol buffer
    //jtencode.wspr_encode(callsign, grid_4, flash_config.dbm, (uint8_t *)msg_buffer);
    switch(cur_mode)
    {
      case MODE_WSPR:
        jtencode.wspr_encode(callsign, grid_4, flash_config.dbm, (uint8_t *)msg_buffer);
        break;
      case MODE_JT65:
        jtencode.jt65_encode(jt_message, (uint8_t *)msg_buffer);
        break;
      case MODE_JT9:
        jtencode.jt9_encode(jt_message, (uint8_t *)msg_buffer);
        break;
      case MODE_JT4:
        jtencode.jt4_encode(jt_message, (uint8_t *)msg_buffer);
        break;
      default:
        break;
    }
    
    // Convert symbols from uints to chars representing those values
    /*
    uint8_t i;
    for(i = 0; i < WSPR_BUFFER_SIZE - 1; i++)
    {
      // TODO: need to make this safer
      *(msg_buffer + i) = *(symbol_buffer + i) + '0';
    }
    */
    // Append 0xFF to indicate EOM
    switch(cur_mode)
    {
      case MODE_WSPR:
        msg_buffer[WSPR_SYMBOL_COUNT] = 0xFF;
        break;
      case MODE_JT65:
        msg_buffer[JT65_SYMBOL_COUNT] = 0xFF;
        break;
      case MODE_JT9:
        msg_buffer[JT9_SYMBOL_COUNT] = 0xFF;
        break;
      case MODE_JT4:
        msg_buffer[JT4_SYMBOL_COUNT] = 0xFF;
        break;
      default:
        break;
    }
    
    cur_msg_p = msg_buffer;
    cur_character = '\0';

    // Build the display buffer
    memset(display_buffer, 0, 22);
    if(cur_mode == MODE_WSPR)
    {
      sprintf(display_buffer, "%s %s %d", flash_config.callsign, grid_4, flash_config.dbm);
    }
    else
    {
      strcpy(display_buffer, jt_message);
    }

    // Reset to IDLE state
    cur_state_end = 0xFFFFFFFFFFFFFFFF;
    cur_state = STATE_IDLE;
  }
  else
  {
    // Reset the message buffer
    switch(cur_buffer)
    {
    case BUFFER_0:
    default:
      strcpy(msg_buffer, flash_config.callsign);
      break;
    case BUFFER_1:
      strcpy(msg_buffer, flash_config.msg_mem_1);
      break;
    case BUFFER_2:
      strcpy(msg_buffer, flash_config.msg_mem_2);
      break;
    case BUFFER_3:
      strcpy(msg_buffer, flash_config.msg_mem_3);
      break;
    case BUFFER_4:
      strcpy(msg_buffer, flash_config.msg_mem_4);
      break;
    }
    
    cur_msg_p = msg_buffer;
    cur_character = '\0';

    // If in message delay mode, set the delay
    if(msg_delay > 0)
    {
      msg_delay_end = cur_timer + get_msg_delay(msg_delay);
    }

    // Reset Hell index
    cur_hell_row = 0;
    cur_hell_col = 0;

    // Reset WPM
    if(cur_mode == MODE_CW)
    {
      wpm = flash_config.wpm;
    }
    else
    {
      wpm = dit_speed[cur_mode];
    }
    set_wpm(wpm);

    if(cur_mode == MODE_DFCW3 || cur_mode == MODE_DFCW6 || cur_mode == MODE_DFCW10 || cur_mode == MODE_DFCW120)
    {
      cur_state_end = cur_timer + (dit_length * MULT_WORDDELAY);
      cur_state = STATE_PREAMBLE;
    }
    else
    {
      // Reset to IDLE state
      cur_state_end = cur_timer;
      cur_state = STATE_IDLE;
    }
  }
}

void get_grid_square(double lat, double lng, char* result)
{
  uint8_t i = 0;

  // First digit
  // TODO: bounds check
  uint8_t lng_digit = (((int16_t)lng + 180U) / 20U) + 1;
  result[i++] = (char)lng_digit + 'A' - 1;

  // Second digit
  uint8_t lat_digit = (((int16_t)lat + 90U ) / 10U) + 1;
  result[i++] = (char)lat_digit + 'A' - 1;

  // Third digit
  double lng_remainder = (lng + 180.0) - ((lng_digit - 1) * 20);
  lng_digit = (uint8_t)(lng_remainder / 2);
  result[i++] = (char)lng_digit + '0';

  // Fourth digit
  double lat_remainder = (lat + 90.0) - ((lat_digit - 1) * 10);
  lat_digit = (uint8_t)(lat_remainder);
  result[i++] = (char)lat_digit + '0';

  // Fifth digit
  lng_remainder = lng_remainder - (lng_digit * 2);
  lng_digit = (uint8_t)((lng_remainder * 12.0) + 1.0);
  result[i++] = (char)lng_digit + 'a' - 1;

  // Sixth digit
  lat_remainder = lat_remainder - lat_digit;
  lat_digit = (uint8_t)((lat_remainder * 24.0) + 1.0);
  result[i++] = (char)lat_digit + 'a' - 1;
  result[i] = '\0';
}

void update_display(void)
{
  char text[30];
  
  yield();
  ssd1306.buffer_clear();

  // GPS status
  if(ext_gps_ant)
  {
    sprintf(text, "E");
  }
  else
  {
    sprintf(text, "I");
  }
  //sprintf(text, "%lu", cur_timer);
  yield();
  ssd1306.text(0, 0, TEXT_FONT_5X8, false, text);
  yield();
  sprintf(text, "%u", gps.satellites.value());
  ssd1306.text(6, 0, TEXT_FONT_5X8, false, text);

  // Time
  if(gps.time.isValid())
  {
    sprintf(text, "%02u:%02u:%02u", gps.time.hour(), gps.time.minute(), gps.time.second());
  }
  else
  {
    sprintf(text, "--:--:--");
  }
  yield();
  ssd1306.text(26, 0, TEXT_FONT_5X8, false, text);

  // Mode
  yield();
  ssd1306.text(82, 0, TEXT_FONT_5X8, false, mode_list[cur_mode]);
  if(cur_mode == MODE_CW)
  {
    sprintf(text, "%2d", wpm / 1000);
    ssd1306.text(108, 0, TEXT_FONT_5X8, false, text);
  }
  
  // debugging info
  /*
  sprintf(text, "%X", cur_character);
  ssd1306.text(0, 45, TEXT_FONT_5X8, text);
  sprintf(text, "%c", *cur_msg_p);
  ssd1306.text(20, 45, TEXT_FONT_5X8, text);
  sprintf(text, "%d", cur_state);
  ssd1306.text(40, 45, TEXT_FONT_5X8, text);

  if(gps.location.isValid())
  {
    get_grid_square(gps.location.lat(), gps.location.lng(), text);
    yield();
    ssd1306.text(55, 45, TEXT_FONT_5X8, text);
  }
  */

  // Frequency
  yield();
  print_freq(9, frequency);

  // TX lock indicator
  if(tx_lock)
  {
    yield();
    ssd1306.text(123, 10, TEXT_FONT_5X8, false, "T");
    ssd1306.text(123, 18, TEXT_FONT_5X8, false, "X");
  }

  uint8_t indicator_pos;
  char temp_buffer[22];

  switch(cur_menu)
  {
  case MENU_MAIN:
    // PA current and supply voltage
    yield();
    //tx_current = get_tx_current();
    sprintf(text, "PA:%u mA", tx_current);
    //yield();
    ssd1306.text(0, 35, TEXT_FONT_5X8, false, text);
    yield();
    //supply_voltage = get_supply_voltage();
    sprintf(text, "VDD:%u.%u V", supply_voltage / 1000, (supply_voltage % 1000) / 100);
    yield();
    ssd1306.text(64, 35, TEXT_FONT_5X8, false, text);
  
    // Message buffer
    // Can show 19 chars at a time.
    //char temp_buffer[22];
    memset(temp_buffer, 0, 22);
    //uint8_t indicator_pos = cur_msg_p - msg_buffer;
    indicator_pos = cur_msg_p - msg_buffer;
    yield();
  
    if(cur_mode == MODE_WSPR || cur_mode == MODE_JT65 || cur_mode == MODE_JT9 || cur_mode == MODE_JT4)
    {
      // probably need to check for updates of grid while idling
      if(strcmp(display_buffer, "") == 0)
      {
        char cur_grid[8];
        
        // Get the current grid square, otherwise use default
        if(gps.location.isValid())
        {
          get_grid_square(gps.location.lat(), gps.location.lng(), cur_grid);
        }
        else
        {
          strcpy(cur_grid, flash_config.grid);
        }
    
        // Truncate grid to 4 characters
        char grid_4[5];
        memset(grid_4, 0, 5);
        strncpy(grid_4, cur_grid, 4);
        
        sprintf(display_buffer, "%s %s %d", flash_config.callsign, grid_4, flash_config.dbm);
      }
  
      yield();
      strcpy(text, display_buffer);
      ssd1306.text(0, 44, TEXT_FONT_5X8, false, text);
    }
    else
    {
      // TODO: add indicator for message delay
      if(strlen(msg_buffer) > 19)
      {
        if(indicator_pos > 9)
        {
          if((strlen(msg_buffer) - indicator_pos) < 10)
          {
            yield();
            strcpy(temp_buffer, "\xAE");
            strncat(temp_buffer, msg_buffer + (strlen(msg_buffer) - 17), 18);
            indicator_pos -= (strlen(msg_buffer) - 18);
          }
          else
          {
            yield();
            strcpy(temp_buffer, "\xAE");
            strncat(temp_buffer, msg_buffer + indicator_pos - 8, 17);
            strcat(temp_buffer, "\xAF");
            indicator_pos = 9;
          }
        }
        else
        {
          yield();
          strncpy(temp_buffer, msg_buffer, 18);
          strcat(temp_buffer, "\xAF");
        }
      }
      else
      {
        strncpy(temp_buffer, msg_buffer, 19);
      }
      yield();
      sprintf(text, "%1d:%s", cur_buffer, temp_buffer);
      ssd1306.text(0, 44, TEXT_FONT_5X8, false, text);
    
      // Current character indicator
      yield();
      ssd1306.hline(52, (indicator_pos + 2) * 6, ((indicator_pos + 2) * 6) + 4, PIXEL_WHITE);
    }
    break;
  case MENU_BAND:
    sprintf(text, "%s", band_table[cur_band].name);
    ssd1306.text(50, 40, TEXT_FONT_5X8, false, text);
    break;
  case MENU_MODE:
    sprintf(text, "%s", mode_list[cur_mode]);
    ssd1306.text(50, 40, TEXT_FONT_5X8, false, text);
    break;
  }
  
  // Menu system
  yield();
  ssd1306.hline(53, 0, 127, PIXEL_WHITE);
  yield();
  ssd1306.vline(40, 54, 63, PIXEL_WHITE);
  yield();
  ssd1306.vline(81, 54, 63, PIXEL_WHITE);

  // S1
  yield();
  switch(cur_menu)
  {
  case MENU_MAIN:
    sprintf(text, "Menu");
    break;
  case MENU_BAND:
  case MENU_MODE:
    sprintf(text, "Band");
    break;
  }
  ssd1306.text(7, 55, TEXT_FONT_5X8, false, text);

  // S2
  yield();
  switch(cur_menu)
  {
  case MENU_MAIN:
    if(cur_state == STATE_IDLE)
    {
      sprintf(text, "Start");
      ssd1306.text(46, 55, TEXT_FONT_5X8, false, text);
    }
    else
    {
      sprintf(text, "Stop");
      ssd1306.text(49, 55, TEXT_FONT_5X8, false, text);
    }
    break;
  case MENU_BAND:
  case MENU_MODE:
    sprintf(text, "Mode");
    ssd1306.text(49, 55, TEXT_FONT_5X8, false, text);
    break;
  }

  // S3
  yield();
  switch(cur_menu)
  {
  case MENU_MAIN:
    if(tx_enable)
    {
      sprintf(text, "TX Dis");
      ssd1306.text(86, 55, TEXT_FONT_5X8, false, text);
    }
    else
    {
      sprintf(text, "TX Enb");
      ssd1306.text(86, 55, TEXT_FONT_5X8, false, text);
    }
    break;
  case MENU_BAND:
  case MENU_MODE:
    sprintf(text, "Exit");
    ssd1306.text(86, 55, TEXT_FONT_5X8, false, text);
    break;
  }
  
  // Display update
  yield();
  ssd1306.buffer_write();
}

void process_inputs(void)
{
 // Reset the debouncer
  rotating = true;
  
  // Update frequency based on encoder
  if(last_reported_pos != frequency)
  {
    last_reported_pos = frequency;
    
    si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
  }

  yield();
  // Handle encoder button
  if(digitalRead(ENC_BTN) == LOW)
  {
    delay(50);   // delay to debounce
    if(digitalRead(ENC_BTN) == LOW)
    {
      uint8_t i;
      for(i = 0; i < 10; i++)
      {
        //yield();
        if(digitalRead(ENC_BTN) == HIGH)
        {
          // short press
          if(cur_menu == MENU_MAIN)
          {
            if(tune_step < 5)
            {
              tune_step++;
            }
            else
            {
              tune_step = 0;
            }
            delay(50);
            break;
          }
          else
          {
            cur_menu = MENU_MAIN;
            tx_reset();
            delay(50);
            break;
          }
        }
        delay(50);
      }
      if(digitalRead(ENC_BTN) == LOW)
      {
        // long press
        cur_menu = MENU_BAND;
        tx_enable = false;
        
        while(digitalRead(ENC_BTN) == LOW)
        {
          delay(50);
        }
      }
      
      delay(50); //delay to avoid many steps at one
    }
  }

  yield();
  // Handle S1 button
  if(digitalRead(BTN_S1) == LOW)
  {
    delay(50);   // delay to debounce
    if (digitalRead(BTN_S1) == LOW)
    {
      switch(cur_menu)
      {
      case MENU_MAIN:
        init_tx();
        break;
      case MENU_BAND:
      case MENU_MODE:
        cur_menu = MENU_BAND;
        break;
      }
      
      delay(50); //delay to avoid many steps at one
    }
  }

  yield();
  // Handle S2 button
  if(digitalRead(BTN_S2) == LOW)
  {
    delay(50);   // delay to debounce
    if (digitalRead(BTN_S2) == LOW)
    {
      switch(cur_menu)
      {
      case MENU_MAIN:
        if(cur_state == STATE_IDLE)
        {
          tx_enable = true;
          init_tx();
        }
        else
        {
          tx_enable = false;
          cur_state = STATE_IDLE;
          cur_state_end = 0xFFFFFFFFFFFFFFFF;
        }
        break;
      case MENU_BAND:
      case MENU_MODE:
        cur_menu = MENU_MODE;
        break;
      }
      
      delay(50); //delay to avoid many steps at one
    }
  }

  yield();
  // Handle S3 button
  if(digitalRead(BTN_S3) == LOW)
  {
    delay(50);   // delay to debounce
    if (digitalRead(BTN_S3) == LOW)
    {
      switch(cur_menu)
      {
      case MENU_MAIN:
        if(tx_enable)
        {
          tx_enable = false;
        }
        else
        {
          tx_enable = true;
        }
        break;
      case MENU_BAND:
      case MENU_MODE:
        cur_menu = MENU_MAIN;
        tx_reset();
        break;
      }
      
      delay(50); //delay to avoid many steps at one
    }
  }

  // Feed GPS data to TinyGPSPlus and mirror serial from GPS
  char ser_buffer;

  yield();
  
  while(Serial1.available() > 0)
  {
    ser_buffer = Serial1.read();
    //if(GPS_DEBUG)
    //{
      //SerialUSB.print(ser_buffer);
    //}
    gps.encode(ser_buffer);
  }
}

// TODO: still needed?
void update_gps(void)
{
  // Feed GPS data to TinyGPSPlus and mirror serial from GPS
  char ser_buffer;

  yield();
  
  while(Serial1.available() > 0)
  {
    yield();
    ser_buffer = Serial1.read();
    if(GPS_DEBUG)
    {
      SerialUSB.print(ser_buffer);
    }
    yield();
    gps.encode(ser_buffer);
  }
}

void setup()
{
  Serial1.begin(9600);

  //if(GPS_DEBUG)
  //{
  //  SerialUSB.begin(57600);
  //  while (!SerialUSB); // Wait for serial port to connect.
  //}
  
  // Initialize the display
  ssd1306.init();
  
  // Set GPIO
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(BTN_S1, INPUT_PULLUP);
  pinMode(BTN_S2, INPUT_PULLUP);
  pinMode(BTN_S3, INPUT_PULLUP);
  pinMode(REFCLK, INPUT);
  pinMode(GPS_ANT, OUTPUT);
  pinMode(TX_KEY, OUTPUT);
  REG_PORT_DIRSET0 = PORT_PA27; // TX LED
  REG_PORT_DIRSET1 = PORT_PB03; // RX LED
  
  // Set initial output states
  digitalWrite(TX_KEY, LOW);
  digitalWrite(GPS_ANT, LOW);

  digitalWrite(PIN_LED_TXL, HIGH);
  digitalWrite(PIN_LED_RXL, HIGH);
  
  // Encoder pin A on interrupt 14 (pin 1)
  attachInterrupt(ENC_A, do_encoder_A, CHANGE);
  // Encoder pin B on interrupt 9 (pin 2)
  attachInterrupt(ENC_B, do_encoder_B, CHANGE);
  
  // Setup ADC pins
  pinMode(TXI, INPUT);
  pinMode(VCC, INPUT);

  analogReadResolution(12);

  // Load the flash config
  flash_config = flash_store.read();
  if(flash_config.valid == false)
  {
    flash_config.valid = true;
    flash_config.version_major = 0;
    flash_config.version_minor = 1;
    flash_config.mode = DEFAULT_MODE;
    flash_config.band = DEFAULT_BAND;
    flash_config.wpm = DEFAULT_WPM;
    flash_config.msg_delay = DEFAULT_MSG_DELAY;
    flash_config.dfcw_offset = DEFAULT_DFCW_OFFSET;
    flash_config.buffer = BUFFER_0;
    strcpy(flash_config.callsign, DEFAULT_CALLSIGN);
    strcpy(flash_config.grid, DEFAULT_GRID);
    strcpy(flash_config.msg_mem_1, DEFAULT_MSG_1);
    strcpy(flash_config.msg_mem_2, DEFAULT_MSG_2);
    strcpy(flash_config.msg_mem_3, DEFAULT_MSG_3);
    strcpy(flash_config.msg_mem_4, DEFAULT_MSG_4);
    flash_config.si5351_int_corr = DEFAULT_SI5351_INT_CORR;
    flash_config.si5351_ext_corr = DEFAULT_SI5351_EXT_CORR;
    flash_config.ext_gps_ant = DEFAULT_EXT_GPS_ANT;
    flash_config.ext_pll_ref = DEFAULT_EXT_PLL_REF;
    flash_config.ext_pll_ref_freq = DEFAULT_EXT_PLL_REF_FREQ;
    flash_config.dbm = DEFAULT_DBM;
    flash_store.write(flash_config);
  }

  // Initialize states
  cur_mode = flash_config.mode;
  cur_band = flash_config.band;
  set_wpm(flash_config.wpm);
  msg_delay = flash_config.msg_delay;
  dfcw_offset = flash_config.dfcw_offset;
  cur_buffer = flash_config.buffer;
  corr = flash_config.si5351_int_corr; // account for ext and int
  ext_gps_ant = flash_config.ext_gps_ant;
  ext_pll_ref = flash_config.ext_pll_ref;
  strcpy(callsign, flash_config.callsign);
  dbm = flash_config.dbm;
  cur_state = STATE_IDLE;
  cur_state_end = 0xFFFFFFFFFFFFFFFF;
  msg_delay_end = cur_timer + get_msg_delay(msg_delay);
  cur_menu = MENU_MAIN;
  
  cur_timer = 0;
  tx = 0;

  // Initialize the Si5351A
  // (also initializes the Wire library for other functions
  //si5351.init(SI5351_VARIANT_C, SI5351_CRYSTAL_LOAD_8PF, 0);
  if(ext_pll_ref)
  {
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, flash_config.ext_pll_ref_freq);
    si5351.set_correction(corr);
    si5351.set_pll_input(SI5351_PLLA, SI5351_PLL_INPUT_CLKIN);
  }
  else
  {
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0);
    si5351.set_correction(corr);
    si5351.set_pll_input(SI5351_PLLA, SI5351_PLL_INPUT_XO);
  }
  //si5351.set_correction(corr);
  //si5351.set_pll_input(SI5351_PLLA, SI5351_PLL_INPUT_XO);
  si5351.set_clock_fanout(SI5351_FANOUT_MS, 1);
  si5351.set_clock_source(SI5351_CLK1, SI5351_CLK_SRC_MS0);
  si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
  si5351.drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA);
  si5351.set_clock_disable(SI5351_CLK0, SI5351_CLK_DISABLE_HI_Z);
  //si5351.set_clock_disable(SI5351_CLK1, SI5351_CLK_DISABLE_HI_Z);
  //si5351.output_enable(SI5351_CLK1, 0);
  //si5351.set_clock_pwr(SI5351_CLK1, 0);
  //si5351.set_freq(10000000ULL, 0, SI5351_CLK2);

  
  // Initialize the PA bias off
  set_pa_bias(PA_BIAS_FULL);
  //set_pa_bias(0);

  // Set up the message buffer
  //strcpy(msg_buffer, flash_config.msg_mem_1);
  //cur_msg_p = msg_buffer;

  memset(display_buffer, 0, 22);

  // Start the USB-UART bridge
  SerialUSB.begin(57600);
  
  Scheduler.startLoop(update_timer);
  Scheduler.startLoop(update_display);
  Scheduler.startLoop(process_inputs);
  //Scheduler.startLoop(update_gps);
  //Scheduler.startLoop(uart_handler);
  
  if(cur_mode == MODE_WSPR || cur_mode == MODE_JT65 || cur_mode == MODE_JT9 || cur_mode == MODE_JT4)
  {
    //init_tx();
    //tx_reset();
  }
  else
  {
    //init_tx();
    tx_reset();
  }
}

void loop()
{
  char text[21];
  
  // Latch the current time
  // MUST disable interrupts during this read or there will be an occasional corruption of cur_timer
  noInterrupts();
  cur_timer = millis();
  interrupts();

  // Trigger WSPR
  if((cur_mode == MODE_WSPR || cur_mode == MODE_JT65 || cur_mode == MODE_JT9 || cur_mode == MODE_JT4) 
    && (gps.time.minute() % 6 == 0) && (gps.time.second() == 1))
  {
    init_tx();
  }
  
  if(tx)
  {
    si5351.set_clock_pwr(SI5351_CLK0, 1);
    si5351.set_clock_pwr(SI5351_CLK1, 1);
    digitalWrite(PIN_LED_TXL, LOW);
  }
  else
  {
    si5351.set_clock_pwr(SI5351_CLK0, 0);
    si5351.set_clock_pwr(SI5351_CLK1, 0);
    digitalWrite(PIN_LED_TXL, HIGH);
  }

  if(pa_enable)
  {
    digitalWrite(TX_KEY, HIGH);
  }
  else
  {
    digitalWrite(TX_KEY, LOW);
  }

  // State machine
  switch(cur_mode)
  {
  case MODE_DFCW3:
  case MODE_DFCW6:
  case MODE_DFCW10:
  case MODE_DFCW120:
  case MODE_QRSS3:
  case MODE_QRSS6:
  case MODE_QRSS10:
  case MODE_QRSS120:
  case MODE_CW:
    switch(cur_state)
    {
    case STATE_IDLE:
      // TX off
      if(cur_mode == MODE_CW)
      {
        pa_enable = false;
        tx = false;
      }
      else if(cur_mode == MODE_DFCW3 || cur_mode == MODE_DFCW6 || cur_mode == MODE_DFCW10 ||
        cur_mode == MODE_DFCW120)
      {
        pa_enable = true;
        tx = true;
      }
      else
      {
        pa_enable = true;
        tx = false;
      }

      tx_lock = false;
      
      if(tx_enable)
      {
        if(msg_delay > 0 && msg_delay_end <= cur_timer && cur_msg_p == msg_buffer && 
          (cur_mode == MODE_DFCW3 || cur_mode == MODE_DFCW6 || cur_mode == MODE_DFCW10 ||
          cur_mode == MODE_DFCW120))
        {
          //msg_delay_end = cur_timer + get_msg_delay(msg_delay);
          cur_state_end = cur_timer + (dit_length * MULT_WORDDELAY);
          cur_state = STATE_PREAMBLE;
          break;
        }

        msg_delay_end = cur_timer + get_msg_delay(msg_delay);
  
        // If this is the first time thru the message loop, get the first character
        if((cur_msg_p == msg_buffer) && (cur_character == '\0'))
        {
          cur_character = pgm_read_byte(&morsechar[(*cur_msg_p) - MORSE_CHAR_START]);
        }
  
        // Get the current element in the current character
        if(cur_character != '\0')
        {
          if(cur_character == 0b10000000 || cur_character == 0b11111111)	// End of character marker or SPACE
          {
            // Set next state based on whether EOC or SPACE
            if(cur_character == 0b10000000)
            {
              cur_state_end = cur_timer + (dit_length * MULT_DAH);
              cur_state = STATE_DAHDELAY;
              
              // Grab next character, set state to inter-character delay
              cur_msg_p++;
    
              // If we read a NULL from the announce buffer, set cur_character to NULL,
              // otherwise set to correct morse character
              if((*cur_msg_p) == '\0')
              {
                cur_character = '\0';
              }
              else
              {
                cur_character = pgm_read_byte(&morsechar[(*cur_msg_p) - MORSE_CHAR_START]);
              }
            }
            else
            {
              cur_state_end = cur_timer + (dit_length * MULT_WORDDELAY);
              cur_state = STATE_WORDDELAY;
            }
  
            // Grab next character, set state to inter-character delay
            //cur_msg_p++;
  
            /*
            // If we read a NULL from the announce buffer, set cur_character to NULL,
            // otherwise set to correct morse character
            if((*cur_msg_p) == '\0')
            {
              cur_character = '\0';
            }
            else
            {
              cur_character = pgm_read_byte(&morsechar[(*cur_msg_p) - MORSE_CHAR_START]);
            }
            */
          }
          else
          {
            // Mask off MSb, set cur_element
            if((cur_character & 0b10000000) == 0b10000000)
            {
              cur_state_end = cur_timer + (dit_length * MULT_DAH);
              cur_state = STATE_DAH;
            }
            else
            {
              cur_state_end = cur_timer + dit_length;
              cur_state = STATE_DIT;
            }
  
            // Shift left to get next element
            cur_character = cur_character << 1;
          }
        }
        else
        {
          // Reload the message buffer and set buffer pointer back to beginning
          switch(cur_buffer)
          {
          case BUFFER_0:
          default:
            strcpy(msg_buffer, flash_config.callsign);
            break;
          case BUFFER_1:
            strcpy(msg_buffer, flash_config.msg_mem_1);
            break;
          case BUFFER_2:
            strcpy(msg_buffer, flash_config.msg_mem_2);
            break;
          case BUFFER_3:
            strcpy(msg_buffer, flash_config.msg_mem_3);
            break;
          case BUFFER_4:
            strcpy(msg_buffer, flash_config.msg_mem_4);
            break;
          }
          cur_msg_p = msg_buffer;
          cur_character = '\0';
  
          if(msg_delay == 0)
          {
            // If a constantly repeating message, put a word delay at the end of message
            cur_state_end = cur_timer + (dit_length * MULT_WORDDELAY);
            cur_state = STATE_EOMDELAY;
          }
          else
          {
            // Otherwise, set the message delay time
            if(msg_delay_end < (cur_timer + (dit_length * MULT_WORDDELAY)))
            {
              cur_state_end = cur_timer + (dit_length * MULT_WORDDELAY);
            }
            else
            {
              cur_state_end = msg_delay_end;
            }
            
    	      cur_state = STATE_MSGDELAY;
          }
        }
      }
      break;

    case STATE_PREAMBLE:
      // Wait a word delay with TX on before starting message
      si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
  
      // Transmitter on
      pa_enable = true;
      tx = true;
      tx_lock = true;
      
      // When done waiting, go back to IDLE state to start the message
      if(cur_timer > cur_state_end)
      {
        cur_state = STATE_IDLE;
      }
      break;
  
    case STATE_DIT:
    case STATE_DAH:
      pa_enable = true;
      tx = true;
      tx_lock = true;
      
      switch(cur_mode)
      {
      case MODE_DFCW3:
      case MODE_DFCW6:
      case MODE_DFCW10:
      case MODE_DFCW120:
        // Set FSK to MARK
        si5351.set_freq((frequency + dfcw_offset) * 100ULL , 0, SI5351_CLK0);
        break;
      case MODE_QRSS3:
      case MODE_QRSS6:
      case MODE_QRSS10:
      case MODE_QRSS120:
      case MODE_CW:
        // Set FSK to 0
        si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
        break;
      default:
        break;
      }

      if(cur_timer > cur_state_end)
      {
        switch(cur_mode)
        {
        case MODE_DFCW3:
        case MODE_DFCW6:
        case MODE_DFCW10:
        case MODE_DFCW120:
          // Transmitter on
          pa_enable = true;
          tx = true;

          // Set FSK to 0
          si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
          break;
        case MODE_QRSS3:
        case MODE_QRSS6:
        case MODE_QRSS10:
        case MODE_QRSS120:
          // Transmitter off
          pa_enable = true;
          tx = false;

          // Set FSK to 0
          si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
          break;
        case MODE_CW:
          // Transmitter off
          pa_enable = false;
          tx = false;

          // Set FSK to 0
          si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
          break;
        default:
          break;
        }

        cur_state_end = cur_timer + dit_length;
        cur_state = STATE_DITDELAY;
      }
      break;
    case STATE_DITDELAY:
    case STATE_DAHDELAY:
    case STATE_WORDDELAY:
    case STATE_MSGDELAY:
    case STATE_EOMDELAY:
      tx_lock = true;
      // Set FSK to 0
      si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);

      if(cur_state == STATE_MSGDELAY || cur_mode == MODE_QRSS3 || cur_mode == MODE_QRSS6 || cur_mode == MODE_QRSS10 || cur_mode == MODE_QRSS120 || cur_mode == MODE_CW)
      {
        // Transmitter off
        if(cur_mode == MODE_CW)
        {
          pa_enable = false;
        }
        else
        {
          pa_enable = true;
        }
        tx = false;
      }
      else
      {
        // Transmitter on
        pa_enable = true;
        tx = true;
      }

      if(cur_timer > cur_state_end)
      {
        if(cur_state == STATE_WORDDELAY)
        {
          // Grab next character
          cur_msg_p++;

          // If we read a NULL from the announce buffer, set cur_character to NULL,
          // otherwise set to correct morse character
          if((*cur_msg_p) == '\0')
          {
            cur_character = '\0';
          }
          else
          {
            cur_character = pgm_read_byte(&morsechar[(*cur_msg_p) - MORSE_CHAR_START]);
          }
        }

        cur_state = STATE_IDLE;
      }
      break;

    default:
      break;
    }
    break;

  case MODE_HELL:
    switch(cur_state)
    {
    case STATE_IDLE:
      tx_lock = false;
    
      if(msg_delay > 0 && msg_delay_end <= cur_timer)
      {
        msg_delay_end = cur_timer + get_msg_delay(msg_delay);
      }

      // If this is the first time thru the message loop, get the first character
      if((cur_msg_p == msg_buffer) && (cur_hell_char == '\0'))
      {
        cur_hell_col = 0;
        cur_hell_char = pgm_read_byte(&fontchar[(*cur_msg_p) - FONT_START][cur_hell_col++]);
        cur_state_end = cur_timer + (dit_length);
        cur_state = STATE_HELLCOL;
      }
      else
      {
        cur_hell_char = pgm_read_byte(&fontchar[(*cur_msg_p) - FONT_START][cur_hell_col++]);

        if(cur_hell_col > HELL_COLS)
        {
          // Reset Hell column
          cur_hell_col = 0;

          // Grab next character
          cur_msg_p++;

          if((*cur_msg_p) == '\0')
          {
            // End of message
            // Reload the message buffer and set buffer pointer back to beginning
            switch(cur_buffer)
            {
            case BUFFER_1:
            default:
              strcpy(msg_buffer, flash_config.msg_mem_1);
              break;
            case BUFFER_2:
              strcpy(msg_buffer, flash_config.msg_mem_2);
              break;
            case BUFFER_3:
              strcpy(msg_buffer, flash_config.msg_mem_3);
              break;
            case BUFFER_4:
              strcpy(msg_buffer, flash_config.msg_mem_4);
              break;
            }
            cur_msg_p = msg_buffer;
            cur_hell_char = '\0';

            if(msg_delay == 0)
            {
              // If a constantly repeating message, put a word delay at the end of message
              cur_state_end = cur_timer + (dit_length * MULT_HELL_WORDDELAY);
            }
            else
            {
              // Otherwise, set the message delay time
              if(msg_delay_end < cur_timer + (dit_length * MULT_HELL_WORDDELAY))
              {
                cur_state_end = cur_timer + (dit_length * MULT_HELL_WORDDELAY);
              }
              else
              {
                cur_state_end = msg_delay_end;
              }
            }

            cur_state = STATE_WORDDELAY;
          }
          else
          {
            cur_state_end = cur_timer + (dit_length * MULT_HELL_CHARDELAY);
            cur_state = STATE_CHARDELAY;
          }
        }
        else
        {
          //cur_hell_char = pgm_read_byte(&fontchar[(*cur_msg_p) - FONT_START][cur_hell_col]);
          cur_state_end = cur_timer + (dit_length);
          cur_state = STATE_HELLCOL;
        }
      }
      break;
    case STATE_HELLCOL:
      tx_lock = true;
      //OCR1B = hell_tune[cur_hell_row];
      if((cur_hell_char & (1 << cur_hell_row)) != 0)
      {
        // Pixel on
        pa_enable = true;
        tx = true;
      }
      else
      {
        // Pixel off
        pa_enable = true;
        tx = false;
      }

      if(cur_timer > cur_state_end)
      {
        cur_hell_row++;
        if(cur_hell_row > HELL_ROWS)
        {
          cur_hell_row = 0;
          cur_state = STATE_IDLE;
        }
        else
        {
          cur_state_end = cur_timer + dit_length;
          cur_state = STATE_HELLCOL;
        }
      }
      break;

    case STATE_WORDDELAY:
    case STATE_CHARDELAY:
      tx_lock = true;
      // Set FSK to 0
      si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);

      // Transmitter off
      pa_enable = true;
      tx = false;

      if(cur_timer > cur_state_end)
      cur_state = STATE_IDLE;
      break;
    default:
      cur_state = STATE_IDLE;
      break;
    }
    break;

  case MODE_WSPR:
  case MODE_JT65:
  case MODE_JT9:
  case MODE_JT4:
    switch(cur_state)
    {
    case STATE_IDLE:
      // Transmitter off
      pa_enable = true;
      tx = false;

      tx_lock = false;

      if(cur_timer > cur_state_end)
      {
        switch(cur_mode)
        {
        case MODE_WSPR:
          cur_state_end = cur_timer + WSPR_SYMBOL_LENGTH;
          break;
        case MODE_JT65:
          cur_state_end = cur_timer + JT65_SYMBOL_LENGTH;
          break;
        case MODE_JT9:
          cur_state_end = cur_timer + JT9_SYMBOL_LENGTH;
          break;
        case MODE_JT4:
          cur_state_end = cur_timer + JT4_SYMBOL_LENGTH;
          break;
        default:
          break;
        }
        
        cur_state = STATE_FSK;
      }
      break;

    case STATE_FSK:
      // Transmitter on
      pa_enable = true;
      tx = true;
      tx_lock = true;

      uint16_t tone_spacing;
      switch(cur_mode)
      {
      case MODE_WSPR:
        tone_spacing = WSPR_TONE_SPACING;
        break;
      case MODE_JT65:
        tone_spacing = JT65_TONE_SPACING;
        break;
      case MODE_JT9:
        tone_spacing = JT9_TONE_SPACING;
        break;
      case MODE_JT4:
        tone_spacing = JT4_TONE_SPACING;
        break;
      default:
        break;
      }

      // Transmit the FSK symbol
      si5351.set_freq((frequency * 100ULL) + (tone_spacing * (*(cur_msg_p))), 0, SI5351_CLK0);

      if(cur_timer > cur_state_end)
      {        
        // Get the next symbol from the buffer
        cur_msg_p++;

	      // If at end of buffer, reset and go into message delay
        if((*cur_msg_p) == 0xFF)
        {
          //eeprom_read_block((void*)&msg_buffer, (const void*)&ee_wspr_symbols, WSPR_BUFFER_SIZE - 1);
          cur_msg_p = msg_buffer;

          cur_state_end = 0xFFFFFFFFFFFFFFFF;
          cur_state = STATE_IDLE;
          pa_enable = true;
          tx = false;
          si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
        }
        else
        {
          switch(cur_mode)
          {
          case MODE_WSPR:
            cur_state_end = cur_timer + WSPR_SYMBOL_LENGTH;
            break;
          case MODE_JT65:
            cur_state_end = cur_timer + JT65_SYMBOL_LENGTH;
            break;
          case MODE_JT9:
            cur_state_end = cur_timer + JT9_SYMBOL_LENGTH;
            break;
          case MODE_JT4:
            cur_state_end = cur_timer + JT4_SYMBOL_LENGTH;
            break;
          default:
            break;
          }
          
          cur_state = STATE_FSK;
        }
      }
      break;
    default:
      cur_state = STATE_IDLE;
      break;
    }
    break;
  case MODE_CAL:
    si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
    pa_enable = true;
    tx = true;
    break;
  }

  /*
  // Reset the debouncer
  rotating = true;
  
  // Update frequency based on encoder
  if(last_reported_pos != frequency)
  {
    last_reported_pos = frequency;
    
    si5351.set_freq(frequency * 100ULL, 0, SI5351_CLK0);
  }

  // Handle encoder button
  if(digitalRead(ENC_BTN) == LOW)
  {
    delay(50);   // delay to debounce
    if (digitalRead(ENC_BTN) == LOW)
    {
      if(tune_step < 5)
      {
        tune_step++;
      }
      else
      {
        tune_step = 0;
      }
      
      delay(50); //delay to avoid many steps at one
    }
  }

  // Handle S1 button
  if(digitalRead(BTN_S1) == LOW)
  {
    delay(50);   // delay to debounce
    if (digitalRead(BTN_S1) == LOW)
    {
      init_tx();
      
      delay(50); //delay to avoid many steps at one
    }
  }

  // Handle S2 button
  if(digitalRead(BTN_S2) == LOW)
  {
    delay(50);   // delay to debounce
    if (digitalRead(BTN_S2) == LOW)
    {
      if(ext_gps_ant == true)
      {
        ext_gps_ant = false;
        digitalWrite(GPS_ANT, LOW);
      }
      else
      {
        ext_gps_ant = true;
        digitalWrite(GPS_ANT, HIGH);
      }
      
      delay(50); //delay to avoid many steps at one
    }
  }
  

  // Draw the display
  
  ssd1306.buffer_clear();
  ssd1306.text(0, 0, TEXT_FONT_5X8, "OpenBeacon 2");
  ssd1306.text(90, 0, TEXT_FONT_5X8, mode_list[cur_mode]);
  sprintf(text, "%X", cur_character);
  //ssd1306.text(0, 45, TEXT_FONT_5X8, text);
  //sprintf(text, "%c", *cur_msg_p);
  //ssd1306.text(20, 45, TEXT_FONT_5X8, text);
  //sprintf(text, "%d", cur_state);
  //ssd1306.text(40, 45, TEXT_FONT_5X8, text);
  if(ext_gps_ant)
  {
    sprintf(text, "Ext Ant");
  }
  else
  {
    sprintf(text, "Int Ant");
  }
  //sprintf(text, "%lu", cur_timer);
  ssd1306.text(0, 54, TEXT_FONT_5X8, text);
  
  if(gps.time.isValid())
  {
    sprintf(text, "%02u:%02u:%02u", gps.time.hour(), gps.time.minute(), gps.time.second());
  }
  else
  {
    sprintf(text, "--:--:--");
  }
  ssd1306.text(0, 45, TEXT_FONT_5X8, text);

  sprintf(text, "%u", gps.satellites.value());
  ssd1306.text(96, 45, TEXT_FONT_5X8, text);
  //sprintf(text, "%u", gps.failedChecksum());
  //ssd1306.text(90, 54, TEXT_FONT_5X8, text);

  if(gps.location.isValid())
  {
    get_grid_square(gps.location.lat(), gps.location.lng(), text);
    ssd1306.text(55, 45, TEXT_FONT_5X8, text);
  }
  
  print_freq(9, frequency);
  
  if(tx)
  {
    ssd1306.text(123, 10, TEXT_FONT_5X8, "T");
    ssd1306.text(123, 18, TEXT_FONT_5X8, "X");
  }
  
  tx_current = get_tx_current();
  sprintf(text, "PA:%u mA", tx_current);
  ssd1306.text(0, 35, TEXT_FONT_5X8, text);
  supply_voltage = get_supply_voltage();
  sprintf(text, "VCC:%u.%u V", supply_voltage / 1000, (supply_voltage % 1000) / 100);
  ssd1306.text(64, 35, TEXT_FONT_5X8, text);
  ssd1306.hline(43, 0, 127, PIXEL_WHITE);
  
  // Display update
  ssd1306.buffer_write();
  */
  /*
  // Feed GPS data to TinyGPSPlus and mirror serial from GPS
  char ser_buffer;
  
  while(Serial1.available() > 0)
  {
    ser_buffer = Serial1.read();
    //if(GPS_DEBUG)
    //{
    //  SerialUSB.print(ser_buffer);
    //}
    gps.encode(ser_buffer);
  }
  */

  // Read ADC values
  yield();
  tx_current = get_tx_current();
  yield();
  supply_voltage = get_supply_voltage();

  // Handle the USB-UART bridge
  char in_data;
  
  //while(!SerialUSB) // Make sure connected to serial port before proceeding
  //{
   // yield();
  //}

  //if(SerialUSB)
  //{
    if(SerialUSB.available() > 0)   // see if incoming serial data:
    { 
      in_data = SerialUSB.read();  // read oldest byte in serial buffer:
      //Serial.readBytes(in_data, 1);
  
      yield();
      switch(in_data)
      {
      case 'R':
        {
          DynamicJsonBuffer jsonBuffer;
          JsonObject& root = jsonBuffer.createObject();
          //root["sensor"] = "gps";
          //root["time"] = 1351824120;
    
          root.printTo(SerialUSB);
          break;
        }
      case 'W':
        {
          String recv_config;
          char last_data;
  
          // Don't like this blocking code, but the scheduler seems
          // to lose serial data. Need better solution
          while(last_data != '@')
          {
            if(SerialUSB.available() > 0)
            {
              in_data = SerialUSB.read();
              last_data = in_data;
              if(last_data != '@')
              {
                recv_config += in_data;
              }
            }
            //yield();
          }
  
          yield();
          SerialUSB.println(recv_config);
    
          DynamicJsonBuffer jsonBuffer;
          JsonObject& root = jsonBuffer.parseObject(recv_config);
          if(!root.success())
          {
            SerialUSB.println("Unable to parse configuration");
            return;
          }
  
          yield();
          if(root.containsKey("default_buffer"))
          {
            flash_config.buffer = (enum BUFFER)root["buffer"].as<uint8_t>();
            // validate data
          }
          if(root.containsKey("msg_mem_1"))
          {
            strcpy(flash_config.msg_mem_1, root["msg_mem_1"]);
          }
          if(root.containsKey("msg_mem_2"))
          {
            strcpy(flash_config.msg_mem_2, root["msg_mem_2"]);
          }
          if(root.containsKey("msg_mem_3"))
          {
            strcpy(flash_config.msg_mem_3, root["msg_mem_3"]);
          }
          if(root.containsKey("msg_mem_4"))
          {
            strcpy(flash_config.msg_mem_4, root["msg_mem_4"]);
          }
          if(root.containsKey("callsign"))
          {
            strcpy(flash_config.callsign, root["callsign"]);
          }
          if(root.containsKey("grid"))
          {
            strcpy(flash_config.grid, root["grid"]);
          }
          if(root.containsKey("dbm"))
          {
            // validate data
            flash_config.dbm = root["ext_gps_ant"].as<uint8_t>();
          }
          if(root.containsKey("ext_gps_ant"))
          {
            // validate data
            flash_config.ext_gps_ant = root["ext_gps_ant"].as<boolean>();
          }
          if(root.containsKey("ext_pll_ref"))
          {
            flash_config.ext_pll_ref = root["ext_pll_ref"].as<boolean>();
          }
          if(root.containsKey("ext_pll_ref_freq"))
          {
            flash_config.ext_pll_ref_freq = root["ext_pll_ref_freq"].as<uint32_t>();
          }
          
          flash_store.write(flash_config);
          break;
        }
      }
    }
  //}
}

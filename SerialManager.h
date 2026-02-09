/* 
 *  SerialManager
 *  
 *  Created: Chip Audette, OpenAudio, April 2017
 *  Modified: Line-based commands with numeric arguments (gain/delay)
 *  
 *  Purpose: Receive commands over the serial link to control the Tympan.
 */

#ifndef _SerialManager_h
#define _SerialManager_h

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>

// functions in the main sketch that we want to call from here
extern void setVolKnobGain_dB(float);
extern float vol_knob_gain_dB;
extern void printGainSettings(void);

extern void togglePrintMemoryAndCPU(void);

extern void setDelay_ms(float ms);
extern float delay_ms;

// Serial Manager class
class SerialManager {
  public:
    SerialManager(void) : cmd_len_(0) { cmd_buffer_[0] = '\0'; }

    // Call this for each received character. Commands are executed on newline.
    void respondToByte(char c);

    void printHelp(void);

  private:
    void processLine_(const char *line);

    char cmd_buffer_[64];
    uint8_t cmd_len_;
};

void SerialManager::printHelp(void) {
  Serial.println();
  Serial.println("SerialManager Help: Available Commands:");
  Serial.println("   h or ?: Print this help");
  Serial.println("   g      : Print the current gain and delay settings");
  Serial.println("   C      : Toggle printing of CPU and Memory usage");
  Serial.println("   k <dB> : Set digital gain in dB (example: k 10)");
  Serial.println("   d <ms> : Set delay time in ms (example: d 25)");
  Serial.println();
}

void SerialManager::processLine_(const char *line) {
  // skip leading whitespace
  while (*line && isspace((unsigned char)*line)) line++;
  if (*line == '\0') return;

  // command letter
  char cmd = *line;
  line++;

  // skip whitespace before optional argument
  while (*line && isspace((unsigned char)*line)) line++;

  // optional numeric argument
  bool has_arg = false;
  float arg = 0.0f;
  if (*line) {
    char *endptr = nullptr;
    arg = strtof(line, &endptr);
    if (endptr != line) has_arg = true;
  }

  switch (cmd) {
    case 'h': case '?':
      printHelp();
      break;

    case 'g': case 'G':
      printGainSettings();
      Serial.print("Delay = "); Serial.print(delay_ms); Serial.println(" ms");
      break;

    case 'C': case 'c':
      Serial.println("Command Received: toggle printing of memory and CPU usage.");
      togglePrintMemoryAndCPU();
      break;

    case 'k': case 'K':
      if (has_arg) {
        setVolKnobGain_dB(arg);
      } else {
        Serial.print("Usage: k <dB>   (current = ");
        Serial.print(vol_knob_gain_dB, 1);
        Serial.println(" dB)");
      }
      break;

    case 'd': case 'D':
      if (has_arg) {
        setDelay_ms(arg);
      } else {
        Serial.print("Usage: d <ms>   (current = ");
        Serial.print(delay_ms);
        Serial.println(" ms)");
      }
      break;

    default:
      Serial.print("Unknown command: "); Serial.println(cmd);
      Serial.println("Type 'h' for help.");
      break;
  }
}

void SerialManager::respondToByte(char c) {
  if (c == '\r') return; // ignore CR

  if (c == '\n') {        // end of command
    cmd_buffer_[cmd_len_] = '\0';
    processLine_(cmd_buffer_);
    cmd_len_ = 0;
    cmd_buffer_[0] = '\0';
    return;
  }

  // accumulate into buffer
  if (cmd_len_ < (sizeof(cmd_buffer_) - 1)) {
    cmd_buffer_[cmd_len_++] = c;
    cmd_buffer_[cmd_len_] = '\0';
  } else {
    // overflow: reset buffer to avoid weird partial commands
    cmd_len_ = 0;
    cmd_buffer_[0] = '\0';
    Serial.println("Command too long. Buffer cleared.");
  }
}

#endif

/*
*   ControlViaSerial  (SerialDelayV2)
*
*   MIT License.  use at your own risk.
*/

//here are the libraries that we need
#include <Tympan_Library.h>  //include the Tympan Library
#include "SerialManager.h"

//set the sample rate and block size
const float sample_rate_Hz = 24000.0f ; //24000 or 44117 (or other frequencies in the table in AudioOutputI2S_F32)
const int audio_block_samples = 32;     //do not make bigger than AUDIO_BLOCK_SAMPLES from AudioStream.h (which is 128)
AudioSettings_F32 audio_settings(sample_rate_Hz, audio_block_samples);

//create audio library objects for handling the audio
Tympan         myTympan(TympanRev::E, audio_settings);         //do TympanRev::D or E or F
EarpieceShield earpieceShield(TympanRev::E, AICShieldRev::A);  //earpiece shield

// -------------------------
// ACTIVE PATH: EARPIECES
// -------------------------
AudioInputI2SQuad_F32    i2s_in(audio_settings);     // Quad input required for earpiece channels
AudioEffectGain_F32      gainL, gainR;               // separate objects, same settings
AudioEffectDelay_F32     delayL, delayR;             // separate objects, same settings
AudioOutputI2SQuad_F32   i2s_out(audio_settings);    // Quad output required for earpiece outputs (and/or Tympan outputs)

// Earpiece routing: FRONT mics -> gain -> delay -> earpiece receivers
AudioConnection_F32  patchCord1(i2s_in,  EarpieceShield::PDM_LEFT_FRONT,   gainL,   0);  // Left front mic -> gainL
AudioConnection_F32  patchCord2(gainL,   0,                                delayL,  0);  // gainL -> delayL
AudioConnection_F32  patchCord3(delayL,  0,                                i2s_out, EarpieceShield::OUTPUT_LEFT_EARPIECE);   // -> left earpiece receiver

AudioConnection_F32  patchCord4(i2s_in,  EarpieceShield::PDM_RIGHT_FRONT,  gainR,   0);  // Right front mic -> gainR
AudioConnection_F32  patchCord5(gainR,   0,                                delayR,  0);  // gainR -> delayR
AudioConnection_F32  patchCord6(delayR,  0,                                i2s_out, EarpieceShield::OUTPUT_RIGHT_EARPIECE);  // -> right earpiece receiver

// OPTIONAL: also send to Tympan headphone jack outputs (uncomment if you want this too)
// AudioConnection_F32  patchCord7(delayL,  0, i2s_out, EarpieceShield::OUTPUT_LEFT_TYMPAN);
// AudioConnection_F32  patchCord8(delayR,  0, i2s_out, EarpieceShield::OUTPUT_RIGHT_TYMPAN);


/*
--------------------------------------------------------------------------------
PREVIOUS (ONBOARD MIC) VERSION — kept here as an option

//create audio library objects for handling the audio
Tympan                  myTympan(TympanRev::E, audio_settings);   //do TympanRev::D or E or F
EarpieceShield          earpieceShield(TympanRev::E, AICShieldRev::A);

AudioInputI2S_F32       i2s_in(audio_settings);     //Stereo I2S input
AudioEffectGain_F32     gain1;                      //single gain
AudioEffectDelay_F32    delay1;                     //single delay
AudioOutputI2S_F32      i2s_out(audio_settings);    //Stereo I2S output

//Make all of the audio connections
AudioConnection_F32     patchCord1(i2s_in, 0, gain1, 0);        // left input -> gain
AudioConnection_F32     patchCord2(gain1, 0, delay1, 0);        // gain -> delay
AudioConnection_F32     patchCord5(delay1, 0, i2s_out, 0);      // delay -> left output
AudioConnection_F32     patchCord6(delay1, 0, i2s_out, 1);      // delay -> right output (dual-mono)
--------------------------------------------------------------------------------
*/


//control display and serial interaction
bool enable_printCPUandMemory = false;
void togglePrintMemoryAndCPU(void) { enable_printCPUandMemory = !enable_printCPUandMemory; };
SerialManager serialManager;

// define the setup() function, the function that is called once when the device is booting
const float input_gain_dB = 20.0f; // NOTE: used for analog mic preamp path; not used for earpiece digital mics
float vol_knob_gain_dB = 0.0f;     // set via serial command
float delay_ms = 10.0f;            // current delay time

void setup() {
  //begin the serial comms (for debugging)
  Serial.begin(115200);  delay(500);
  Serial.println("ControlViaSerial (SerialDelayV2 - Earpieces): Starting setup()...");

  //allocate the audio memory
  AudioMemory_F32(200, audio_settings); // increase/decrease as needed

  //Enable the Tympan + EarpieceShield to start the audio flowing!
  myTympan.enable();            // activate AIC on main board
  earpieceShield.enable();      // activate AIC on earpiece shield

  // Choose earpieces (digital mics)
  Serial.println("setup(): Using Tympan Earpieces as Inputs (front mics)");
  myTympan.enableDigitalMicInputs(true);
  earpieceShield.enableDigitalMicInputs(true);

  // Set the desired volume levels
  myTympan.volume_dB(0.0f);         // main board headphone amp (only matters if routing to OUTPUT_*_TYMPAN)
  earpieceShield.volume_dB(0.0f);   // earpiece receiver amp

  /*
  // PREVIOUS (ONBOARD MIC) SETUP

  myTympan.inputSelect(TYMPAN_INPUT_ON_BOARD_MIC); // use the on board microphones
  // myTympan.inputSelect(TYMPAN_INPUT_JACK_AS_MIC);
  // myTympan.inputSelect(TYMPAN_INPUT_JACK_AS_LINEIN);

  myTympan.setInputGain_dB(input_gain_dB); // set input volume, 0-47.5dB in 0.5dB steps.
  */

  // Set initial algorithm parameters (apply to BOTH channels)
  setVolKnobGain_dB(vol_knob_gain_dB);
  setDelay_ms(delay_ms);

  //End of setup
  Serial.println("Setup complete.");
  serialManager.printHelp();
} //end setup()


// define the loop() function, the function that is repeated over and over for the life of the device
void loop() {

  //respond to Serial commands, if any have been received
  while (Serial.available()) serialManager.respondToByte((char)Serial.read());

  //update the memory and CPU usage...if enough time has passed
  if (enable_printCPUandMemory) myTympan.printCPUandMemory(millis(), 3000); //update every 3000 msec

} //end loop();


// ///////////////////////////////////////////// functions used by SerialManager

// print current settings
void printGainSettings(void) {
  Serial.print("printGainSettings (dB): ");
  Serial.print("Vol Knob = "); Serial.print(vol_knob_gain_dB, 1);
  Serial.print(", Input PGA = "); Serial.print(input_gain_dB, 1);
  Serial.println();
}

// set digital gain (dB) — apply to BOTH gain objects
void setVolKnobGain_dB(float gain_dB) {
  vol_knob_gain_dB = gain_dB;
  gainL.setGain_dB(gain_dB);
  gainR.setGain_dB(gain_dB);
  printGainSettings();
}

// set delay (ms) — apply to BOTH delay objects
void setDelay_ms(float ms) {
  delay_ms = max(0.0f, min(1000.0f, ms));
  delayL.delay(0, delay_ms);
  delayR.delay(0, delay_ms);
  Serial.print("Delay = "); Serial.print(delay_ms); Serial.println(" ms");
}

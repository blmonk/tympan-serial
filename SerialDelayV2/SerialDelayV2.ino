/*
*   ControlViaSerial
*
*   Created: Chip Audette, OpenAudio, Apr 2017
*   Purpose: Shows how system parameters can be controlled via text commands
*      from the Arduino Serial Monitor.  After programming your Tympan,
*      go under the Arduino "Tools" menu and select "Serial Monitor".  Then
*      click in the empty text box at the top, press the letter "h" and hit Enter.
*      You should see a menu of fun commands.  You can interact with the Tympan
*      without having to reprogram it!  (note that it will *not* remember any
*      of your settings if you restart the Tympan)
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
Tympan                  myTympan(TympanRev::F, audio_settings);   //do TympanRev::D or E or F
AudioInputI2S_F32        i2s_in(audio_settings);     //Digital audio in *from* the Teensy Audio Board ADC.
AudioEffectGain_F32     gain1;                      //Applies digital gain to audio data.
AudioEffectDelay_F32    delay1;                     // audio delay (F32)
AudioOutputI2S_F32      i2s_out(audio_settings);    //Digital audio out *to* the Teensy Audio Board DAC.

//Make all of the audio connections
AudioConnection_F32       patchCord1(i2s_in, 0, gain1, 0);        // left input -> gain
AudioConnection_F32       patchCord2(gain1, 0, delay1, 0);        // gain -> delay
AudioConnection_F32       patchCord5(delay1, 0, i2s_out, 0);      // delay -> left output
AudioConnection_F32       patchCord6(delay1, 0, i2s_out, 1);      // delay -> right output (dual-mono)


//control display and serial interaction
bool enable_printCPUandMemory = false;
void togglePrintMemoryAndCPU(void) { enable_printCPUandMemory = !enable_printCPUandMemory; };
SerialManager serialManager;

// define the setup() function, the function that is called once when the device is booting
const float input_gain_dB = 20.0f; //gain on the microphone
float vol_knob_gain_dB = 0.0f;     // set via serial command
float delay_ms = 10.0f;            // current delay time

void setup() {
  //begin the serial comms (for debugging)
  Serial.begin(115200);  delay(500);
  Serial.println("ControlViaSerial (SerialDelay): Starting setup()...");

  //allocate the audio memory
  AudioMemory_F32(200, audio_settings); // increase/decrease as needed

  //Enable the Tympan to start the audio flowing!
  myTympan.enable(); // activate AIC

  //Choose the desired input
  myTympan.inputSelect(TYMPAN_INPUT_ON_BOARD_MIC); // use the on board microphones
  // myTympan.inputSelect(TYMPAN_INPUT_JACK_AS_MIC);
  // myTympan.inputSelect(TYMPAN_INPUT_JACK_AS_LINEIN);

  //Set the desired volume levels
  myTympan.volume_dB(0);                   // headphone amplifier.  -63.6 to +24 dB in 0.5dB steps.
  myTympan.setInputGain_dB(input_gain_dB); // set input volume, 0-47.5dB in 0.5dB steps

  //Set initial algorithm parameters
  gain1.setGain_dB(vol_knob_gain_dB);
  delay1.delay(0, delay_ms);               // tap 0, delay in milliseconds

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

// set digital gain (dB)
void setVolKnobGain_dB(float gain_dB) {
  gain1.setGain_dB(gain_dB);
  vol_knob_gain_dB = gain_dB;
  printGainSettings();
}

// set delay (ms)
void setDelay_ms(float ms) {
  delay_ms = max(0.0f, min(1000.0f, ms));
  delay1.delay(0, delay_ms);
  Serial.print("Delay = "); Serial.print(delay_ms); Serial.println(" ms");
}

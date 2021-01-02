
/*
 * 
 * title: chess clock, implements the Fischer-clock (mainTime + increment)
 * author: Pal VARADI NAGY
 * website: csillagtura.ro
 * copyright: copyleft 
 * board: arduino nano or compatible
 * dependencies: sevenSegmentTM1637 library 
 * 
 */

#define VERSION_STRING "20201227"



#include <EEPROM.h>
#include <Encoder.h>
#include <SevenSegmentAsciiMap.h>
#include <SevenSegmentExtended.h>
#include <SevenSegmentFun.h>
#include <SevenSegmentTM1637.h>

// ===================  CONFIG  ===============

#define DEFAULT_GAMETIME___MAINTIME___SECONDS (3*60)
#define DEFAULT_GAMETIME___INCREMENT___SECONDS (5)

#define IMPLEMENT_THE_HOURS 0 /* not enough segments on the PCB */ 

#define BEEP_QUEUE_TICK_DURATION_MS 5

#define FREQ_WHITE_TO_MOVE_HZ (440*4)
#define FREQ_BLACK_TO_MOVE_HZ (440*3)
#define FREQ_UNKNOWN_ACTION_HZ (210)

#define SERIAL_BAUDRATE 9600

#define DEBUGTOSERIAL 0


// ===================  PINS  ===============


#define PIN_BUZZER 12
#define PIN_LED_OUTPUT 13

#define PIN_SEVENSEG_BLACK_CLOCK A0
#define PIN_SEVENSEG_BLACK_DATA A1

#define PIN_SEVENSEG_WHITE_CLOCK 4
#define PIN_SEVENSEG_WHITE_DATA 5

#define PIN_ENCODER_1 2
#define PIN_ENCODER_2 3
#define PIN_ENCODER_BUTTON A3

#define PIN_WHITE 11
#define PIN_BLACK A2



// ===================  TYPES  ===============

#define EEPROM_PERSISTED_DATA_OFFSET 0

/* important for persisting into the EEPROM */
#define SANITY_STRING "sanity"
typedef char SanityCheck[8];

typedef char VersionString[8];

typedef struct GameTime {  
  uint16_t mainTime_s;
  uint16_t increment_s;
} GameTime;

typedef char ShortString[16];

typedef struct PersistedData {
  SanityCheck sanityCheck;
  VersionString versionString;
  GameTime gameTime;
  SanityCheck sanityCheck2;
} PersistedData;

typedef enum ClockState{
  #if 1 == IMPLEMENT_THE_HOURS
  SetupFirstValidPositionAfterThis,
  #endif
  SetupMainTimeHoursX,
  SetupMainTimeHoursI,
  #if 0 == IMPLEMENT_THE_HOURS
  SetupFirstValidPositionAfterThis,
  #endif
  SetupMainTimeMinutesX,
  SetupMainTimeMinutesI,
  SetupMainTimeSecondsX,
  SetupMainTimeSecondsI,
  
  SetupIncrementMinutesX,
  SetupIncrementMinutesI,
  SetupIncrementSecondsX,
  SetupIncrementSecondsI,

  SetupInvalidPosition,

  PlayWhiteToMove,
  PlayBlackToMove,
  
} ClockState; 

typedef enum ClockButton {
  bNone,
  bShift,
  bDecrease,
  bIncrease,
  bWhite,
  bBlack
} ClockButton;


typedef struct ButtonState {
  int currentState;
  int oldState;
  int sameStateCounter;
  int skipsLeft;
} ButtonState;

typedef struct Inputs {
  uint32_t lastTickExecutedAtMillis;  
  uint32_t encoderLastTickExecutedAtMillis;
  uint32_t lastButtonEventConflictingWithEncoderAtMillis;
  int encoderPosition;
  int encoderDelta;
  int oldEncoderPosition;
  int encoderSamePositionCounter;
  int encoderLastConsideredPosition;
  ButtonState encoderButton;
  ButtonState white;
  ButtonState black;
} Inputs;

typedef struct TimeLeft {
  uint16_t black;
  uint16_t white;
  ShortString whiteAsString;
  ShortString blackAsString;
  uint32_t lastTickExecutedAtMillis;
} TimeLeft;

typedef struct Displays {
  uint32_t lastTickExecutedAtMillis;  
  byte blinkerState;
  byte forceFlag;
  ShortString leftDisplay;
  ShortString rightDisplay;
  ShortString leftDisplayInternal;
  ShortString rightDisplayInternal;
} Displays;




typedef struct BeepQueueItem {
  uint16_t valid;
  uint16_t freq;
  uint16_t timeLeft_ms;  
} BeepQueueItem;

#define BEEP_QUEUE_ITEM_COUNT 32

typedef struct BeepQueue {
  uint32_t lastTickExecutedAtMillis;
  int16_t readCursor;
  int16_t writeCursor;
  uint16_t currentFreq;
  BeepQueueItem items[BEEP_QUEUE_ITEM_COUNT];
} BeepQueue;


// ================= globals ===================

Displays displays;
BeepQueue beepQueue;

Encoder settingsEncoder(PIN_ENCODER_1, PIN_ENCODER_2);

SevenSegmentExtended  sevenSegBlackMinSec((byte)PIN_SEVENSEG_BLACK_CLOCK, (byte)PIN_SEVENSEG_BLACK_DATA);
SevenSegmentExtended  sevenSegWhiteMinSec((byte)PIN_SEVENSEG_WHITE_CLOCK, (byte)PIN_SEVENSEG_WHITE_DATA);

Inputs inputs;

byte persistedDataIsDirty = 0;
PersistedData persistedData;

TimeLeft currentGameTimeLeft_s;

int clockState;

//================= some utils ====================

void secToString(uint16_t sec, char *dest){  
  int di = 0;
  if (1==IMPLEMENT_THE_HOURS){
    // hhmmss
    dest[di++] = (sec / (3600 * 10)) + 48;
    sec = sec % (3600*10);
    dest[di++] = (sec / (3600 * 1))+48;
    sec = sec % (3600 * 1);
  }else{
    // mmss as below
  }
  dest[di++] = (sec / (60*10)) + 48;
  sec = sec % (60*10);
  dest[di++] = (sec / (60 * 1)) + 48;
  sec = sec % (60 * 1);
  dest[di++] = (sec / (10 * 1)) + 48;
  sec = sec % (10 * 1);
  dest[di++] = (sec % 10) + 48;  
  dest[di++] = 0;
}

void secToStringWithoutLeadingZeros(uint16_t sec, char *dest){
  secToString(sec, dest);
  int i = 0;
  int len = strlen(dest);  
  while ((dest[i] == 48)&&(i < len - 2)){
    //leave at least two zeros
    dest[i] = 32;
    i++;
  }
}

uint16_t stringToSec(char * src){
  uint16_t sec = 0;
  int p = strlen(src)-1;
  uint16_t multi[] = {1, 10, 60, 600, 3600, 36000};
  int j = 0;
  while ((p >= 0)&&(j < 6)){
    if ((src[p] >= 48)&&(src[p] <= 48 + 9)){
      sec += (src[p]-48)*multi[j];
    }    
    p--;
    j++;
  }
  return sec;
}

void setTimeDigit(uint16_t * psec, byte digit, short int dir){
  //digit is from right to left
  uint8_t mt[16];
  secToString(psec[0], mt);  
  //Serial.print((char *)mt);
  //delay(1000);
  int theMax = 9;
  if (1 == digit){
    theMax = 5;
  }
  if (3 == digit){
    theMax = 5;
  }
  
  int p = strlen(mt)-1 -digit;
  if (p >= 0){
    mt[p] = mt[p] + dir;
    if (mt[p] < 48){
      mt[p] = 48+theMax;
    };
    if (mt[p] > 48+theMax){
      mt[p] = 48;
    };
  }
  psec[0] = stringToSec(mt);
}


// ================= implementation: non-blocking beeps ===================

void Beep_setup(){
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_OUTPUT, OUTPUT);
}

void Beep_adjustCursors(){
  while ((beepQueue.writeCursor >= BEEP_QUEUE_ITEM_COUNT) && (beepQueue.readCursor >= BEEP_QUEUE_ITEM_COUNT)){
    beepQueue.writeCursor -= BEEP_QUEUE_ITEM_COUNT;
    beepQueue.readCursor -= BEEP_QUEUE_ITEM_COUNT;
  }
}

void Beep_enqueue(uint16_t freq, uint16_t duration_ms){
  // duration gets rounded to tick-length
  beepQueue.items[beepQueue.writeCursor % BEEP_QUEUE_ITEM_COUNT ].valid = 1;
  beepQueue.items[beepQueue.writeCursor % BEEP_QUEUE_ITEM_COUNT ].freq = freq;
  beepQueue.items[beepQueue.writeCursor % BEEP_QUEUE_ITEM_COUNT ].timeLeft_ms = duration_ms;
  beepQueue.writeCursor++;
  Beep_adjustCursors();
}

void Beep_enqueueFreqAndSilence(uint16_t freq, uint16_t freq_dur_ms, uint16_t silence_dur_ms){
  Beep_enqueue(freq, freq_dur_ms);
  Beep_enqueue(0, silence_dur_ms);
}

void Beep_executeFreq(uint16_t freq){
  //freq 0 is silence
  
  if (beepQueue.currentFreq != freq){
    beepQueue.currentFreq = freq;
    if (0 == freq){      
      noTone(PIN_BUZZER);
      pinMode(PIN_BUZZER, INPUT);
      // the buzzer made a noise if left OUTPUT, 
      //   due to the buzzing of the TM1637 displays
      digitalWrite(PIN_LED_OUTPUT, LOW);
    }else{
      pinMode(PIN_BUZZER, OUTPUT);
      tone(PIN_BUZZER, freq);
      digitalWrite(PIN_LED_OUTPUT, HIGH);
    }
  }
}


void Beep_tick(void){
  if (beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].valid){
    Beep_executeFreq(beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].freq);
    if (beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].timeLeft_ms < BEEP_QUEUE_TICK_DURATION_MS){
      beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].timeLeft_ms = 0;
    }else{
      beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].timeLeft_ms -= BEEP_QUEUE_TICK_DURATION_MS;
    }
    if (0 == beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].timeLeft_ms ){
      beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].valid = 0;
      beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].freq = 0;
      if (beepQueue.readCursor < beepQueue.writeCursor){
        beepQueue.readCursor++;
        Beep_adjustCursors();
      }
      if (1 == beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].valid){
        Beep_executeFreq(beepQueue.items[beepQueue.readCursor % BEEP_QUEUE_ITEM_COUNT ].freq);
      }else{
        Beep_executeFreq(0);
      } 
    }
  }else{
    Beep_executeFreq(0);
  }
}

void Beep_mainLoopEntry(uint32_t present){
  if (present - beepQueue.lastTickExecutedAtMillis >= BEEP_QUEUE_TICK_DURATION_MS){
    beepQueue.lastTickExecutedAtMillis = present;
    Beep_tick();
  }
}

//============== clock-related beeps ====================

inline void output_blackToMove(void){
  Beep_enqueueFreqAndSilence(FREQ_BLACK_TO_MOVE_HZ, 50, 50);
  Beep_enqueueFreqAndSilence(FREQ_BLACK_TO_MOVE_HZ, 50, 50);
  Beep_enqueueFreqAndSilence(FREQ_BLACK_TO_MOVE_HZ, 50, 50);
};

inline void output_whiteToMove(void){
  Beep_enqueueFreqAndSilence(FREQ_WHITE_TO_MOVE_HZ, 50, 50);
  Beep_enqueueFreqAndSilence(FREQ_WHITE_TO_MOVE_HZ, 50, 50);
  Beep_enqueueFreqAndSilence(FREQ_WHITE_TO_MOVE_HZ, 50, 50);
};

inline void output_gameStart(void){
  output_whiteToMove();
};

void output_blackLostByTime(void){
  // 5th sym
  Beep_enqueueFreqAndSilence(784,   50, 50);
  Beep_enqueueFreqAndSilence(784,   50, 50);
  Beep_enqueueFreqAndSilence(784,   50, 50);
  Beep_enqueueFreqAndSilence(622, 1000, 50);  
};

void output_whiteLostByTime(void){
  // 5th sym
  Beep_enqueueFreqAndSilence(1568,   50, 50);
  Beep_enqueueFreqAndSilence(1568,   50, 50);
  Beep_enqueueFreqAndSilence(1568,   50, 50);
  Beep_enqueueFreqAndSilence(1244, 1000, 50);
};


inline void output_settingsExecuted(void){
  Beep_enqueueFreqAndSilence(1200, 100, 50);  
}

inline void output_unknownAction(void){
  Beep_enqueueFreqAndSilence(FREQ_UNKNOWN_ACTION_HZ, 50, 50);
  Beep_enqueueFreqAndSilence(FREQ_UNKNOWN_ACTION_HZ, 50, 50);
  Beep_enqueueFreqAndSilence(FREQ_UNKNOWN_ACTION_HZ, 50, 50);
}

void output_boot(void){  
  Beep_enqueueFreqAndSilence(440*4, 50, 50);  
  Beep_enqueueFreqAndSilence(440*5, 50, 50);
  Beep_enqueueFreqAndSilence(440*6, 50, 50);  
};

// ================= implementation: inputs ===================


int ButtonState_process(ButtonState * pb){
  if (pb->skipsLeft > 0){
    pb->skipsLeft--;
    pb->sameStateCounter = 0;
    return 0;
  }
  if (pb->currentState == pb->oldState){
    pb->sameStateCounter++;
  }else{
    pb->sameStateCounter = 0;
    pb->oldState = pb->currentState;
  }
  if (pb->sameStateCounter > 5){
    pb->sameStateCounter = 5;
    if (1 == pb->currentState){
      pb->skipsLeft = 50;
      return 1;
    }
  }
  return 0;
}


int _Encoder_mainLoopEntry(uint32_t present){ 
  int ret = bNone; 
  inputs.encoderPosition = settingsEncoder.read();  
  int32_t d = present;  
  d -= inputs.encoderLastTickExecutedAtMillis;
  if (present - inputs.lastButtonEventConflictingWithEncoderAtMillis < 200){    
    d = -1;
  }
  if (d < 0){
    inputs.oldEncoderPosition = inputs.encoderPosition;
    
  }else{
    //no conflicts, not premature
    if (inputs.oldEncoderPosition != inputs.encoderPosition){
      if (inputs.oldEncoderPosition < inputs.encoderPosition){
        inputs.encoderDelta++;
      }
      if (inputs.oldEncoderPosition > inputs.encoderPosition){
        inputs.encoderDelta--;
      }
      inputs.oldEncoderPosition = inputs.encoderPosition;
      inputs.encoderSamePositionCounter = 0;
    }else{
      inputs.encoderSamePositionCounter++;
    }
    
    if (inputs.encoderSamePositionCounter > 1000){
      inputs.encoderSamePositionCounter = 1000;
      if (present - inputs.encoderLastTickExecutedAtMillis > 100){
        inputs.encoderLastTickExecutedAtMillis = present;
        if (inputs.encoderDelta < 0){
          ret = bDecrease;
        }
        if (inputs.encoderDelta > 0){
          ret = bIncrease;
        }
        if (inputs.encoderDelta != 0){
          // throttle
          inputs.encoderLastTickExecutedAtMillis += 250;
        }
        inputs.encoderDelta = 0;
        inputs.encoderSamePositionCounter = 0;
      }        
    }
  }
  return ret;  
}
void Inputs_setup(void){
  pinMode(PIN_ENCODER_BUTTON, INPUT_PULLUP);  
  pinMode(PIN_WHITE, INPUT_PULLUP);
  pinMode(PIN_BLACK, INPUT_PULLUP);
}

int Inputs_mainLoopEntry(uint32_t present){
  int ret = bNone;
  inputs.white.currentState = (digitalRead(PIN_WHITE) == HIGH) ? 0 : 1;
  inputs.black.currentState = (digitalRead(PIN_BLACK) == HIGH) ? 0 : 1;
  inputs.encoderButton.currentState = (digitalRead(PIN_ENCODER_BUTTON) == HIGH) ? 0 : 1;
  
  if (present - inputs.lastTickExecutedAtMillis > 5){    
    inputs.lastTickExecutedAtMillis = present;    
    if (ButtonState_process(&inputs.black)){
       ret = bBlack;
    }
    if (ButtonState_process(&inputs.white)){
       ret = bWhite;
    }
    if (ButtonState_process(&inputs.encoderButton)){
       inputs.lastButtonEventConflictingWithEncoderAtMillis = present;
       ret = bShift; 
    }
  }
  if (bNone == ret){
    ret = _Encoder_mainLoopEntry(present);
  }
  return ret;
}

void Clock_processUserInput(int b){
  if (bShift == b){
    ClockState_executeButtonAction(bShift);
    Displays_forceRefresh();
  }
  if (bWhite == b){
    ClockState_executeButtonAction(bWhite);
    secToStringWithoutLeadingZeros(currentGameTimeLeft_s.black, displays.leftDisplay);
    secToStringWithoutLeadingZeros(currentGameTimeLeft_s.white, displays.rightDisplay);
    Displays_forceRefresh();
  }
  if (bBlack == b){
    ClockState_executeButtonAction(bBlack);
    secToStringWithoutLeadingZeros(currentGameTimeLeft_s.black, displays.leftDisplay);
    secToStringWithoutLeadingZeros(currentGameTimeLeft_s.white, displays.rightDisplay);       
    Displays_forceRefresh();       
  };
  if (bDecrease == b){
    ClockState_executeButtonAction(bDecrease);
    Displays_forceRefresh();
  }
  if (bIncrease == b){
    ClockState_executeButtonAction(bIncrease);
    Displays_forceRefresh();
  };  
}

//


//======== implementation: Persisted Data ======================

void PersistedData_setDefault(){
  strcpy(persistedData.sanityCheck, SANITY_STRING);
  strcpy(persistedData.sanityCheck2, SANITY_STRING);
  strcpy(persistedData.versionString, VERSION_STRING);  
  persistedData.gameTime.mainTime_s = DEFAULT_GAMETIME___MAINTIME___SECONDS;
  persistedData.gameTime.increment_s = DEFAULT_GAMETIME___INCREMENT___SECONDS;
  persistedDataIsDirty = 1;
}

void PersistedData_toCurrentGameTime(){
  currentGameTimeLeft_s.white = persistedData.gameTime.mainTime_s;
  currentGameTimeLeft_s.black = persistedData.gameTime.mainTime_s;
  currentGameTimeLeft_s.lastTickExecutedAtMillis = millis();
}

void PersistedData_load(){
  EEPROM.get(EEPROM_PERSISTED_DATA_OFFSET, persistedData);
  if ((strcmp(SANITY_STRING, persistedData.sanityCheck) != 0) || (strcmp(SANITY_STRING, persistedData.sanityCheck2) != 0)){
    // uninitialized eeprom, maybe
    PersistedData_setDefault();
  }else{
    // the version may still be a mismatch, 
    //  but at least the record is byte compatible
    //  so we ignore this now
  }
  PersistedData_toCurrentGameTime();
}

void PersistedData_persistIfDirty(){
  if (0 != persistedDataIsDirty){
    persistedDataIsDirty = 0;
    Serial.println("persisting to eeprom");
    EEPROM.put(EEPROM_PERSISTED_DATA_OFFSET, persistedData);
  }
}

//======== implementation: chess clock input ====================

void setMainTimeDigit(byte digit, short int dir){
  setTimeDigit(&persistedData.gameTime.mainTime_s, digit, dir);
  persistedDataIsDirty = 1;
}

void setIncrementDigit(byte digit, short int dir){
  setTimeDigit(&persistedData.gameTime.increment_s, digit, dir);
  persistedDataIsDirty = 1;
}

void ClockState_executeButtonAction(int b){
  int beepHandled = 0;
  Debug_printButtonExecution(b);
  if (bShift == b){
    if ((PlayWhiteToMove == clockState) || (PlayBlackToMove == clockState)){
      clockState = SetupInvalidPosition;
      // effectively freezes the game clock before entering setup
      // should all blink
      return ;
    }
    clockState++;    
    if (clockState >= SetupInvalidPosition){
      clockState = SetupFirstValidPositionAfterThis+1;
    }    
    output_settingsExecuted();
    return ;
  }

  if ((bDecrease == b) || (bIncrease == b)){
    int bh = 0;
    if (SetupMainTimeHoursX == clockState){
      setMainTimeDigit(5, (bDecrease == b) ? -1 : 1);
      bh = 1;      
    }    
    if (SetupMainTimeHoursI == clockState){
      setMainTimeDigit(4, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupMainTimeMinutesX == clockState){
      setMainTimeDigit(3, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupMainTimeMinutesI == clockState){
      setMainTimeDigit(2, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupMainTimeSecondsX == clockState){
      setMainTimeDigit(1, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupMainTimeSecondsI == clockState){
      setMainTimeDigit(0, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupIncrementMinutesX == clockState){
      setIncrementDigit(3, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupIncrementMinutesI == clockState){
      setIncrementDigit(2, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupIncrementSecondsX == clockState){
      setIncrementDigit(1, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    
    if (SetupIncrementSecondsI == clockState){
      setIncrementDigit(0, (bDecrease == b) ? -1 : 1);
      bh = 1;
    }    

    if (1 == bh){
      beepHandled = 1;
      output_settingsExecuted();
    }
  };  


  
  if (bBlack == b){    
    if (clockState <= SetupInvalidPosition){
      //we are in setup, so this starts the game
      PersistedData_toCurrentGameTime();
      PersistedData_persistIfDirty();
      clockState = PlayWhiteToMove;      
      output_gameStart();
      beepHandled = 1;
    }else{      
      //we are in a game, so switch
      if (PlayBlackToMove == clockState){
        clockState = PlayWhiteToMove;
        if (currentGameTimeLeft_s.black > 0){
          currentGameTimeLeft_s.black += persistedData.gameTime.increment_s;
          output_whiteToMove();
          beepHandled = 1;
        }
      }      
    }
  }
  if (bWhite == b){
    if (clockState <= SetupInvalidPosition){
      //we are in setup, so this does nothing
    }else{
      //we are in a game, so switch
      if (PlayWhiteToMove == clockState){
        clockState = PlayBlackToMove;
        if (currentGameTimeLeft_s.white > 0){
          currentGameTimeLeft_s.white += persistedData.gameTime.increment_s;
        }
        output_blackToMove();
        beepHandled = 1;        
      }      
    }
  }
  if (0 == beepHandled){
    output_unknownAction();
  }
}




void Displays_forceRefresh(void){
  displays.forceFlag = 1;
}

void Displays_copyFromExternalToInternalConsideringSevenSegSizes(char * dest, char * src){
  int charLen = 8;
  if (1 == IMPLEMENT_THE_HOURS){
    //    
  }else{
    charLen = 4;
  }
  dest[0] = 0;
  int delta = charLen - strlen(src);
  while (delta > 0){
    strcat(dest, " ");
    delta--;
  }
  if (delta < 0){
    // truncate stuff on the left
    delta = 0 - delta;
  }
  strcpy(dest, src + delta);
}

void Displays_blinkOutDigit(int digit, int leftOrRight){
  char * s;
  if (0 == leftOrRight){
    s = displays.leftDisplayInternal;
  }else{
    s = displays.rightDisplayInternal;
  }
  int len = strlen(s);
  int i = len - 1 - digit;
  if (digit >= 0){
    if (displays.blinkerState % 2 == 0){
      s[i] = ' ';
    }
  }
};



void Displays_blankAccordingToClockState(){
    if (SetupMainTimeHoursX == clockState){
      Displays_blinkOutDigit(5, 0);
    }   
    if (SetupMainTimeHoursI == clockState){
      Displays_blinkOutDigit(4, 0);
    }    
//----    
    if (SetupMainTimeMinutesX == clockState){
      Displays_blinkOutDigit(3, 0);
    }    
    if (SetupMainTimeMinutesI == clockState){
      Displays_blinkOutDigit(2, 0);
    }    
    if (SetupMainTimeSecondsX == clockState){
      Displays_blinkOutDigit(1, 0);
    }        
    if (SetupMainTimeSecondsI == clockState){
      Displays_blinkOutDigit(0, 0);
    }    
//----
    if (SetupIncrementMinutesX == clockState){
      Displays_blinkOutDigit(3, 1);
    }    
    if (SetupIncrementMinutesI == clockState){
      Displays_blinkOutDigit(2, 1);
    }    
    if (SetupIncrementSecondsX == clockState){
      Displays_blinkOutDigit(1, 1);
    }    
    if (SetupIncrementSecondsI == clockState){
      Displays_blinkOutDigit(0, 1);
    }    
}

void Displays_writeInternalsOntoTheSevenSegs(void){
      sevenSegBlackMinSec.print(displays.leftDisplayInternal);  
      sevenSegWhiteMinSec.print(displays.rightDisplayInternal);  
}

void Displays_putColonAccordingToClockState(void){
    int colonHandled = 0;
    if (clockState == PlayWhiteToMove){
      colonHandled = 1;
      sevenSegWhiteMinSec.setColonOn(0 == displays.blinkerState % 2);
      sevenSegBlackMinSec.setColonOn(1 == 1);      
    }    
    if (clockState == PlayBlackToMove){
      colonHandled = 1;
      sevenSegWhiteMinSec.setColonOn(1 == 1);
      sevenSegBlackMinSec.setColonOn(0 == displays.blinkerState % 2);      
    }    

    if (0 == colonHandled){
      sevenSegBlackMinSec.setColonOn(0 == displays.blinkerState % 2);
      sevenSegWhiteMinSec.setColonOn(0 == displays.blinkerState % 2);
    }
}

void Displays_setup(){
  sevenSegWhiteMinSec.begin();
  sevenSegWhiteMinSec.setBacklight(100);  // set the brightness to 100 %
  sevenSegBlackMinSec.begin();
  sevenSegBlackMinSec.setBacklight(100);  // set the brightness to 100 %
  Displays_forceRefresh();  
}

void Displays_mainLoopEntry(uint32_t present){
  if ((present - displays.lastTickExecutedAtMillis >= 250) || (1 == displays.forceFlag)){
    if ((clockState >= SetupFirstValidPositionAfterThis) && (clockState < SetupInvalidPosition)){
       secToString(persistedData.gameTime.mainTime_s, displays.leftDisplay);
       secToString(persistedData.gameTime.increment_s, displays.rightDisplay);
    }
    Debug_printClockState();    
    displays.lastTickExecutedAtMillis = present;
    displays.forceFlag = 0;
    displays.blinkerState++;
    Displays_copyFromExternalToInternalConsideringSevenSegSizes(displays.leftDisplayInternal, displays.leftDisplay);
    Displays_copyFromExternalToInternalConsideringSevenSegSizes(displays.rightDisplayInternal, displays.rightDisplay);
    Displays_blankAccordingToClockState();
    Displays_putColonAccordingToClockState();

    Displays_writeInternalsOntoTheSevenSegs();
    
    Serial.print(displays.leftDisplayInternal);
    Serial.print(' ');
    Serial.println(displays.rightDisplayInternal);
  }  
}

void Game_mainLoopEntry(uint32_t present){
  if (present - currentGameTimeLeft_s.lastTickExecutedAtMillis >= 1000){
    currentGameTimeLeft_s.lastTickExecutedAtMillis = present;
    if (PlayWhiteToMove == clockState){
      if (currentGameTimeLeft_s.white > 0){
        currentGameTimeLeft_s.white--;
      }else{
        currentGameTimeLeft_s.white = 0;
        clockState = SetupInvalidPosition;
        output_whiteLostByTime();
      }      
      secToStringWithoutLeadingZeros(currentGameTimeLeft_s.black, displays.leftDisplay);
      secToStringWithoutLeadingZeros(currentGameTimeLeft_s.white, displays.rightDisplay);
      Displays_forceRefresh();
    };
    if (PlayBlackToMove == clockState){
      if (currentGameTimeLeft_s.black > 0){
        currentGameTimeLeft_s.black--;
      }else{
        currentGameTimeLeft_s.black = 0;
        clockState = SetupInvalidPosition;
        output_blackLostByTime();
      }     
      secToStringWithoutLeadingZeros(currentGameTimeLeft_s.black, displays.leftDisplay);
      secToStringWithoutLeadingZeros(currentGameTimeLeft_s.white, displays.rightDisplay);
      Displays_forceRefresh();
    };
  };
}


//======== some debug code =============================


void Debug_printClockState(){
  #if 1 == DEBUGTOSERIAL
    Serial.print("clockstate=");
    if (clockState == SetupMainTimeHoursX){
      Serial.println("SetupMainTimeHoursX");
    }
    if (clockState == SetupMainTimeHoursI){
      Serial.println("SetupMainTimeHoursI");
    }
    if (clockState == SetupFirstValidPositionAfterThis){
      Serial.println("SetupFirstValidPositionAfterThis");
    }
    if (clockState == SetupMainTimeMinutesX){
      Serial.println("SetupMainTimeMinutesX");
    }
    if (clockState == SetupMainTimeMinutesI){
      Serial.println("SetupMainTimeMinutesI");
    }
    if (clockState == SetupMainTimeSecondsX){
      Serial.println("SetupMainTimeSecondsX");
    }
    if (clockState == SetupMainTimeSecondsI){
      Serial.println("SetupMainTimeSecondsI");
    }
    if (clockState == SetupIncrementMinutesX){
      Serial.println("SetupIncrementMinutesX");
    }
    if (clockState == SetupIncrementMinutesI){
      Serial.println("SetupIncrementMinutesI");
    }
    if (clockState == SetupIncrementSecondsX){
      Serial.println("SetupIncrementSecondsX");
    }
    if (clockState == SetupIncrementSecondsI){
      Serial.println("SetupIncrementSecondsI");
    }
    if (clockState == SetupInvalidPosition){
      Serial.println("SetupInvalidPosition");
    }
    if (clockState == PlayWhiteToMove){
      Serial.println("PlayWhiteToMove");
    }
    if (clockState == PlayBlackToMove){
      Serial.println("PlayBlackToMove");
    }
  #endif  
}


void Debug_printButtonExecution(int b){
  #if 1 == DEBUGTOSERIAL
  Serial.print("buttonExec=");
  if (bShift == b){
    Serial.println("bShift");
  }
  if (bDecrease == b){
    Serial.println("bDecrease");
  }
  if (bIncrease == b){
    Serial.println("bIncrease");
  }
  if (bWhite == b){
    Serial.println("bWhite");
  }
  if (bBlack == b){
    Serial.println("bBlack");
  }
  #endif
};

void ClockState_setup(){
  clockState = SetupInvalidPosition;
  secToStringWithoutLeadingZeros(persistedData.gameTime.mainTime_s, displays.leftDisplay);
  secToStringWithoutLeadingZeros(persistedData.gameTime.increment_s, displays.rightDisplay);
}

//===== the arduino callbacks =====================================

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Beep_setup();
  Inputs_setup();
  PersistedData_load();
  ClockState_setup();
  Displays_setup();
  output_boot();
}



void loop() {
  uint32_t present = millis();
  Beep_mainLoopEntry(present);
  Game_mainLoopEntry(present);  
  Clock_processUserInput(
    Inputs_mainLoopEntry(present)
  );
  Displays_mainLoopEntry(present);
}

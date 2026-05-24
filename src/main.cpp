/**
 * @file main.cpp
 * @brief Super Decoder / Crack The Code - Centered Pop-Up Edition
 */

#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <pgmspace.h> 

// ============================================================
// HARDWARE DEFINITIONS
// ============================================================
#define WS2812B_DATA_PIN   2   
#define NUM_LEDS           40
#define SH110X_SDA         8   
#define SH110X_SCL         9   
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);

#define BUTTON_PREV        4   
#define BUTTON_NEXT        6   
#define BUTTON_SUBMIT      7   
#define BUTTON_COLOR       10  
#define BUTTON_RESETGAME   12  

#define BATT_PIN           1   
#define BATT_R1            100000.0f
#define BATT_R2            100000.0f
#define BATT_ADC_MAX       4095.0f
#define BATT_SAMPLES       16

#define BUZZER_PIN         3   

// ============================================================
// GAME SETTINGS & STATE
// ============================================================
#define CODE_LENGTH        4
#define MAX_GUESSES        5
#define NUM_GAME_COLORS    6   
#define DEBOUNCE_MS        40
#define BLINK_INTERVAL     400 

CRGB leds[NUM_LEDS];
uint8_t guessHistory[MAX_GUESSES][CODE_LENGTH];
uint8_t secretCode[CODE_LENGTH];
uint8_t feedbackExact[MAX_GUESSES];
uint8_t feedbackColor[MAX_GUESSES];
uint8_t feedbackPegs[MAX_GUESSES][CODE_LENGTH]; 
uint8_t currentRow = 0;
uint8_t currentCol = 0;
uint8_t currentGuess[CODE_LENGTH];

enum AppState { STATE_MENU, STATE_PLAYING, STATE_GAMEOVER, STATE_HELP, STATE_CONFIRM_RESET };
AppState appState = STATE_MENU;
uint8_t  menuSelection = 0;
uint8_t  resetSelection = 1; // 0 = YES, 1 = NO
uint8_t  gameMode = 1; 
uint8_t  allowedGuesses = 5; 
uint8_t  timerSelection = 0; 
bool     isGameWon = false; 

// Time Attack Tracking
int      timeRemaining = 0;
uint32_t lastTickTime = 0;

// Animation Tracking for OLED Marquee
int gameOverTitleX = 128;
int gameOverTitleDir = -1;

const CRGB GAME_COLORS[] = {
  CRGB(180, 0, 0), CRGB(0, 180, 0), CRGB(0, 0, 180),
  CRGB(180, 180, 0), CRGB(180, 0, 180), CRGB(0, 180, 180),
  CRGB(0, 0, 0)
};
const char* MODE_NAMES[]  = {"Random", "Unique", "Repeat"};
const char* COLOR_NAMES[] = {"Red", "Grn", "Blu", "Yel", "Mag", "Cyn", "---"};
const char* TIMER_NAMES[] = {"OFF", "60s", "90s"};

uint32_t lastBlink = 0;
bool     blinkState = false;
float    batteryVoltage = 4.2f;
uint8_t  batteryPct = 100;
uint32_t lastBattRead = 0;

// ============================================================
// AUDIO ENGINE
// ============================================================

void beepNav()   { tone(BUZZER_PIN, 800, 20); }
void beepColor() { tone(BUZZER_PIN, 1200, 20); }
void beepError() { tone(BUZZER_PIN, 150, 300); }
void beepTick()  { tone(BUZZER_PIN, 2000, 20); }

void playRowSubmit() {
  tone(BUZZER_PIN, 800, 50); delay(60);
  tone(BUZZER_PIN, 1200, 80);
}

void playWinJingle() {
  tone(BUZZER_PIN, 1319, 100); delay(120); 
  tone(BUZZER_PIN, 1568, 100); delay(120); 
  tone(BUZZER_PIN, 2637, 200); delay(220); 
  tone(BUZZER_PIN, 2093, 100); delay(120); 
  tone(BUZZER_PIN, 2349, 100); delay(120); 
  tone(BUZZER_PIN, 3136, 400);             
}

void playLoseJingle() {
  tone(BUZZER_PIN, 311, 300); delay(350); 
  tone(BUZZER_PIN, 294, 300); delay(350); 
  tone(BUZZER_PIN, 277, 300); delay(350); 
  tone(BUZZER_PIN, 262, 800);             
}

void playBootJingle() {
  tone(BUZZER_PIN, 440, 100); delay(120); 
  tone(BUZZER_PIN, 554, 100); delay(120); 
  tone(BUZZER_PIN, 659, 100); delay(120); 
  tone(BUZZER_PIN, 880, 250); delay(250); 
}

void playResetJingle() {
  tone(BUZZER_PIN, 1047, 50); delay(60);  
  tone(BUZZER_PIN, 784,  50); delay(60);  
  tone(BUZZER_PIN, 523, 100); delay(120); 
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

int getLEDIndex(int row, int col) {
  int physicalRow = 4 - row; 
  return (physicalRow * 8) + col;                     
}

float readBatteryVoltage() {
  long sumMv = 0;
  for (int i = 0; i < BATT_SAMPLES; i++) { 
    sumMv += analogReadMilliVolts(BATT_PIN); 
    delay(2); 
  }
  float vPin = (sumMv / BATT_SAMPLES) / 1000.0f; 
  float vBatt = vPin * ((BATT_R1 + BATT_R2) / BATT_R2);
  return (vBatt > 4.25f) ? 4.25f : (vBatt < 2.8f ? 2.8f : vBatt);
}

uint8_t batteryPercent(float voltage) {
  if (voltage >= 4.20f) return 100;
  if (voltage >= 3.70f) return 50 + (voltage - 3.70f) * 100;
  if (voltage >= 3.20f) return (voltage - 3.20f) * 20;
  return 0;
}

void drawBatteryIcon(float voltage, int x, int y, bool isBlack = false) {
  uint16_t color = isBlack ? SH110X_BLACK : SH110X_WHITE;
  uint8_t pct = batteryPercent(voltage);
  uint8_t bars = pct / 25;
  display.drawRect(x, y, 22, 10, color);
  display.fillRect(x + 22, y + 3, 3, 4, color);
  for (uint8_t i = 0; i < 4; i++) {
    if (i < bars) display.fillRect(x + 2 + i * 5, y + 2, 4, 6, color);
  }
}

// ============================================================
// UI SCREENS
// ============================================================

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1); 
  display.fillRect(0, 0, 128, 13, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(14, 3); display.print(F("CRACK THE CODE"));
  
  display.setTextColor(SH110X_WHITE);
  auto drawCursor = [](int idx, int y, const char* text) {
    display.setCursor(10, y);
    if (menuSelection == idx) {
      display.print(blinkState ? F("> ") : F("  ")); 
    } else { display.print(F("  ")); }
    display.print(text);
  };

  drawCursor(0, 16, "START GAME");
  drawCursor(1, 26, "MODE: "); display.print(MODE_NAMES[gameMode - 1]);
  drawCursor(2, 36, "CHANCES: "); display.print(allowedGuesses);
  drawCursor(3, 46, "TIMER: "); display.print(TIMER_NAMES[timerSelection]);
  drawCursor(4, 56, "HELP");
  
  drawBatteryIcon(batteryVoltage, 100, 52);
  display.display();
}

void drawHelpScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 12, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(30, 2); display.print(F("HOW TO PLAY"));
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 16); display.print(F("Guess 4 secret colors"));
  display.setCursor(0, 28); display.print(F("W = Exact match"));
  display.setCursor(0, 40); display.print(F("O = Color match only"));
  display.setCursor(16, 54);
  if (blinkState) display.print(F("Press ANY BUTTON"));
  display.display();
}

void drawGameStatus() {
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 12, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(2, 2); display.print(F("CRACK THE CODE"));
  drawBatteryIcon(batteryVoltage, 102, 1, true);
  display.setTextColor(SH110X_WHITE);
  
  display.setCursor(0, 15); display.print(F("Row: ")); display.print(currentRow + 1); display.print(F("/")); display.print(allowedGuesses);
  
  display.setCursor(85, 15); 
  if (timeRemaining > 0) {
    display.print(F("T:")); display.print(timeRemaining); display.print(F("s"));
  } else {
    display.print(F("Md:")); display.print(gameMode);
  }
  
  display.setCursor(0, 30); display.print(F("Col: ")); display.print(currentCol + 1);
  display.setCursor(60, 30); display.print(F("Clr: ")); display.print(currentGuess[currentCol] < NUM_GAME_COLORS ? COLOR_NAMES[currentGuess[currentCol]] : "---");
  
  if (currentRow > 0) {
    display.setCursor(0, 50); display.print(F("Last: W=")); display.print(feedbackExact[currentRow-1]);
    display.print(F(" O=")); display.print(feedbackColor[currentRow-1]);
  }

  display.display();
}

void drawGameOver() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  if (isGameWon) {
    display.setCursor(gameOverTitleX, 16); display.print(F("GAME WON"));
  } else {
    display.setCursor(gameOverTitleX, 16); display.print(F("GAME OVER"));
  }
  display.setTextSize(1);
  display.setCursor(16, 44);
  if (blinkState) display.print(F("Press ANY BUTTON"));
  display.display();
}

// NEW: Centered Confirmation Pop-Up Screen
void drawConfirmReset() {
  display.clearDisplay();
  display.setTextSize(1);
  
  // Draw the centered bounding box
  display.drawRect(14, 12, 100, 40, SH110X_WHITE);
  display.fillRect(14, 12, 100, 11, SH110X_WHITE); // Inverted Header bar
  
  // Title Text (perfectly centered horizontally)
  display.setTextColor(SH110X_BLACK);
  display.setCursor(34, 14); 
  display.print(F("EXIT GAME?"));
  
  display.setTextColor(SH110X_WHITE);
  
  // YES Option
  display.setCursor(43, 28);
  if (resetSelection == 0) { 
    display.print(blinkState ? F("> YES <") : F("  YES  ")); 
  } else { 
    display.print(F("  YES  ")); 
  }
  
  // NO Option
  display.setCursor(46, 40);
  if (resetSelection == 1) { 
    display.print(blinkState ? F("> NO <") : F("  NO  ")); 
  } else { 
    display.print(F("  NO  ")); 
  }
  
  display.display();
}

// ============================================================
// LED RENDERING & ANIMATIONS
// ============================================================

void renderAll() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
  for (uint8_t r = 0; r < MAX_GUESSES; r++) {
    for (uint8_t c = 0; c < CODE_LENGTH; c++) {
      if (r < currentRow) { leds[getLEDIndex(r, c)] = GAME_COLORS[guessHistory[r][c]]; } 
      else if (r == currentRow) {
        if (currentGuess[c] < NUM_GAME_COLORS) {
          CRGB temp = GAME_COLORS[currentGuess[c]];
          temp.nscale8(30); 
          leds[getLEDIndex(r, c)] = temp;
        } else { 
          leds[getLEDIndex(r, c)] = CRGB(35, 35, 35); 
        }
      }
    }
    if (r < currentRow) {
      for (uint8_t i = 0; i < CODE_LENGTH; i++) {
        if (feedbackPegs[r][i] == 2) leds[getLEDIndex(r, i + 4)] = CRGB::White; 
        else if (feedbackPegs[r][i] == 1) leds[getLEDIndex(r, i + 4)] = CRGB(100, 50, 0); 
        else leds[getLEDIndex(r, i + 4)] = CRGB(10, 10, 10); 
      }
    }
  }
  FastLED.show();
}

void doEndGameTransition() {
  CRGB flashColor = isGameWon ? CRGB(0, 200, 0) : CRGB(200, 0, 0);
  
  if (isGameWon) playWinJingle();
  else playLoseJingle();

  for(int flash = 0; flash < 4; flash++) {
    for(int i = 0; i < NUM_LEDS; i++) leds[i] = flashColor;
    FastLED.show();
    delay(150);
    FastLED.clear();
    FastLED.show();
    delay(150);
  }
}

void handleAnimations() {
  static uint32_t lastAnimUpdate = 0;
  static uint8_t borderOffset = 0;
  
  if (millis() - lastBlink >= BLINK_INTERVAL) {
    lastBlink = millis();
    blinkState = !blinkState;
    
    if (appState == STATE_MENU) drawMenu();
    if (appState == STATE_HELP) drawHelpScreen();
    if (appState == STATE_CONFIRM_RESET) drawConfirmReset(); 
    
    if (appState == STATE_PLAYING) {
      drawGameStatus(); 
      int idx = getLEDIndex(currentRow, currentCol);
      if (blinkState) {
        leds[idx] = (currentGuess[currentCol] < NUM_GAME_COLORS) ? GAME_COLORS[currentGuess[currentCol]] : CRGB(150, 150, 150);
      } else {
        if (currentGuess[currentCol] < NUM_GAME_COLORS) {
          CRGB tempColor = GAME_COLORS[currentGuess[currentCol]];
          tempColor.nscale8(30);
          leds[idx] = tempColor;
        } else {
          leds[idx] = CRGB(35, 35, 35);
        }
      }
      FastLED.show();
    }
  }

  if (appState == STATE_GAMEOVER && millis() - lastAnimUpdate >= 60) {
    lastAnimUpdate = millis();

    CRGB borderColor = isGameWon ? CRGB(0, 40, 0) : CRGB(40, 0, 0);
    CRGB chaserColor = isGameWon ? CRGB(0, 255, 0) : CRGB(255, 0, 0);

    const int perim[22] = {
      getLEDIndex(0,0), getLEDIndex(0,1), getLEDIndex(0,2), getLEDIndex(0,3), getLEDIndex(0,4), getLEDIndex(0,5), getLEDIndex(0,6), getLEDIndex(0,7),
      getLEDIndex(1,7), getLEDIndex(2,7), getLEDIndex(3,7),
      getLEDIndex(4,7), getLEDIndex(4,6), getLEDIndex(4,5), getLEDIndex(4,4), getLEDIndex(4,3), getLEDIndex(4,2), getLEDIndex(4,1), getLEDIndex(4,0),
      getLEDIndex(3,0), getLEDIndex(2,0), getLEDIndex(1,0)
    };

    FastLED.clear();
    for(int i = 0; i < 22; i++) leds[perim[i]] = borderColor;
    for(int i = 0; i < 5; i++) leds[perim[(borderOffset + i) % 22]] = chaserColor;
    for(int i = 0; i < 4; i++) leds[getLEDIndex(2, i + 2)] = GAME_COLORS[secretCode[i]];
    
    FastLED.show();
    
    if (isGameWon) {
      tone(BUZZER_PIN, 1000 + (borderOffset * 40), 20); 
    } else {
      tone(BUZZER_PIN, 300 + (borderOffset * 5), 20); 
    }

    borderOffset = (borderOffset + 1) % 22; 

    gameOverTitleX -= 5; 
    if (gameOverTitleX < -115) gameOverTitleX = 128; 
    drawGameOver(); 
  }
}

// ============================================================
// GAME CORE LOGIC
// ============================================================

void generateCode() {
  if (gameMode == 1) {
    for (int i=0; i<4; i++) secretCode[i] = random(NUM_GAME_COLORS);
  } else if (gameMode == 2) {
    uint8_t p[]={0,1,2,3,4,5};
    for(int i=0; i<4; i++) { 
      int j=i+random(NUM_GAME_COLORS-i); 
      int t=p[i]; p[i]=p[j]; p[j]=t; 
      secretCode[i]=p[i]; 
    }
  } else {
    bool hasRepeat = false;
    while (!hasRepeat) {
      for(int i=0; i<4; i++) secretCode[i] = random(NUM_GAME_COLORS);
      for(int i=0; i<4; i++) for(int j=i+1; j<4; j++) if(secretCode[i]==secretCode[j]) hasRepeat=true;
    }
  }
}

void calcFeedback(uint8_t row) {
  uint8_t exact = 0, colorOnly = 0;
  bool sUsed[4] = {0}, gUsed[4] = {0};
  
  for(int i=0; i<4; i++) feedbackPegs[row][i] = 0;
  for (int i=0; i<4; i++) {
    if (guessHistory[row][i] == secretCode[i]) { exact++; sUsed[i]=gUsed[i]=1; feedbackPegs[row][i] = 2; }
  }
  for (int i=0; i<4; i++) {
    if (gUsed[i]) continue;
    for (int j=0; j<4; j++) {
      if (!sUsed[j] && guessHistory[row][i] == secretCode[j]) { colorOnly++; sUsed[j]=1; feedbackPegs[row][i] = 1; break; }
    }
  }
  feedbackExact[row] = exact;
  feedbackColor[row] = colorOnly;
}

void initGame() {
  currentRow = currentCol = 0;
  isGameWon = false;
  
  if (timerSelection == 0) timeRemaining = 0;
  else if (timerSelection == 1) timeRemaining = 60;
  else if (timerSelection == 2) timeRemaining = 90;
  lastTickTime = millis();

  for (int r=0; r<5; r++) { 
    feedbackExact[r]=feedbackColor[r]=0; 
    for(int c=0; c<4; c++) { guessHistory[r][c]=255; feedbackPegs[r][c]=0; }
  }
  for (int c=0; c<4; c++) currentGuess[c]=255;
  generateCode(); 
  appState = STATE_PLAYING; 
  renderAll(); 
  drawGameStatus();
}

// ============================================================
// MAIN EXECUTION
// ============================================================

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  
  pinMode(BUZZER_PIN, OUTPUT);

  playBootJingle();

  display.begin(0x3C, true);
  display.setContrast(0); 
  display.setTextWrap(false);
  display.clearDisplay(); display.display();
  
  uint8_t p[] = {4,6,7,10,12};
  for(int i=0; i<5; i++) pinMode(p[i], INPUT_PULLUP);
  
  FastLED.addLeds<WS2812B, WS2812B_DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(20); 
  
  batteryVoltage = readBatteryVoltage();
  drawMenu();
}

void loop() {
  uint32_t now = millis();
  if (now - lastBattRead > 10000) { batteryVoltage = readBatteryVoltage(); batteryPct = batteryPercent(batteryVoltage); lastBattRead = now; }

  if (digitalRead(BUTTON_RESETGAME) == LOW) { 
    delay(50); 
    if (digitalRead(BUTTON_RESETGAME) == LOW) {
      if (appState == STATE_PLAYING) {
        beepNav();
        appState = STATE_CONFIRM_RESET;
        resetSelection = 1; 
        drawConfirmReset();
      } else if (appState != STATE_CONFIRM_RESET) {
        playResetJingle(); 
        appState = STATE_MENU; FastLED.clear(); FastLED.show(); drawMenu(); 
      }
      delay(200);
    }
    return; 
  }

  handleAnimations();

  if (appState == STATE_PLAYING && timeRemaining > 0) {
    if (millis() - lastTickTime >= 1000) {
      lastTickTime = millis();
      timeRemaining--;
      
      if (timeRemaining <= 10 && timeRemaining > 0) {
        beepTick(); 
      }
      
      if (timeRemaining == 0) {
        isGameWon = false;
        doEndGameTransition();
        gameOverTitleX = 128;
        gameOverTitleDir = -1;
        appState = STATE_GAMEOVER;
        drawGameOver();
      } else {
        drawGameStatus();
      }
    }
  }

  if (appState == STATE_CONFIRM_RESET) {
    if (digitalRead(BUTTON_PREV) == LOW || digitalRead(BUTTON_NEXT) == LOW || digitalRead(BUTTON_COLOR) == LOW) {
      beepNav();
      resetSelection = (resetSelection == 0) ? 1 : 0; 
      drawConfirmReset();
      delay(200);
    }
    if (digitalRead(BUTTON_SUBMIT) == LOW) {
      if (resetSelection == 0) {
        playResetJingle();
        appState = STATE_MENU;
        FastLED.clear(); FastLED.show();
        drawMenu();
      } else {
        beepNav();
        appState = STATE_PLAYING;
        lastTickTime = millis(); 
        drawGameStatus();
      }
      delay(200);
    }
  }
  else if (appState == STATE_HELP || appState == STATE_GAMEOVER) {
    if (digitalRead(BUTTON_PREV) == LOW || digitalRead(BUTTON_NEXT) == LOW || 
        digitalRead(BUTTON_SUBMIT) == LOW || digitalRead(BUTTON_COLOR) == LOW) {
      beepNav(); delay(200);
      FastLED.clear(); FastLED.show(); 
      appState = STATE_MENU;
      drawMenu();
    }
  }
  else if (appState == STATE_MENU) {
    if (digitalRead(BUTTON_NEXT) == LOW || digitalRead(BUTTON_PREV) == LOW) { 
      beepNav();
      menuSelection = (menuSelection + 1) % 5; 
      drawMenu(); delay(200); 
    }
    if (digitalRead(BUTTON_COLOR) == LOW) { 
      beepNav();
      if (menuSelection == 1) { gameMode = (gameMode % 3) + 1; } 
      else if (menuSelection == 2) { allowedGuesses = (allowedGuesses == 3) ? 5 : allowedGuesses - 1; }
      else if (menuSelection == 3) { timerSelection = (timerSelection + 1) % 3; }
      drawMenu(); delay(200); 
    }
    if (digitalRead(BUTTON_SUBMIT) == LOW) { 
      beepNav();
      if (menuSelection == 0) initGame(); 
      else if (menuSelection == 1) { gameMode = (gameMode % 3) + 1; drawMenu(); }
      else if (menuSelection == 2) { allowedGuesses = (allowedGuesses == 3) ? 5 : allowedGuesses - 1; drawMenu(); }
      else if (menuSelection == 3) { timerSelection = (timerSelection + 1) % 3; drawMenu(); }
      else if (menuSelection == 4) { appState = STATE_HELP; drawHelpScreen(); }
      delay(200); 
    }
  } 
  else if (appState == STATE_PLAYING) {
    if (digitalRead(BUTTON_PREV) == LOW) { beepNav(); currentCol = (currentCol == 0) ? 3 : currentCol - 1; renderAll(); drawGameStatus(); delay(200); }
    if (digitalRead(BUTTON_NEXT) == LOW) { beepNav(); currentCol = (currentCol + 1) % 4; renderAll(); drawGameStatus(); delay(200); }
    
    if (digitalRead(BUTTON_COLOR) == LOW) { 
      beepColor(); 
      currentGuess[currentCol] = (currentGuess[currentCol] >= NUM_GAME_COLORS - 1) ? 0 : currentGuess[currentCol] + 1; 
      renderAll(); drawGameStatus(); delay(200); 
    }
    
    if (digitalRead(BUTTON_SUBMIT) == LOW) {
      bool ready = true; 
      for(int i=0; i<4; i++) if(currentGuess[i] >= NUM_GAME_COLORS) ready=false;
      
      if (!ready) { 
        beepError();
        display.clearDisplay();
        display.fillRect(0, 0, 128, 64, SH110X_WHITE); 
        display.setTextColor(SH110X_BLACK);
        display.setTextSize(2);
        display.setCursor(16, 16); display.print(F("FILL ALL"));
        display.setCursor(22, 36); display.print(F("COLORS!"));
        display.display();
        delay(800); 
        drawGameStatus(); 
        return; 
      }
      
      playRowSubmit();
      for(int i=0; i<4; i++) guessHistory[currentRow][i] = currentGuess[i];
      calcFeedback(currentRow);
      renderAll();
      
      if (feedbackExact[currentRow] == 4) { 
        isGameWon = true;
        doEndGameTransition(); 
        gameOverTitleX = 128;
        gameOverTitleDir = -1;
        appState = STATE_GAMEOVER; 
        drawGameOver();
      }
      else if (currentRow >= allowedGuesses - 1) { 
        isGameWon = false;
        doEndGameTransition(); 
        gameOverTitleX = 128;
        gameOverTitleDir = -1;
        appState = STATE_GAMEOVER; 
        drawGameOver();
      }
      else { 
        currentRow++; currentCol = 0; 
        for(int i=0; i<4; i++) currentGuess[i]=255; 
        renderAll(); drawGameStatus(); 
      }
      delay(500);
    }
  }
}
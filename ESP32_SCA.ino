#include <Arduino.h>
#include <HardwareSerial.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdarg.h>
#include "mbedtls/aes.h"
#include "aes_sw.h"

// Configuración de puertos serie para ESP32-C3
const int UART1_RX = 20;
const int UART1_TX = 21;

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

const int BUTTON_PIN = 9;   
const int TRIGGER_PIN = 7;  
const int LED_PIN = 8;     

long executionCount = 0;
const unsigned char AES_KEY[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0xB0, 0xB5, 0xFA, 0x57, 0xF0, 0x0d, 0xB0, 0xBA};

const char* options[] = {
  "G(a) - Glitch Auto", "G(m) - Glitch Manual", 
  "SH(a) - AES HW Auto", "SH(m) - AES HW Manual", 
  "SS(a) - AES SW Auto", "SS(m) - AES SW Manual", 
  "PV(m) - Password Vuln Manual", "PV(j) - Password Vuln Jitter", 
  "PS(m) - Password Safe Manual", "PS(j) - Password Safe Jitter"
};

int currentOption = 0;
bool inExercise = false;
bool paused = false;
const char correct_passwd[] = "SCA2026";

// Prototipos
void showMenu();
void loop();
void updateRunningDisplay();
void runSelectedExercise();
int getButtonAction();
int getSerialAction();
void setup();
void checkExit();
void glitchTest();
void printHex(const char* label, const unsigned char* data, int len);
void generateRandomInput(unsigned char* buffer);
void aesHardware();
void aesSoftware();
void myLog(const char* format, ...);
String readSerialLine();
bool processChar(Stream* source, char c, String& line);

void bootMessage() {
  myLog("\n***************************************************\n");
  myLog("* Welcome to the 'ESP32 SCA Multitarget v0.5'     *\n");
  myLog("* Target: ESP32-C3 Mini                           *\n");
  myLog("***************************************************\n");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 15, "ESP32 SCA");
  u8g2.drawStr(0, 28, "Multitarget v0.5");
  u8g2.sendBuffer();
  delay(1500);
}

void showMenu() {
  u8g2.clearBuffer();
  if (currentOption < 2) {
    u8g2.setFont(u8g2_font_open_iconic_embedded_4x_t);
    u8g2.drawGlyph(20, 32, 0x43);
  } else if (currentOption < 4) {
    u8g2.setFont(u8g2_font_open_iconic_www_4x_t);
    u8g2.drawGlyph(20, 32, 0x51);
  } else if (currentOption < 6) {
    u8g2.setFont(u8g2_font_open_iconic_www_4x_t);
    u8g2.drawGlyph(20, 32, 0x48);
  } else {
    u8g2.setFont(u8g2_font_open_iconic_thing_4x_t);
    u8g2.drawGlyph(20, 32, (currentOption < 8) ? 0x43 : 0x4F);
  }
  u8g2.setFont(u8g2_font_5x8_tr);
  const char* modeText = (currentOption < 6) ? 
                        ((currentOption % 2 == 0) ? "(auto)" : "(manual)") :
                        ((currentOption % 2 == 0) ? "(manual)" : "(jitter)");
  int textWidth = u8g2.getStrWidth(modeText);
  u8g2.drawStr((72 - textWidth) / 2, 40, modeText);
  u8g2.sendBuffer();

  myLog("\n--- [ MAIN MENU ] ---\n");
  for (int i = 0; i < 10; i++) {
    if (i == currentOption) myLog("  -> [%d] %s\n", i, options[i]);
    else myLog("     [%d] %s\n", i, options[i]);
  }
  myLog("----------------------\n");
  myLog("Commands: [n] Next, [s] Select, [0-9] Jump to\n");
}

bool processChar(Stream* source, char c, String& line) {
  if (c == '\r' || c == '\n') {
    if (line.length() > 0) { source->print("\r\n"); return true; }
  } else if (c == 8 || c == 127) {
    if (line.length() > 0) { line.remove(line.length() - 1); source->print("\b \b"); }
  } else if (isprint(c)) {
    line += c; source->print(c);
  }
  return false;
}

String readSerialLine() {
  String inputLine = "";
  while (true) {
    if (Serial.available() > 0) {
      if (processChar(&Serial, Serial.read(), inputLine)) return inputLine;
    }
    if (Serial1.available() > 0) {
      if (processChar(&Serial1, Serial1.read(), inputLine)) return inputLine;
    }
    int btn = getButtonAction();
    if (btn == 2) { 
      inExercise = false; 
      showMenu(); 
      return ""; 
    }
    delay(1); yield();
  }
}

void passwordExercise(bool vuln, bool hasJitter) {
  char input_buff[32] = {0};
  myLog("\n[%s] Enter Pass: ", hasJitter ? "JITTER" : "NORMAL");
  
  String input = readSerialLine();
  if (input.length() == 0) return; // readSerialLine ya manejó la salida al menú
  
  input.trim();
  input.toCharArray(input_buff, 32);

  if (hasJitter) delayMicroseconds(esp_random() % 1000);

  digitalWrite(TRIGGER_PIN, HIGH);
  volatile uint8_t result = 0;
  if (vuln) {
    for(uint8_t i = 0; i < 7; i++) {
      if (correct_passwd[i] != input_buff[i]) { result = 1; break; }
    }
  } else {
    for(uint8_t i = 0; i < 7; i++) result |= (correct_passwd[i] ^ input_buff[i]);
  }
  digitalWrite(TRIGGER_PIN, LOW);

  if (result == 0) {
    myLog(" ACCESS GRANTED\n");
    digitalWrite(LED_PIN, LOW); 
    myLog("--- [PRESS BUTTON OR ENTER TO CONTINUE] ---\n");
    bool waiting = true;
    while(waiting) {
      int btn = getButtonAction();
      if (btn == 1) waiting = false;
      if (btn == 2) { 
        inExercise = false; 
        showMenu(); 
        waiting = false; 
      }
      if (Serial.available() > 0) { Serial.read(); waiting = false; }
      if (Serial1.available() > 0) { Serial1.read(); waiting = false; }
      delay(1); yield();
    }
    digitalWrite(LED_PIN, HIGH); 
  } else {
    myLog(" ACCESS DENIED\n");
  }

  while(Serial.available() > 0) Serial.read();
  while(Serial1.available() > 0) Serial1.read();
}

int getButtonAction() {
  static unsigned long pressStart = 0;
  static bool lastState = HIGH;
  bool currentState = digitalRead(BUTTON_PIN);
  int result = 0;
  if (currentState == LOW && lastState == HIGH) pressStart = millis();
  else if (currentState == HIGH && lastState == LOW) {
    unsigned long duration = millis() - pressStart;
    if (duration > 1500) result = 2;
    else if (duration > 50) result = 1; 
  }
  lastState = currentState;
  return result;
}

int getSerialAction() {
  Stream* source = NULL;
  if (Serial.available()) source = &Serial;
  else if (Serial1.available()) source = &Serial1;
  if (source) {
    char c = source->read();
    if (inExercise) {
      if (currentOption < 6 && (c == 'n' || c == 'N')) return 1;
      if (c == 's' || c == 'S') return 2;
      return 0; 
    }
    if (c == 'n' || c == 'N') return 1;
    if (c == 's' || c == 'S') return 2;
    if (c >= '0' && c <= '9') { currentOption = c - '0'; showMenu(); return 0; }
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, UART1_RX, UART1_TX);  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); 
  Wire.begin(5, 6); 
  u8g2.begin();
  delay(100);
  bootMessage();
  showMenu();
}

void myLog(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
  String msg = String(buffer);
  msg.replace("\n", "\r\n");
  Serial1.print(msg);
}

void loop() {
  int action = getButtonAction();
  if (action == 0) action = getSerialAction();

  if (!inExercise) {
    if (action == 1) { currentOption = (currentOption + 1) % 10; showMenu(); } 
    else if (action == 2) { 
      inExercise = true; executionCount = 0; 
      myLog("\n>>> STARTING: %s\n", options[currentOption]);
      myLog(">>> Long press button (>1.5s) to return to menu\n");
      updateRunningDisplay(); delay(300); 
    }
  } else {
    if (action == 2) { 
      inExercise = false; paused = false; 
      showMenu(); delay(500); return; 
    }

    if (currentOption >= 6) {
      // SOLO actualizamos si seguimos en el ejercicio
      executionCount++; 
      // Si la función interna salió al menú, inExercise será false
      runSelectedExercise(); 
      if (inExercise) updateRunningDisplay(); 
    } else {
      bool isManual = (currentOption % 2 == 1);
      if (isManual) {
        if (action == 1) { executionCount++; runSelectedExercise(); updateRunningDisplay(); }
      } else {
        if (action == 1) { paused = !paused; myLog(paused ? ">> PAUSED\n" : ">> RESUMED\n"); }
        if (!paused) {
          executionCount++; runSelectedExercise();
          if (inExercise && executionCount % 10 == 0) updateRunningDisplay();
          delay(100);
        }
      }
    }
  }
}

void updateRunningDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(5, 12, "RUNNING...");
  u8g2.drawStr(5, 24, options[currentOption]);
  u8g2.setCursor(5, 38);
  u8g2.print("Execs: "); u8g2.print(executionCount);
  u8g2.sendBuffer();
}

void runSelectedExercise() {
  switch (currentOption) {
    case 0: case 1: glitchTest(); break;
    case 2: case 3: aesHardware(); break;
    case 4: case 5: aesSoftware(); break;
    case 6: passwordExercise(true, false); break;
    case 7: passwordExercise(true, true); break;
    case 8: passwordExercise(false, false); break;
    case 9: passwordExercise(false, true); break;
  }
}

void glitchTest() {
  digitalWrite(TRIGGER_PIN, HIGH);
  volatile int i, j, ctr = 0;
  bool glitch = false;
  for (i = 0; i < 500; i++) {
    for (j = 0; j < 500; j++) if (j % 100 == 0) ctr++;
    if (j != 500) { glitch = true; break; }
  }
  digitalWrite(TRIGGER_PIN, LOW);
  if (glitch || i != 500 || ctr != 2500) {
    myLog("Glitch: i=%d, j=%d, ctr=%d\n", i, j, ctr);
    u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "GLITCH!"); u8g2.setCursor(0, 30);
    u8g2.print("CTR:"); u8g2.print(ctr); u8g2.sendBuffer();
    delay(2000);
  } else myLog("OK -> i:%d j:%d ctr:%d\n", i, j, ctr);
}

void printHex(const char* label, const unsigned char* data, int len) {
  myLog(label);
  for (int i = 0; i < len; i++) myLog("%02X", data[i]);
  myLog("\n");
}

void generateRandomInput(unsigned char* buffer) {
  uint32_t r1 = esp_random(), r2 = esp_random(), r3 = esp_random(), r4 = esp_random();
  memcpy(buffer, &r1, 4); memcpy(buffer+4, &r2, 4); memcpy(buffer+8, &r3, 4); memcpy(buffer+12, &r4, 4);
}

void aesHardware() {
  unsigned char input[16], output[16]; generateRandomInput(input);
  mbedtls_aes_context aes; mbedtls_aes_init(&aes);
  digitalWrite(TRIGGER_PIN, HIGH);
  mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, output);
  digitalWrite(TRIGGER_PIN, LOW);
  printHex("[AES-HW] Out:", output, 16);
  mbedtls_aes_free(&aes);
}

void aesSoftware() {
  unsigned char input[16], output[16]; generateRandomInput(input);
  digitalWrite(TRIGGER_PIN, HIGH);
  AES128_ECB_encrypt(input, (uint8_t*)AES_KEY, output);
  digitalWrite(TRIGGER_PIN, LOW);
  printHex("[AES-SW] Out:", output, 16);
}

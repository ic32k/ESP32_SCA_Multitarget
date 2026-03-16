#include "aes_sw.h"
#include "mbedtls/aes.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// Intentaremos con la configuración más común para esta placa específica
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

const int BUTTON_PIN = 9;
const int TRIGGER_PIN = 7;

long executionCount = 0;

const unsigned char AES_KEY[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE,
                                   0xBA, 0xBE, 0xB0, 0xB5, 0xFA, 0x57,
                                   0xF0, 0x0d, 0xB0, 0xBA};

const char *options[] = {"G(a)",  "G(m)",  "SH(a)", "SH(m)", "SS(a)",
                         "SS(m)", "PV(m)", "PV(j)", "PS(m)", "PS(j)"};
int currentOption = 0;
bool inExercise = false;
bool paused = false;

const char correct_passwd[] = "SCA2026";

void printSerialMenu();
void myLog(const char *format, ...);
void loop();
void updateRunningDisplay();
void runSelectedExercise();
int getButtonAction();
void setup();
void checkExit();
void glitchTest();
void printHex(const char *label, const unsigned char *data, int len);
void generateRandomInput(unsigned char *buffer);
void aesHardware();
void aesSoftware();

void bootMessage() {
  // Mensaje por Serial
  myLog("\n***************************************************\n");
  myLog("* Welcome to the 'ESP32 SCA Multitarget v0.5'     *\n");
  myLog("* Please choose an option and have fun!!!         *\n");
  myLog("***************************************************\n");
  uint32_t freq = getCpuFrequencyMhz();
  Serial.print("CPU Frequency: ");
  Serial.print(freq);
  Serial.println(" MHz");

  // Mensaje en Pantalla OLED
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 15, "ESP32 SCA");
  u8g2.drawStr(0, 28, "Multitarget v0.5");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 40, "Initializing...");
  u8g2.sendBuffer();

  delay(2000); // Pausa de 2 segundos para que se pueda leer
}

void printSerialMenu() {
  myLog("\n***************************************************\n");
  myLog("*             AVAILABLE EXERCISES                 *\n");
  myLog("***************************************************\n");
  for (int i = 0; i < 10; i++) {
    myLog("  [%2d] %s\n", i + 1, options[i]);
  }
  myLog("***************************************************\n");
  myLog("Select exercise (1-10): ");
}

void showMenu() {
  u8g2.clearBuffer();

  // --- SECCIÓN DE ICONOS ---
  if (currentOption < 2) {
    // GLITCH: Fuente EMBEDDED
    u8g2.setFont(u8g2_font_open_iconic_embedded_4x_t);
    u8g2.drawGlyph(20, 32, 0x43); // Rayo
  } else if (currentOption < 4) {
    // AES HARDWARE (SH): Fuente WWW
    u8g2.setFont(u8g2_font_open_iconic_www_4x_t);
    u8g2.drawGlyph(20, 32, 0x51); // Ondas/WiFi
  } else if (currentOption < 6) {
    // AES SOFTWARE (SS): Fuente WWW
    u8g2.setFont(u8g2_font_open_iconic_www_4x_t);
    u8g2.drawGlyph(20, 32, 0x48); // Engranaje
  } else {
    // PASSWORDS (PV/PS): Fuente THING
    u8g2.setFont(u8g2_font_open_iconic_thing_4x_t);
    if (currentOption < 8) {
      u8g2.drawGlyph(20, 32, 0x43); // Llave
    } else {
      u8g2.drawGlyph(20, 32, 0x4F); // Candado
    }
  }

  // --- SECCIÓN DE TEXTO MODO ---
  u8g2.setFont(u8g2_font_5x8_tr);
  const char *modeText;

  if (currentOption < 6) {
    modeText = (currentOption % 2 == 0) ? "(auto)" : "(manual)";
  } else {
    modeText = (currentOption % 2 == 0) ? "(manual)" : "(jitter)";
  }

  int textWidth = u8g2.getStrWidth(modeText);
  u8g2.drawStr((72 - textWidth) / 2, 40, modeText);

  u8g2.sendBuffer();
}

void passwordExercise(bool vuln, bool hasJitter) {
  char input_buff[32] = {0};
  myLog("\n[%s] Enter Pass: ", hasJitter ? "JITTER" : "NORMAL");

  while (Serial.available() == 0) {
    if (digitalRead(BUTTON_PIN) == LOW)
      return;
  }

  String input = Serial.readStringUntil('\n');
  input.trim();
  input.toCharArray(input_buff, 32);

  // --- CONTRAMEDIDA: JITTER ---
  if (hasJitter) {
    uint32_t jitter = esp_random() % 1000; // 0-1ms de ruido
    delayMicroseconds(jitter);
  }

  digitalWrite(TRIGGER_PIN, HIGH);

  volatile uint8_t result = 0;

  if (vuln) {
    // VULNERABLE: Salida temprana (Timing Attack)
    for (uint8_t i = 0; i < 7; i++) {
      if (correct_passwd[i] != input_buff[i]) {
        result = 1;
        break;
      }
    }
  } else {
    // SEGURO: Tiempo Constante
    for (uint8_t i = 0; i < 7; i++) {
      result |= (correct_passwd[i] ^ input_buff[i]);
    }
  }

  digitalWrite(TRIGGER_PIN, LOW);

  myLog(result == 0 ? " ACCESS GRANTED\n" : " ACCESS DENIED\n");
}

void loop() {
  int action = getButtonAction();

  if (!inExercise) {
    if (action == 1) { // Navegar
      currentOption = (currentOption + 1) % 10;
      showMenu();
      myLog("Seleccionado: %s\n", options[currentOption]);
    } else if (action == 2) { // Entrar
      inExercise = true;
      paused = false; // Resetear pausa al entrar
      executionCount = 0;
      updateRunningDisplay();
      myLog("Entrando en ejercicio: %s\n", options[currentOption]);
      delay(500);
    }

    // --- SELECCIÓN POR SERIAL (NO BLOQUEANTE) ---
    static String serialBuffer = "";
    while (Serial.available() > 0 || Serial1.available() > 0) {
      char c = (Serial.available() > 0) ? Serial.read() : Serial1.read();

      if (c == '\n' || c == '\r') {
        if (serialBuffer.length() > 0) {
          int val = serialBuffer.toInt();
          serialBuffer = ""; // Limpiar
          if (val >= 1 && val <= 10) {
            currentOption = val - 1;
            inExercise = true;
            paused = false; // Resetear pausa al entrar
            executionCount = 0;
            updateRunningDisplay();
            myLog("\n[SERIAL] Seleccionado ejercicio: %d - %s\n", val,
                  options[currentOption]);
            return;
          } else {
            myLog("\n[!] Opcion invalida: %d. Elige entre 1 y 10.\n", val);
            printSerialMenu();
          }
        }
      } else if (isdigit(c)) {
        serialBuffer += c;
      } else if (c == ' ' || c == '\b') {
        // Ignorar espacios o retrocesos
      } else {
        // Si llega cualquier otro caracter (ruido), limpiamos el buffer para
        // evitar basura
        serialBuffer = "";
      }
    }
  } else {
    // SALIR del ejercicio (Pulsación larga)
    if (action == 2) {
      inExercise = false;
      showMenu();
      printSerialMenu();
      delay(500);
      return;
    }

    if (currentOption >= 6) {
      // --- MODO PASSWORD ---
      // IMPORTANTE: Quitamos el "if (action == 1)".
      // Ahora entra directo y se queda esperando en readPassword()
      runSelectedExercise();
    } else {
      // --- MODO GLITCH / AES --- (Mantenemos tu lógica que funciona bien)
      bool isManual = (currentOption % 2 != 0);
      if (isManual) {
        if (action == 1) {
          executionCount++;
          runSelectedExercise();
          updateRunningDisplay();
        }
      } else {
        if (action == 1) {
          paused = !paused;
          myLog(paused ? ">> PAUSADO\n" : ">> CONTINUAR\n");
        }
        if (!paused) {
          executionCount++;
          runSelectedExercise();
          if (executionCount % 10 == 0)
            updateRunningDisplay();
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

  // Imprimir el contador de ejecuciones debajo
  u8g2.setCursor(5, 38);
  u8g2.print("Execs: ");
  u8g2.print(executionCount);

  u8g2.sendBuffer();
}

void runSelectedExercise() {
  switch (currentOption) {
  case 0:
  case 1:
    glitchTest();
    break;
  case 2:
  case 3:
    aesHardware();
    break;
  case 4:
  case 5:
    aesSoftware();
    break;
  case 6:
    passwordExercise(true, false);
    break; // PV manual
  case 7:
    passwordExercise(true, true);
    break; // PV jitter
  case 8:
    passwordExercise(false, false);
    break; // PS manual
  case 9:
    passwordExercise(false, true);
    break; // PS jitter
  }
}

// Asegúrate de que esta función esté en tu código para detectar los tipos de
// pulsación
int getButtonAction() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long start = millis();
    while (digitalRead(BUTTON_PIN) == LOW)
      ; // Esperar a soltar
    unsigned long duration = millis() - start;
    if (duration > 1000)
      return 2; // Largo
    if (duration > 50)
      return 1; // Corto
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(3, INPUT); // Forzamos el Pin 3 como entrada (RX)
  Serial1.setRxBufferSize(256);
  Serial1.begin(115200, SERIAL_8N1, 3, 2);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIGGER_PIN, OUTPUT);

  // IMPORTANTE: Definir pines SDA y SCL antes de iniciar u8g2
  // Si con 5 y 6 no funciona, prueba con 18 y 19
  Wire.begin(5, 6);

  u8g2.begin();
  delay(100); // Dar tiempo al OLED para estabilizarse
  bootMessage();

  showMenu();
  printSerialMenu();
}

void myLog(const char *format, ...) {
  char loc_res[128];
  va_list arg;
  va_start(arg, format);
  vsnprintf(loc_res, sizeof(loc_res), format, arg);
  va_end(arg);

  // Recorremos la cadena y si encontramos \n, enviamos \r antes
  for (int i = 0; loc_res[i] != '\0'; i++) {
    if (loc_res[i] == '\n') {
      Serial.print('\r');
      Serial1.print('\r');
    }
    Serial.print(loc_res[i]);
    Serial1.print(loc_res[i]);
  }
}

void checkExit() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long start = millis();
    while (digitalRead(BUTTON_PIN) == LOW)
      ;
    if (millis() - start > 1000) {
      inExercise = false;
      showMenu();
      printSerialMenu();
      delay(500);
    }
  }
}

void glitchTest() {
  digitalWrite(TRIGGER_PIN, LOW);

  // Usamos volatile para que el compilador no "entienda" el truco y lo borre
  volatile int i, j;
  volatile int ctr = 0;
  bool glitch_detected = false;
  digitalWrite(TRIGGER_PIN, HIGH);
  for (i = 0; i < 500; i++) {
    for (j = 0; j < 500; j++) {
      // if (j % 100 == 0) {
      ctr++;
      //}
    }
    if (j != 500) {
      glitch_detected = true;
      break; // Salimos para reportar el fallo
    }
  }

  digitalWrite(TRIGGER_PIN,
               LOW); // El TRIGGER_PIN baja justo al terminar el proceso crítico

  if (glitch_detected || i != 500 || ctr != 250000) {
    myLog("Glitch:\n i=%d, j=%d, ctr=%d\n", i, j, ctr);

    // Feedback visual en la pantalla OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "GLITCH!");
    u8g2.setCursor(0, 30);
    u8g2.print("CTR:");
    u8g2.print(ctr);
    u8g2.sendBuffer();

    delay(2000);
  } else {
    // Solo loguear cada 100 veces si es modo automático para no saturar el
    // Serial
    bool isManual = (currentOption % 2 != 0);
    myLog("OK -> i:%d j:%d ctr:%d (Exec: %ld)\n", i, j, ctr, executionCount);
  }
}

void printHex(const char *label, const unsigned char *data, int len) {
  myLog(label);
  for (int i = 0; i < len; i++) {
    myLog("%02X", data[i]);
  }
  myLog("\n");
}

void generateRandomInput(unsigned char *buffer) {
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint32_t r3 = esp_random();
  uint32_t r4 = esp_random();
  memcpy(buffer, &r1, 4);
  memcpy(buffer + 4, &r2, 4);
  memcpy(buffer + 8, &r3, 4);
  memcpy(buffer + 12, &r4, 4);
}

// --- Funciones de Cifrado ---

void aesHardware() {
  digitalWrite(TRIGGER_PIN, LOW);
  unsigned char input[16];
  unsigned char output[16];
  generateRandomInput(input);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  // myLog("[AES-HW]:\n");
  // printHex("Key: ", AES_KEY, 16);
  // printHex("In:  ", input, 16);

  mbedtls_aes_setkey_enc(
      &aes, AES_KEY, 128); //  ret = mbedtls_aes_setkey_enc(&aes, AES_KEY, 128)
  // if (ret != 0) myLog("[DEBUG] Error setkey: %d\n", ret);

  digitalWrite(TRIGGER_PIN, HIGH);

  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input,
                        output); // ret = mbedtls_aes_crypt_cbc(&aes,
                                 // MBEDTLS_AES_ENCRYPT, input, output);

  digitalWrite(TRIGGER_PIN, LOW);

  // if (ret == 0) {
  printHex("[AES-HW]:", output, 16);
  // myLog("\n");
  //} else {
  //  myLog("[!!!] ERROR en cifrado HW: %d\n", ret);
  //}

  mbedtls_aes_free(&aes);
}

void aesSoftware() {
  digitalWrite(TRIGGER_PIN, LOW);
  unsigned char input[16];
  unsigned char output[16];
  generateRandomInput(input);
  memcpy(output, input, 16);

  // myLog("--- [AES SW TINY START] ---\n");
  // printHex("Key: ", AES_KEY, 16);
  // printHex("In:  ", input, 16);

  digitalWrite(TRIGGER_PIN, HIGH);

  AES128_ECB_encrypt(input, (uint8_t *)AES_KEY, output);

  digitalWrite(TRIGGER_PIN, LOW);

  printHex("[AES-SW]:", output, 16);
  // myLog("\n");
}

bool readPassword(char *buffer, size_t maxSize) {
  size_t index = 0;

  // Limpieza total antes de empezar
  while (Serial.available())
    Serial.read();
  while (Serial1.available())
    Serial1.read();

  myLog("\n[UART READY] Escribe el password en Tera Term:\n");

  while (inExercise) {
    // 1. REVISIÓN AGRESIVA DE SERIAL
    if (Serial1.available() > 0 || Serial.available() > 0) {
      char c = (Serial1.available() > 0) ? Serial1.read() : Serial.read();

      // ECO INMEDIATO (Para confirmar que el chip te oye)
      if (Serial1)
        Serial1.print(c);
      Serial.print(c);

      if (c == '\r' || c == '\n') {
        if (index > 0) {
          buffer[index] = '\0';
          return true;
        }
      } else if (index < maxSize - 1 && c >= 32) {
        buffer[index++] = c;
      }
    }

    // 2. REVISIÓN MANUAL DEL BOTÓN (Sin funciones complejas)
    // Usamos digitalRead directamente para no bloquear la UART
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(500); // Pequeño debounce
      inExercise = false;
      return false;
    }

    // 3. DAR RESPIRACIÓN AL SISTEMA
    delay(1);
  }
  return false;
}

void passwordVulnerable() {
  digitalWrite(TRIGGER_PIN, LOW);
  char input_buff[32] = {0};
  myLog("\n[VULNERABLE] Enter Pass: ");

  // Se queda aquí bloqueado hasta que pulses Enter en Tera Term
  if (readPassword(input_buff, 32)) {
    // Una vez recibido el password completo, ejecutamos el trigger
    digitalWrite(TRIGGER_PIN, HIGH);
    uint8_t bad = 0;
    for (uint8_t i = 0; i < sizeof(correct_passwd) - 1; i++) {
      if (correct_passwd[i] != input_buff[i]) {
        bad = 1;
        break;
      }
    }
    digitalWrite(TRIGGER_PIN, LOW);
    myLog(bad ? " >>> ACCESS DENIED\n" : " >>> ACCESS GRANTED\n");

    // Pequeña pausa para ver el resultado antes de pedir otro
    delay(500);
  } else {
    // Si readPassword devolvió false es porque se pulsó el botón
    inExercise = false;
    showMenu();
    printSerialMenu();
  }
}

void passwordSecure() {
  digitalWrite(TRIGGER_PIN, LOW);
  char input_buff[32] = {0};
  myLog("\n[SECURE] Enter Pass: ");

  // Espera entrada completa (Enter) o cancelación por botón
  if (readPassword(input_buff, 32)) {

    // --- INICIO ZONA CRÍTICA SCA ---
    digitalWrite(TRIGGER_PIN, HIGH);

    volatile uint8_t diff = 0;
    // Comparamos SIEMPRE 7 iteraciones para mantener el tiempo constante
    for (uint8_t i = 0; i < 7; i++) {
      // XOR: Si los caracteres son iguales, el resultado es 0.
      // OR: Si alguna vez hay una diferencia, 'diff' dejará de ser 0 para
      // siempre.
      diff |= (correct_passwd[i] ^ input_buff[i]);
    }

    // Verificamos también que la longitud sea la correcta (7 caracteres)
    // Usamos una operación lógica simple para no añadir ramas de ejecución
    // (ifs)
    if (strlen(input_buff) != 7)
      diff |= 1;

    digitalWrite(TRIGGER_PIN, LOW);
    // --- FIN ZONA CRÍTICA SCA ---

    myLog(diff ? " >>> ACCESS DENIED\n" : " >>> ACCESS GRANTED\n");
    delay(500);

  } else {
    // Si se pulsó el botón en readPassword, volvemos al menú
    inExercise = false;
    showMenu();
    printSerialMenu();
  }
}

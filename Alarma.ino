// ═══════════════════════════════════════════════════════════════
//  Alarma.ino — Merge completo
// ═══════════════════════════════════════════════════════════════

// ─── LIBRERÍAS ───────────────────────────────────────────────
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "time.h"
#include "BluetoothSerial.h"
#include <DFRobotDFPlayerMini.h>

// ─── PINES ───────────────────────────────────────────────────
#define BTNUP_PIN     19
#define BTNDOWN_PIN   21
#define BTNSEL_PIN    25
#define PUZ_UP_PIN    32
#define PUZ_DOWN_PIN  33
#define PUZ_LEFT_PIN  34
#define PUZ_RIGHT_PIN 35
#define PUZ_SEL_PIN   26
#define DF_RX_PIN     16
#define DF_TX_PIN     17

// ─── CONSTANTES ──────────────────────────────────────────────
#define NAV_COOLDOWN_MS   100
#define DEBOUNCE_MS       200
#define SCREEN_WIDTH      320
#define SCREEN_HEIGHT     240
#define GRID_COLS         3
#define GRID_ROWS         3
#define GRID_CELL_SIZE    60
#define GRID_OFFSET_X     20
#define GRID_OFFSET_Y     40
#define PATTERN_LENGTH    3
#define SHOW_PATTERN_MS   2500
#define ALARM_HOUR        7
#define ALARM_MINUTE      0

const char* ntpServer        = "pool.ntp.org";
const long  gmtOffset_sec    = -5 * 3600;
const int   daylightOffset_sec = 0;

// ─── ENUMERACIONES ───────────────────────────────────────────
typedef enum : uint8_t {
  STATE_MENU,
  STATE_WF,
  STATE_BT,
  STATE_ALARMCONF,
  STATE_ALARM_ACTIVE,
  STATE_SHOW_PATTERN,
  STATE_WAIT_INPUT,
  STATE_SUCCESS,
  STATE_FAIL,
} ProgrammState;

typedef enum : uint8_t {
  RENDER_MENU,
  RENDER_WIFI_CONECTANDO,
  RENDER_WIFI_CONECTADO,
  RENDER_CONECTANDO_BT,
  RENDER_BT_CONECTADO,
  RENDER_BT_SSID,
  RENDER_BT_PASSWORD,
  RENDER_ALARM_ACTIVE,
  RENDER_PUZZLE_INIT,
  RENDER_SHOW_PATTERN,
  RENDER_HIDE_PATTERN,
  RENDER_PUZZLE_CURSOR,
  RENDER_SUCCESS,
  RENDER_FAIL,
} RenderCommand;

// ─── ESTRUCTURA GLOBAL ───────────────────────────────────────
typedef struct {
  ProgrammState currentState;
  int  menuSelection;
  bool Bluethoot;
  bool wifi;
  bool Conectando;
  int  hora;
  int  minuto;
  int  LastTiempotranscurrido;
  bool RepCancion;
  bool BTinit;
  int  pattern[PATTERN_LENGTH];
  int  userInput[PATTERN_LENGTH];
  int  inputCount;
  int  cursorX;
  int  cursorY;
  bool alarmTriggered;
} AppData;

// ─── VARIABLES GLOBALES ──────────────────────────────────────
volatile AppData gd;
DFRobotDFPlayerMini myDFPlayer;
HardwareSerial dfSerial(2);
BluetoothSerial SerialBT;
TFT_eSPI tft = TFT_eSPI();
String SSID     = "";
String password = "";
String buffer   = "";

// ─── FREERTOS ────────────────────────────────────────────────
TaskHandle_t      taskHandle  = NULL;
SemaphoreHandle_t stateMutex  = NULL;
QueueHandle_t     renderQueue = NULL;

// ─── FORWARD DECLARATIONS ────────────────────────────────────
void logicTask(void *p);
void drawTask(void *p);
void handleMenu();
void handleWifi();
void handleBluethoot();
void handleAlarmActive();
void handleShowPattern();
void handlePuzzleInput();
void handleSuccess();
void handleFail();
void generatePattern();
bool checkPattern();
void DrawMenu(int sel);
void DrawWifi(int etapa);
void DrawBT(int fase);
void DrawAlarmActive();
void drawPuzzleGrid();
void showPattern();
void hidePattern();
void drawCursor(int col, int fila);
void updatePuzzleCursor();
void DrawSuccess();
void DrawFail();
void sendRenderCommand(RenderCommand cmd);
int  Tiempotranscurrido();

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("[BOOT] Iniciando sistema...");

  tft.init();
  tft.setRotation(1);

  // Valores iniciales
  gd.currentState           = STATE_MENU;
  gd.menuSelection          = 0;
  gd.wifi                   = false;
  gd.Bluethoot              = false;
  gd.Conectando             = false;
  gd.BTinit                 = false;
  gd.LastTiempotranscurrido = 0;
  gd.alarmTriggered         = false;
  gd.inputCount             = 0;
  gd.cursorX                = 1;
  gd.cursorY                = 1;

  // DFPlayer
  dfSerial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
  delay(1000);
  if (!myDFPlayer.begin(dfSerial)) {
    Serial.println("[DFPlayer] ERROR: No se pudo inicializar.");
  } else {
    Serial.println("[DFPlayer] Inicializado OK.");
    myDFPlayer.volume(20);
    myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  }

  // FreeRTOS
  renderQueue = xQueueCreate(5, sizeof(RenderCommand));
  stateMutex  = xSemaphoreCreateMutex();

  // Botones menú
  pinMode(BTNUP_PIN,   INPUT_PULLUP);
  pinMode(BTNDOWN_PIN, INPUT_PULLUP);
  pinMode(BTNSEL_PIN,  INPUT_PULLUP);

  // Botones puzzle
  pinMode(PUZ_UP_PIN,    INPUT_PULLUP);
  pinMode(PUZ_DOWN_PIN,  INPUT_PULLUP);
  pinMode(PUZ_LEFT_PIN,  INPUT_PULLUP);
  pinMode(PUZ_RIGHT_PIN, INPUT_PULLUP);
  pinMode(PUZ_SEL_PIN,   INPUT_PULLUP);

  xTaskCreatePinnedToCore(logicTask, "LogicTask", 8192, NULL, 2, NULL,        1);
  xTaskCreatePinnedToCore(drawTask,  "DrawTask",  8192, NULL, 1, &taskHandle, 0);

  sendRenderCommand(RENDER_MENU);
}

void loop() {}

// ═══════════════════════════════════════════════════════════════
//  AUDIO
// ═══════════════════════════════════════════════════════════════
void startAlarmSound() {
  Serial.println("[DFPlayer] Reproduciendo alarma...");
  myDFPlayer.loop(1);
}

void stopAlarmSound() {
  Serial.println("[DFPlayer] Deteniendo audio.");
  myDFPlayer.stop();
}

// ═══════════════════════════════════════════════════════════════
//  LOGIC TASK
// ═══════════════════════════════════════════════════════════════
void logicTask(void *p) {
  for (;;) {
    ProgrammState state;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      state = gd.currentState;
      xSemaphoreGive(stateMutex);
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    switch (state) {
      case STATE_MENU:         handleMenu();         break;
      case STATE_WF:           handleWifi();         break;
      case STATE_BT:           handleBluethoot();    break;
      case STATE_ALARMCONF:                          break;
      case STATE_ALARM_ACTIVE: handleAlarmActive();  break;
      case STATE_SHOW_PATTERN: handleShowPattern();  break;
      case STATE_WAIT_INPUT:   handlePuzzleInput();  break;
      case STATE_SUCCESS:      handleSuccess();      break;
      case STATE_FAIL:         handleFail();         break;
      default: vTaskDelay(pdMS_TO_TICKS(10));        break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  DRAW TASK
// ═══════════════════════════════════════════════════════════════
void drawTask(void *p) {
  struct tm timeinfo;
  RenderCommand cmd;
  static int lastSel = -1;

  for (;;) {
    if (xQueueReceive(renderQueue, &cmd, portMAX_DELAY)) {
      switch (cmd) {

        case RENDER_MENU: {
          int sel     = 0;
          int horaN   = -1;
          int minutoN = -1;
          if (gd.wifi) {
            if (getLocalTime(&timeinfo)) {
              horaN   = timeinfo.tm_hour;
              minutoN = timeinfo.tm_min;
            }
          }
          if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            sel = gd.menuSelection;
            xSemaphoreGive(stateMutex);
          }
          if (sel != lastSel || (gd.wifi && (horaN != gd.hora || minutoN != gd.minuto))) {
            if (gd.wifi) {
              gd.hora   = horaN;
              gd.minuto = minutoN;
            }
            DrawMenu(sel);
            lastSel = sel;
          }
          break;
        }

        case RENDER_WIFI_CONECTANDO: DrawWifi(0);          break;
        case RENDER_WIFI_CONECTADO:  DrawWifi(1);          break;
        case RENDER_CONECTANDO_BT:   DrawBT(0);            break;
        case RENDER_BT_SSID:         DrawBT(1);            break;
        case RENDER_BT_PASSWORD:     DrawBT(2);            break;
        case RENDER_ALARM_ACTIVE:    DrawAlarmActive();    break;
        case RENDER_PUZZLE_INIT:     drawPuzzleGrid();     break;
        case RENDER_SHOW_PATTERN:    showPattern();        break;
        case RENDER_HIDE_PATTERN:    hidePattern();        break;
        case RENDER_PUZZLE_CURSOR:   updatePuzzleCursor(); break;
        case RENDER_SUCCESS:         DrawSuccess();        break;
        case RENDER_FAIL:            DrawFail();           break;
        default: break;
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  HANDLERS
// ═══════════════════════════════════════════════════════════════
void handleMenu() {
  struct tm timeinfo;
  static bool     navLocked   = false;
  static uint32_t navLockTime = 0;
  int horaN   = -1;
  int minutoN = -1;
  bool changed = false;

  // Actualizar hora cada 20s
  if (Tiempotranscurrido() > 20000) {
    gd.LastTiempotranscurrido = millis();
    if (gd.wifi) {
      Serial.println("Ya hay internet mano");
      if (getLocalTime(&timeinfo)) {
        horaN   = timeinfo.tm_hour;
        minutoN = timeinfo.tm_min;
              Serial.println(horaN);
      }
    }
  }

  // Chequear alarma y actualizar hora en gd
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {

    // ← Guardar hora en gd y marcar changed
    if (horaN != -1 && minutoN -1 && (horaN != gd.hora || minutoN != gd.minuto)) {
      gd.hora   = horaN;    // ← esto faltaba
      gd.minuto = minutoN;  // ← esto faltaba
      changed   = true;
    }

    if (gd.wifi && !gd.alarmTriggered &&
        gd.hora == ALARM_HOUR && gd.minuto == ALARM_MINUTE) {
      gd.alarmTriggered = true;
      gd.currentState   = STATE_ALARM_ACTIVE;
      xSemaphoreGive(stateMutex);
      startAlarmSound();
      sendRenderCommand(RENDER_ALARM_ACTIVE);
      return;
    }
    xSemaphoreGive(stateMutex);
  }

  // Navegación
  if (navLocked && (millis() - navLockTime > NAV_COOLDOWN_MS))
    navLocked = false;

  if (!navLocked) {
    navLocked   = true;
    navLockTime = millis();

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (!digitalRead(BTNUP_PIN) && gd.menuSelection < 2) {
        gd.menuSelection++;
        changed = true;
        Serial.println("arriba");
      } else if (!digitalRead(BTNDOWN_PIN) && gd.menuSelection > 0) {
        gd.menuSelection--;
        changed = true;
        Serial.println("abajo");
      } else if (!digitalRead(BTNSEL_PIN)) {
        switch (gd.menuSelection) {
          case 0: gd.currentState = STATE_ALARMCONF; break;
          case 1: gd.currentState = STATE_WF;        break;
          case 2: gd.currentState = STATE_BT;        break;
        }
      }
      xSemaphoreGive(stateMutex);
    }
  }

  if (changed) sendRenderCommand(RENDER_MENU);
  vTaskDelay(pdMS_TO_TICKS(50));
}
void handleWifi() {
  static bool yaConectado  = false;
  static bool yaIniciado   = false;
  static int  intentos     = 0;

  // ── Ya conectado ──
  if (gd.wifi) {
    if (!yaConectado) {
      yaConectado = true;
      sendRenderCommand(RENDER_WIFI_CONECTADO);
      // ← Configurar NTP aquí, cuando ya hay conexión
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      Serial.println("NTP configurado, sincronizando hora...");

      // Esperar a que la hora se sincronice
      struct tm timeinfo;
      int intentosNTP = 0;
      while (!getLocalTime(&timeinfo) && intentosNTP < 10) {
        Serial.println("Esperando hora NTP...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        intentosNTP++;
      }

      if (getLocalTime(&timeinfo)) {
        Serial.println("Hora sincronizada!");
        Serial.print("Hora actual: ");
        Serial.print(timeinfo.tm_hour);
        Serial.print(":");
        Serial.println(timeinfo.tm_min);
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          gd.hora   = timeinfo.tm_hour;
          gd.minuto = timeinfo.tm_min;
          xSemaphoreGive(stateMutex);
        }
      } else {
        Serial.println("No se pudo sincronizar la hora");
      }

      vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_MENU);
    yaConectado = false;
    yaIniciado  = false;
    intentos    = 0;
    return;
  }

  // ── Sin credenciales ──
  if (SSID == "" || password == "") {
    Serial.println("Sin credenciales, volviendo al menu");
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_MENU);
    return;
  }

  // ── Llamar WiFi.begin UNA SOLA VEZ ──
  if (!yaIniciado) {
    yaIniciado = true;
    intentos   = 0;
    Serial.println("WiFi.begin UNA SOLA VEZ");
    Serial.println("SSID: "     + SSID);
    Serial.println("Password: " + password);
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.begin(SSID.c_str(), password.c_str());
    sendRenderCommand(RENDER_WIFI_CONECTANDO);
  }

  // ── Verificar estado ──
  int status = WiFi.status();
  intentos++;
  Serial.print("Intento "); Serial.print(intentos);
  Serial.print(" — status: "); Serial.println(status);

  if (status == WL_CONNECTED) {
    Serial.println("Conexion detectada!");
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.wifi = true;
      xSemaphoreGive(stateMutex);
    }
    return;
  }

  // ── Timeout ──
  if (intentos >= 20) {
    Serial.println("Timeout — volviendo al menu");
    WiFi.disconnect();
    yaIniciado = false;
    intentos   = 0;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_MENU);
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(500));
}
void handleBluethoot() {
  static bool     iniciando        = false;
  static uint32_t tiempoInicio     = 0;
  static bool     pantallaSSID     = false;
  static bool     pantallaPassword = false;

  // ── Paso 1: Iniciar BT ──
  if (!gd.BTinit) {
    if (!iniciando) {
      SerialBT.begin("ALARMADIBAR");
      iniciando    = true;
      tiempoInicio = millis();
      sendRenderCommand(RENDER_CONECTANDO_BT);
    }
    if (millis() - tiempoInicio < 1500) {
      vTaskDelay(pdMS_TO_TICKS(100));
      return;
    }
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.BTinit = true;
      xSemaphoreGive(stateMutex);
    }
    return;
  }

  // ── Paso 2: Verificar conexión ──
  bool conectado = SerialBT.connected();
  if (!conectado) {
    sendRenderCommand(RENDER_CONECTANDO_BT);
    pantallaSSID     = false;
    pantallaPassword = false;
    vTaskDelay(pdMS_TO_TICKS(500));
    return;
  }

  // ── Paso 3: Marcar conectado ──
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    gd.Bluethoot = true;
    xSemaphoreGive(stateMutex);
  }

  // ── Paso 4: Pedir SSID ──
  if (SSID == "") {
    if (!pantallaSSID) {
      sendRenderCommand(RENDER_BT_SSID);
      pantallaSSID = true;
    }
    if (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '\n' || c == '\r') {
        if (buffer.length() > 0) {
          SSID = buffer;
          SSID.trim();
          buffer = "";
          Serial.print("SSID: '"); Serial.print(SSID);
          Serial.print("' longitud: "); Serial.println(SSID.length());
          pantallaPassword = false;
        }
      } else {
        buffer += c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return;
  }

  // ── Paso 5: Pedir Password ──
  if (password == "") {
    if (!pantallaPassword) {
      sendRenderCommand(RENDER_BT_PASSWORD);
      pantallaPassword = true;
    }
    if (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '\n' || c == '\r') {
        if (buffer.length() > 0) {
          password = buffer;
          password.trim();
          buffer = "";
          Serial.print("Password: '"); Serial.print(password);
          Serial.print("' longitud: "); Serial.println(password.length());
        }
      } else {
        buffer += c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return;
  }

  // ── Paso 6: Apagar BT y conectar WiFi ──
  if (password != "" && SSID != "") {
    pantallaSSID     = false;
    pantallaPassword = false;
    iniciando        = false;

    // Apagar BT para liberar la antena
    SerialBT.end();
    Serial.println("BT apagado, liberando antena para WiFi...");
    vTaskDelay(pdMS_TO_TICKS(500));

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.BTinit       = false;
      gd.Bluethoot    = false;
      gd.currentState = STATE_WF;
      xSemaphoreGive(stateMutex);
    }
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(50));
}

// ── Alarma ───────────────────────────────────────────────────
void handleAlarmActive() {
  vTaskDelay(pdMS_TO_TICKS(1500));
  generatePattern();
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    gd.currentState = STATE_SHOW_PATTERN;
    gd.inputCount   = 0;
    gd.cursorX      = 1;
    gd.cursorY      = 1;
    xSemaphoreGive(stateMutex);
  }
  sendRenderCommand(RENDER_PUZZLE_INIT);
  vTaskDelay(pdMS_TO_TICKS(500));
  sendRenderCommand(RENDER_SHOW_PATTERN);
}

void handleShowPattern() {
  vTaskDelay(pdMS_TO_TICKS(SHOW_PATTERN_MS));
  sendRenderCommand(RENDER_HIDE_PATTERN);
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    gd.currentState = STATE_WAIT_INPUT;
    xSemaphoreGive(stateMutex);
  }
}

void handleSuccess() {
  stopAlarmSound();
  sendRenderCommand(RENDER_SUCCESS);
  vTaskDelay(pdMS_TO_TICKS(2000));
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    gd.alarmTriggered = false;
    gd.currentState   = STATE_MENU;
    xSemaphoreGive(stateMutex);
  }
  sendRenderCommand(RENDER_MENU);
}

void handleFail() {
  sendRenderCommand(RENDER_FAIL);
  vTaskDelay(pdMS_TO_TICKS(1500));
  generatePattern();
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    gd.currentState = STATE_SHOW_PATTERN;
    gd.inputCount   = 0;
    gd.cursorX      = 1;
    gd.cursorY      = 1;
    xSemaphoreGive(stateMutex);
  }
  sendRenderCommand(RENDER_PUZZLE_INIT);
  vTaskDelay(pdMS_TO_TICKS(400));
  sendRenderCommand(RENDER_SHOW_PATTERN);
}

// ═══════════════════════════════════════════════════════════════
//  PUZZLE
// ═══════════════════════════════════════════════════════════════
void generatePattern() {
  bool usada[9] = {false};
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < PATTERN_LENGTH; i++) {
      int celda;
      do { celda = random(9); } while (usada[celda]);
      usada[celda]  = true;
      gd.pattern[i] = celda;
      Serial.print("[Puzzle] Patron["); Serial.print(i); Serial.print("]="); Serial.println(celda);
    }
    gd.inputCount = 0;
    xSemaphoreGive(stateMutex);
  }
}

bool checkPattern() {
  bool correcto = true;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < PATTERN_LENGTH; i++) {
      if (gd.userInput[i] != gd.pattern[i]) { correcto = false; break; }
    }
    xSemaphoreGive(stateMutex);
  }
  return correcto;
}

void handlePuzzleInput() {
  static bool     locked   = false;
  static uint32_t lockTime = 0;

  if (locked && (millis() - lockTime > NAV_COOLDOWN_MS))
    locked = false;

  if (locked) return;

  bool changed = false;

  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!digitalRead(PUZ_UP_PIN) && gd.cursorY > 0) {
      gd.cursorY--; changed = true;
    } else if (!digitalRead(PUZ_DOWN_PIN) && gd.cursorY < GRID_ROWS - 1) {
      gd.cursorY++; changed = true;
    } else if (!digitalRead(PUZ_LEFT_PIN) && gd.cursorX > 0) {
      gd.cursorX--; changed = true;
    } else if (!digitalRead(PUZ_RIGHT_PIN) && gd.cursorX < GRID_COLS - 1) {
      gd.cursorX++; changed = true;
    } else if (!digitalRead(PUZ_SEL_PIN)) {
      int celda = gd.cursorY * GRID_COLS + gd.cursorX;
      gd.userInput[gd.inputCount] = celda;
      gd.inputCount++;
      Serial.print("[Puzzle] Celda seleccionada: "); Serial.println(celda);

      if (gd.inputCount >= PATTERN_LENGTH) {
        xSemaphoreGive(stateMutex);
        locked = true; lockTime = millis();
        if (checkPattern()) {
          if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            gd.currentState = STATE_SUCCESS;
            xSemaphoreGive(stateMutex);
          }
        } else {
          if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            gd.currentState = STATE_FAIL;
            xSemaphoreGive(stateMutex);
          }
        }
        return;
      }
      changed = true;
    }
    xSemaphoreGive(stateMutex);
  }

  if (changed) {
    locked = true; lockTime = millis();
    sendRenderCommand(RENDER_PUZZLE_CURSOR);
  }
}

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES DE DIBUJO
// ═══════════════════════════════════════════════════════════════
void DrawMenu(int sel) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(2);
  if (gd.wifi) {
    char buf[10];
    sprintf(buf, "%02d:%02d", gd.hora, gd.minuto);
    tft.drawString(buf, 20, 20, 7);
  } else {
    tft.drawString("--:--", 20, 20, 7);
  }
  const char *items[3] = { "Poner Alarma", "Activar Wifi", "Activar BlueThoot" };
  for (int i = 0; i < 3; i++) {
    int iy = 120 + i * 24;
    if (i == sel) {
      tft.fillRect(0, iy, SCREEN_WIDTH, 16, TFT_YELLOW);
      tft.setTextColor(TFT_BLACK);
    } else {
      tft.setTextColor(TFT_BLACK);
    }
    tft.drawString(items[i], 95, iy, 1);
  }
}

void DrawWifi(int etapa) {
  if (etapa == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.drawCircle(160, 80, 50, TFT_WHITE);
    tft.drawCircle(160, 80, 35, TFT_WHITE);
    tft.drawCircle(160, 80, 20, TFT_WHITE);
    tft.fillCircle(160, 80,  5, TFT_WHITE);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Conectando a WiFi",   160 - (17*6), 150, 2);
    tft.drawString("Por favor espere...", 160 - (19*6), 175, 2);
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.drawLine(120, 130, 145, 160, TFT_GREEN);
    tft.drawLine(121, 130, 146, 160, TFT_GREEN);
    tft.drawLine(145, 160, 195, 100, TFT_GREEN);
    tft.drawLine(146, 160, 196, 100, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Conectado!",           160 - (10*6), 180, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Volviendo al menu...", 160 - (20*6), 205, 2);
  }
}

void DrawBT(int fase) {
  switch (fase) {
    case 0:
      tft.fillScreen(TFT_BLACK);
      tft.drawLine(160, 60,  160, 130, TFT_BLUE);
      tft.drawLine(160, 60,  185, 85,  TFT_BLUE);
      tft.drawLine(185, 85,  160, 110, TFT_BLUE);
      tft.drawLine(160, 130, 185, 105, TFT_BLUE);
      tft.drawLine(185, 105, 160, 80,  TFT_BLUE);
      tft.setTextColor(TFT_WHITE);
      tft.drawString("Esperando conexion", 160 - (18*6), 155, 2);
      tft.drawString("Bluetooth...",       160 - (12*6), 178, 2);
      tft.drawFastHLine(40, 205, 240, TFT_BLUE);
      tft.setTextColor(TFT_BLUE);
      tft.drawString("Dispositivo:",  160 - (12*6), 215, 2);
      tft.setTextColor(TFT_CYAN);
      tft.drawString("ALARMADIBAR",   160 - (11*6), 235, 2);
      break;
    case 1:
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_CYAN);
      tft.drawString("Configurar WiFi",      160 - (15*6),  20, 2);
      tft.drawFastHLine(20, 42, 280, TFT_CYAN);
      tft.drawCircle(160, 80, 30, TFT_WHITE);
      tft.drawCircle(160, 80, 18, TFT_WHITE);
      tft.fillCircle(160, 80,  5, TFT_WHITE);
      tft.drawFastHLine(20, 115, 280, TFT_WHITE);
      tft.setTextColor(TFT_YELLOW);
      tft.drawString("Ingresa el nombre de", 160 - (20*6), 125, 2);
      tft.drawString("la red WiFi a la que", 160 - (20*6), 145, 2);
      tft.drawString("te quieras conectar",  160 - (19*6), 165, 2);
      tft.setTextColor(TFT_RED);
      tft.drawString("Recuerda incluir",      160 - (16*6), 195, 2);
      tft.drawString("MAYUSCULAS y simbolos", 160 - (21*6), 215, 2);
      tft.drawFastHLine(20, 235, 280, TFT_CYAN);
      tft.setTextColor(TFT_GREEN);
      tft.drawString("Envia por Bluetooth",  160 - (19*6), 245, 2);
      break;
    case 2:
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_CYAN);
      tft.drawString("Configurar WiFi",      160 - (15*6),  20, 2);
      tft.drawFastHLine(20, 42, 280, TFT_CYAN);
      tft.drawRect(140, 65, 40, 30, TFT_WHITE);
      tft.drawArc(160, 68, 18, 12, 180, 360, TFT_WHITE, TFT_BLACK);
      tft.fillCircle(160, 80, 3, TFT_YELLOW);
      tft.drawFastHLine(20, 115, 280, TFT_WHITE);
      tft.setTextColor(TFT_YELLOW);
      tft.drawString("Ingresa la",           160 - (10*6), 125, 2);
      tft.drawString("contrasena de la red", 160 - (20*6), 145, 2);
      tft.drawString("a la que te quieras",  160 - (19*6), 165, 2);
      tft.drawString("conectar",             160 - ( 8*6), 185, 2);
      tft.setTextColor(TFT_RED);
      tft.drawString("Recuerda incluir",      160 - (16*6), 205, 2);
      tft.drawString("MAYUSCULAS y simbolos", 160 - (21*6), 220, 2);
      tft.drawFastHLine(20, 235, 280, TFT_CYAN);
      tft.setTextColor(TFT_GREEN);
      tft.drawString("Envia por Bluetooth",  160 - (19*6), 245, 2);
      break;
  }
}

void DrawAlarmActive() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("ALARMA!", 80, 40, 6);
  tft.drawString("Resuelve el puzzle", 50, 130, 2);
  tft.drawString("para apagarla",      75, 155, 2);
}

void drawPuzzleGrid() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Recuerda el patron!", 20, 5, 2);
  for (int fila = 0; fila < GRID_ROWS; fila++) {
    for (int col = 0; col < GRID_COLS; col++) {
      int x = GRID_OFFSET_X + col * (GRID_CELL_SIZE + 5);
      int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
      tft.fillRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_DARKGREY);
      tft.drawRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_WHITE);
    }
  }
  drawCursor(gd.cursorX, gd.cursorY);
}

void showPattern() {
  drawPuzzleGrid();
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    for (int i = 0; i < PATTERN_LENGTH; i++) {
      int celda = gd.pattern[i];
      int col   = celda % GRID_COLS;
      int fila  = celda / GRID_COLS;
      int x = GRID_OFFSET_X + col  * (GRID_CELL_SIZE + 5);
      int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
      tft.fillRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_YELLOW);
      tft.drawRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_WHITE);
      tft.setTextColor(TFT_BLACK);
      char num[2];
      sprintf(num, "%d", i + 1);
      tft.drawString(num, x + 22, y + 20, 4);
    }
    xSemaphoreGive(stateMutex);
  }
}

void hidePattern() {
  drawPuzzleGrid();
  tft.setTextColor(TFT_CYAN);
  tft.drawString("Tu turno! Selecciona", 15, 5, 2);
}

void drawCursor(int col, int fila) {
  int x = GRID_OFFSET_X + col  * (GRID_CELL_SIZE + 5);
  int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
  tft.drawRoundRect(x - 2, y - 2, GRID_CELL_SIZE + 4, GRID_CELL_SIZE + 4, 10, TFT_GREEN);
  tft.drawRoundRect(x - 3, y - 3, GRID_CELL_SIZE + 6, GRID_CELL_SIZE + 6, 11, TFT_GREEN);
}

void updatePuzzleCursor() {
  for (int fila = 0; fila < GRID_ROWS; fila++) {
    for (int col = 0; col < GRID_COLS; col++) {
      int x = GRID_OFFSET_X + col  * (GRID_CELL_SIZE + 5);
      int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
      bool seleccionada = false;
      int celdaIdx = fila * GRID_COLS + col;
      if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int k = 0; k < gd.inputCount; k++) {
          if (gd.userInput[k] == celdaIdx) { seleccionada = true; break; }
        }
        xSemaphoreGive(stateMutex);
      }
      uint16_t color = seleccionada ? TFT_BLUE : TFT_DARKGREY;
      tft.fillRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, color);
      tft.drawRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_WHITE);
    }
  }
  int cx, cy;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    cx = gd.cursorX;
    cy = gd.cursorY;
    xSemaphoreGive(stateMutex);
  }
  drawCursor(cx, cy);
  tft.setTextColor(TFT_WHITE);
  char progreso[20];
  sprintf(progreso, "%d/%d", gd.inputCount, PATTERN_LENGTH);
  tft.fillRect(250, 5, 70, 20, TFT_BLACK);
  tft.drawString(progreso, 255, 7, 2);
}

void DrawSuccess() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("CORRECTO!", 65, 70, 6);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Alarma apagada", 60, 150, 2);
}

void DrawFail() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED);
  tft.drawString("INCORRECTO", 55, 70, 4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Nuevo patron...", 65, 140, 2);
}

// ═══════════════════════════════════════════════════════════════
//  UTILIDADES
// ═══════════════════════════════════════════════════════════════
void sendRenderCommand(RenderCommand cmd) {
  xQueueSend(renderQueue, &cmd, 0);
}

int Tiempotranscurrido() {
  return millis() - gd.LastTiempotranscurrido;
}
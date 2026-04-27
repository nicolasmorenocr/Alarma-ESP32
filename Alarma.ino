// ═══════════════════════════════════════════════════════════════
//  Alarma.ino — Versión extendida con DFPlayer Mini y Puzzle 3x3
// ═══════════════════════════════════════════════════════════════

// ─── LIBRERÍAS ───────────────────────────────────────────────
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "time.h"
#include <DFRobotDFPlayerMini.h>

// ─── PINES Y HARDWARE ────────────────────────────────────────
// Pantalla TFT (sin cambios — definidos en User_Setup.h)
//   CS=15, RST=4, SCK=18, DC=2, SDA=23

// Botones originales de 2 patas (menú)
#define BTNUP_PIN    19
#define BTNDOWN_PIN  21
#define BTNSEL_PIN   25

// Botones de 4 patas para el puzzle
#define PUZ_UP_PIN    32
#define PUZ_DOWN_PIN  33
#define PUZ_LEFT_PIN  34
#define PUZ_RIGHT_PIN 35
#define PUZ_SEL_PIN   26

// DFPlayer Mini — usa UART2 del ESP32
#define DF_RX_PIN 16   // ESP32 RX2 ← DFPlayer TX
#define DF_TX_PIN 17   // ESP32 TX2 → DFPlayer RX (+ resistencia 1kΩ en serie)

// ─── CONSTANTES ──────────────────────────────────────────────
#define NAV_COOLDOWN_MS   100
#define SCREEN_WIDTH      320
#define SCREEN_HEIGHT     240
#define GRID_COLS         3
#define GRID_ROWS         3
#define GRID_CELL_SIZE    60    // Tamaño de cada celda en píxeles
#define GRID_OFFSET_X     20    // Margen izquierdo de la grilla
#define GRID_OFFSET_Y     40    // Margen superior de la grilla
#define PATTERN_LENGTH    3     // Número de casillas en el patrón
#define SHOW_PATTERN_MS   2500  // Tiempo que se muestra el patrón
#define ALARM_HOUR        7     // Hora de activación de la alarma
#define ALARM_MINUTE      0     // Minuto de activación

// WiFi y NTP (sin cambios)
const char* ssid             = "Nico";
const char* password         = "pipitariano";
const char* ntpServer        = "pool.ntp.org";
const long  gmtOffset_sec    = -5 * 3600;
const int   daylightOffset_sec = 0;

// ─── ENUMERACIONES ───────────────────────────────────────────

// Estados principales del programa
typedef enum : uint8_t {
  STATE_MENU,
  STATE_WF,
  STATE_BT,
  STATE_ALARMCONF,
  STATE_ALARM_ACTIVE,   // Alarma sonando, esperando inicio del puzzle
  STATE_SHOW_PATTERN,   // Mostrando el patrón al usuario
  STATE_WAIT_INPUT,     // Esperando que el usuario ingrese secuencia
  STATE_SUCCESS,        // Patrón correcto — apagar alarma
  STATE_FAIL,           // Patrón incorrecto — reiniciar puzzle
} ProgrammState;

// Comandos de renderizado para la tarea de dibujo
typedef enum : uint8_t {
  RENDER_MENU,
  RENDER_WIFI_CONECTANDO,
  RENDER_WIFI_CONECTADO,
  RENDER_CONECTANDO_BT,
  RENDER_ALARM_ACTIVE,   // Pantalla de alarma activa
  RENDER_PUZZLE_INIT,    // Dibuja la grilla vacía
  RENDER_SHOW_PATTERN,   // Destella las celdas del patrón
  RENDER_HIDE_PATTERN,   // Oculta el patrón (solo cursor)
  RENDER_PUZZLE_CURSOR,  // Solo actualiza posición del cursor
  RENDER_SUCCESS,        // Pantalla de éxito
  RENDER_FAIL,           // Pantalla de fallo
} RenderCommand;

// ─── ESTRUCTURA GLOBAL DE ESTADO ─────────────────────────────
typedef struct {
  ProgrammState currentState;
  int  menuSelection;
  bool Bluethoot;
  bool wifi;
  bool Conectando;
  int  hora;
  int  minuto;
  int  LastTiempotranscurrido;

  // ── Puzzle ──────────────────────────────────────────────────
  int  pattern[PATTERN_LENGTH];   // Índices de celdas (0-8) del patrón
  int  userInput[PATTERN_LENGTH]; // Secuencia ingresada por el usuario
  int  inputCount;                // Cuántas celdas lleva ingresadas
  int  cursorX;                   // Columna del cursor (0-2)
  int  cursorY;                   // Fila del cursor (0-2)
  bool alarmTriggered;            // true cuando la alarma está activa
} AppData;

// ─── VARIABLES GLOBALES ──────────────────────────────────────
volatile AppData gd;
TFT_eSPI tft = TFT_eSPI();
DFRobotDFPlayerMini dfPlayer;
HardwareSerial dfSerial(2);       // UART2 del ESP32

// FreeRTOS
SemaphoreHandle_t stateMutex = NULL;
QueueHandle_t     renderQueue = NULL;

// ─── SETUP ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("[BOOT] Iniciando sistema...");

  tft.init();
  tft.setRotation(1);

  // Valores iniciales
  gd.currentState    = STATE_MENU;
  gd.menuSelection   = 0;
  gd.wifi            = false;
  gd.Bluethoot       = false;
  gd.alarmTriggered  = false;
  gd.inputCount      = 0;
  gd.cursorX         = 1;
  gd.cursorY         = 1;

  // FreeRTOS
  renderQueue = xQueueCreate(5, sizeof(RenderCommand));
  stateMutex  = xSemaphoreCreateMutex();

  // Botones originales (menú)
  pinMode(BTNUP_PIN,   INPUT_PULLUP);
  pinMode(BTNDOWN_PIN, INPUT_PULLUP);
  pinMode(BTNSEL_PIN,  INPUT_PULLUP);

  // Botones de 4 patas (puzzle)
  pinMode(PUZ_UP_PIN,    INPUT_PULLUP);
  pinMode(PUZ_DOWN_PIN,  INPUT_PULLUP);
  pinMode(PUZ_LEFT_PIN,  INPUT_PULLUP);
  pinMode(PUZ_RIGHT_PIN, INPUT_PULLUP);
  pinMode(PUZ_SEL_PIN,   INPUT_PULLUP);

  // Inicializar DFPlayer (en setup, antes de crear tasks)
  setupDFPlayer();

  // Crear tareas de FreeRTOS
  xTaskCreatePinnedToCore(logicTask, "LogicTask", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(drawTask,  "DrawTask",  8192, NULL, 1, NULL, 0);

  sendRenderCommand(RENDER_MENU);
}

void loop() {}

// ═══════════════════════════════════════════════════════════════
//  SECCIÓN: DFPlayer Mini
// ═══════════════════════════════════════════════════════════════

// Inicializa la comunicación con el DFPlayer Mini
void setupDFPlayer() {
  dfSerial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
  vTaskDelay(pdMS_TO_TICKS(1000)); // Dar tiempo al módulo de arrancar

  if (!dfPlayer.begin(dfSerial)) {
    // Error: sin módulo conectado o SD no insertada
    Serial.println("[DFPlayer] ERROR: No se pudo inicializar.");
    Serial.println("[DFPlayer] Verifica: cable, SD insertada, GND común.");
    // El programa continúa sin audio — no se bloquea
    return;
  }

  Serial.println("[DFPlayer] Inicializado OK.");
  dfPlayer.volume(20);         // Volumen 0-30
  dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
}

// Reproduce la pista 001 en loop (placeholder de alarma)
void startAlarmSound() {
  Serial.println("[DFPlayer] Reproduciendo alarma...");
  dfPlayer.loop(1); // Archivo: /01/001.mp3 o 0001.mp3 en raíz de la SD
}

// Detiene la reproducción
void stopAlarmSound() {
  Serial.println("[DFPlayer] Deteniendo audio.");
  dfPlayer.stop();
}

// ═══════════════════════════════════════════════════════════════
//  SECCIÓN: LÓGICA PRINCIPAL (Task Core 1)
// ═══════════════════════════════════════════════════════════════

void logicTask(void *p) {
  for (;;) {
    ProgrammState state;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      state = gd.currentState;
      xSemaphoreGive(stateMutex);
    }

    switch (state) {
      case STATE_MENU:         handleMenu();         break;
      case STATE_WF:           handleWifi();         break;
      case STATE_ALARM_ACTIVE: handleAlarmActive();  break;
      case STATE_SHOW_PATTERN: handleShowPattern();  break;
      case STATE_WAIT_INPUT:   handlePuzzleInput();  break;
      case STATE_SUCCESS:      handleSuccess();      break;
      case STATE_FAIL:         handleFail();         break;
      default: break;
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Ceder CPU brevemente
  }
}

// ═══════════════════════════════════════════════════════════════
//  SECCIÓN: HANDLERS DE ESTADO
// ═══════════════════════════════════════════════════════════════

// ── Menú principal (sin cambios funcionales, agrega chequeo de hora) ──
void handleMenu() {
  struct tm timeinfo;
  static bool navLocked = false;
  static uint32_t navLockTime = 0;
  bool changed = false;

  // Actualizar hora cada minuto
  if (millis() - gd.LastTiempotranscurrido > 60000 && getLocalTime(&timeinfo)) {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (timeinfo.tm_hour != gd.hora || timeinfo.tm_min != gd.minuto) {
        gd.hora   = timeinfo.tm_hour;
        gd.minuto = timeinfo.tm_min;
        changed   = true;
      }
      // ── Chequear si es hora de activar la alarma ────────────
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
      gd.LastTiempotranscurrido = millis();
    }
  }

  // Desbloquear cooldown de navegación
  if (navLocked && (millis() - navLockTime > NAV_COOLDOWN_MS))
    navLocked = false;

  if (!navLocked) {
    navLocked   = true;
    navLockTime = millis();

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!digitalRead(BTNUP_PIN) && gd.menuSelection < 2) {
        gd.menuSelection++;
        changed = true;
      } else if (!digitalRead(BTNDOWN_PIN) && gd.menuSelection > 0) {
        gd.menuSelection--;
        changed = true;
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
}

// ── Alarma activa: inicia el puzzle automáticamente ──────────
void handleAlarmActive() {
  vTaskDelay(pdMS_TO_TICKS(1500)); // Breve pausa antes de lanzar puzzle
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

// ── Mostrar patrón: espera N segundos y luego oculta ─────────
void handleShowPattern() {
  vTaskDelay(pdMS_TO_TICKS(SHOW_PATTERN_MS)); // Tiempo para memorizar
  sendRenderCommand(RENDER_HIDE_PATTERN);
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    gd.currentState = STATE_WAIT_INPUT;
    xSemaphoreGive(stateMutex);
  }
}

// ── Éxito: apagar audio y volver al menú ─────────────────────
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

// ── Fallo: reiniciar puzzle con nuevo patrón ─────────────────
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

// ── Wifi (sin cambios estructurales) ─────────────────────────
void handleWifi() {
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (gd.wifi == true) {
      sendRenderCommand(RENDER_WIFI_CONECTADO);
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      vTaskDelay(pdMS_TO_TICKS(1000));
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
      sendRenderCommand(RENDER_MENU);
    } else {
      WiFi.begin(ssid, password);
      xSemaphoreGive(stateMutex);
      sendRenderCommand(RENDER_WIFI_CONECTANDO);
      // Esperar conexión (hasta 10 segundos)
      int intentos = 0;
      while (WiFi.status() != WL_CONNECTED && intentos < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        intentos++;
      }
      if (WiFi.status() == WL_CONNECTED) {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          gd.wifi = true;
          xSemaphoreGive(stateMutex);
        }
      } else {
        // Falló — volver al menú
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          gd.currentState = STATE_MENU;
          xSemaphoreGive(stateMutex);
        }
        sendRenderCommand(RENDER_MENU);
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  SECCIÓN: PUZZLE DE MEMORIA
// ═══════════════════════════════════════════════════════════════

// Genera un patrón aleatorio de PATTERN_LENGTH celdas únicas (0-8)
void generatePattern() {
  Serial.println("[Puzzle] Generando nuevo patrón...");
  bool usada[9] = {false};
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < PATTERN_LENGTH; i++) {
      int celda;
      do {
        celda = random(9); // 0 a 8
      } while (usada[celda]);
      usada[celda]   = true;
      gd.pattern[i]  = celda;
      Serial.print("[Puzzle] Patrón["); Serial.print(i); Serial.print("]="); Serial.println(celda);
    }
    gd.inputCount = 0;
    xSemaphoreGive(stateMutex);
  }
}

// Valida la secuencia ingresada contra el patrón generado
bool checkPattern() {
  bool correcto = true;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < PATTERN_LENGTH; i++) {
      if (gd.userInput[i] != gd.pattern[i]) {
        correcto = false;
        break;
      }
    }
    xSemaphoreGive(stateMutex);
  }
  return correcto;
}

// Maneja los botones del puzzle durante STATE_WAIT_INPUT
void handlePuzzleInput() {
  static bool locked = false;
  static uint32_t lockTime = 0;

  if (locked && (millis() - lockTime > NAV_COOLDOWN_MS))
    locked = false;

  if (locked) return;

  bool changed = false;

  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {

    // ── Movimiento del cursor ──────────────────────────────
    if (!digitalRead(PUZ_UP_PIN) && gd.cursorY > 0) {
      gd.cursorY--;
      changed = true;
    } else if (!digitalRead(PUZ_DOWN_PIN) && gd.cursorY < GRID_ROWS - 1) {
      gd.cursorY++;
      changed = true;
    } else if (!digitalRead(PUZ_LEFT_PIN) && gd.cursorX > 0) {
      gd.cursorX--;
      changed = true;
    } else if (!digitalRead(PUZ_RIGHT_PIN) && gd.cursorX < GRID_COLS - 1) {
      gd.cursorX++;
      changed = true;
    }

    // ── Seleccionar celda actual ───────────────────────────
    else if (!digitalRead(PUZ_SEL_PIN)) {
      int celdaSeleccionada = gd.cursorY * GRID_COLS + gd.cursorX;
      gd.userInput[gd.inputCount] = celdaSeleccionada;
      gd.inputCount++;
      Serial.print("[Puzzle] Usuario seleccionó celda: "); Serial.println(celdaSeleccionada);

      // ¿Completó la secuencia?
      if (gd.inputCount >= PATTERN_LENGTH) {
        xSemaphoreGive(stateMutex);
        locked   = true;
        lockTime = millis();
        // Evaluar resultado
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
        return; // Salir sin hacer Give doble
      }

      changed = true;
    }

    xSemaphoreGive(stateMutex);
  }

  if (changed) {
    locked   = true;
    lockTime = millis();
    sendRenderCommand(RENDER_PUZZLE_CURSOR);
  }
}

// ═══════════════════════════════════════════════════════════════
//  SECCIÓN: DIBUJO (Task Core 0)
// ═══════════════════════════════════════════════════════════════

void drawTask(void *p) {
  struct tm timeinfo;
  RenderCommand cmd;
  static int lastSel = -1;

  for (;;) {
    if (xQueueReceive(renderQueue, &cmd, portMAX_DELAY)) {
      switch (cmd) {

        case RENDER_MENU: {
          int sel, horaN = 0, minutoN = 0;
          if (getLocalTime(&timeinfo)) {
            horaN   = timeinfo.tm_hour;
            minutoN = timeinfo.tm_min;
          }
          if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            sel = gd.menuSelection;
            xSemaphoreGive(stateMutex);
          }
          if (sel != lastSel || horaN != gd.hora || minutoN != gd.minuto) {
            gd.hora   = horaN;
            gd.minuto = minutoN;
            DrawMenu(sel);
            lastSel = sel;
          }
          break;
        }

        case RENDER_WIFI_CONECTANDO:  DrawWifi(0);          break;
        case RENDER_WIFI_CONECTADO:   DrawWifi(1);          break;
        case RENDER_ALARM_ACTIVE:     DrawAlarmActive();    break;
        case RENDER_PUZZLE_INIT:      drawPuzzleGrid();     break;
        case RENDER_SHOW_PATTERN:     showPattern();        break;
        case RENDER_HIDE_PATTERN:     hidePattern();        break;
        case RENDER_PUZZLE_CURSOR:    updatePuzzleCursor(); break;
        case RENDER_SUCCESS:          DrawSuccess();        break;
        case RENDER_FAIL:             DrawFail();           break;
        default: break;
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  SECCIÓN: FUNCIONES DE DIBUJO
// ═══════════════════════════════════════════════════════════════

// ── Menú original (sin cambios) ───────────────────────────────
void DrawMenu(int sel) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(2);
  if (gd.wifi) {
    char buffer[10];
    sprintf(buffer, "%02d:%02d", gd.hora, gd.minuto);
    tft.drawString(buffer, 20, 20, 7);
  } else {
    tft.drawString("--:--", 20, 20, 7);
  }
  const char *items[3] = { "Poner Alarma", "Activar Wifi", "Activar BlueThoot" };
  for (int i = 0; i < 3; i++) {
    int iy = 120 + i * 24;
    if (i == sel) {
      tft.fillRect(0, iy, SCREEN_WIDTH, 16, TFT_YELLOW);
    }
    tft.setTextColor(TFT_BLACK);
    tft.drawString(items[i], 95, iy, 1);
  }
}

// ── WiFi ──────────────────────────────────────────────────────
void DrawWifi(int modo) {
  tft.fillScreen(TFT_WHITE);
  if (modo == 0) {
    tft.setTextColor(TFT_BLACK);
    tft.drawString("Conectando...", 10, 100, 4);
  } else {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Conectado!", 10, 80, 4);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("Regresando...", 10, 130, 2);
  }
}

// ── Pantalla de alarma activa ─────────────────────────────────
void DrawAlarmActive() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.drawString("ALARMA!", 80, 40, 6);
  tft.setTextSize(1);
  tft.drawString("Resuelve el puzzle", 50, 130, 2);
  tft.drawString("para apagarla", 75, 155, 2);
}

// ── Dibuja la grilla 3x3 vacía (celdas ocultas en gris) ──────
void drawPuzzleGrid() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Recuerda el patron!", 20, 5, 2);

  for (int fila = 0; fila < GRID_ROWS; fila++) {
    for (int col = 0; col < GRID_COLS; col++) {
      int x = GRID_OFFSET_X + col * (GRID_CELL_SIZE + 5);
      int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
      // Celda oculta — fondo gris oscuro
      tft.fillRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_DARKGREY);
      tft.drawRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_WHITE);
    }
  }
  // Dibujar cursor en posición inicial
  drawCursor(gd.cursorX, gd.cursorY);
}

// ── Muestra las celdas del patrón iluminadas ─────────────────
void showPattern() {
  // Primero redibujar grilla limpia
  drawPuzzleGrid();

  // Iluminar celdas del patrón en amarillo
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    for (int i = 0; i < PATTERN_LENGTH; i++) {
      int celda = gd.pattern[i];
      int col   = celda % GRID_COLS;
      int fila  = celda / GRID_COLS;
      int x = GRID_OFFSET_X + col * (GRID_CELL_SIZE + 5);
      int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
      // Celda del patrón — amarillo brillante
      tft.fillRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_YELLOW);
      tft.drawRoundRect(x, y, GRID_CELL_SIZE, GRID_CELL_SIZE, 8, TFT_WHITE);
      // Número de orden (1, 2, 3)
      tft.setTextColor(TFT_BLACK);
      char num[2];
      sprintf(num, "%d", i + 1);
      tft.drawString(num, x + 22, y + 20, 4);
    }
    xSemaphoreGive(stateMutex);
  }
}

// ── Oculta el patrón y muestra instrucción ───────────────────
void hidePattern() {
  drawPuzzleGrid(); // Redibujar grilla vacía (sin celdas iluminadas)
  tft.setTextColor(TFT_CYAN);
  tft.drawString("Tu turno! Selecciona", 15, 5, 2);
}

// ── Dibuja el cursor sobre la celda activa ───────────────────
void drawCursor(int col, int fila) {
  int x = GRID_OFFSET_X + col * (GRID_CELL_SIZE + 5);
  int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
  // Borde verde brillante = cursor
  tft.drawRoundRect(x - 2, y - 2, GRID_CELL_SIZE + 4, GRID_CELL_SIZE + 4, 10, TFT_GREEN);
  tft.drawRoundRect(x - 3, y - 3, GRID_CELL_SIZE + 6, GRID_CELL_SIZE + 6, 11, TFT_GREEN);
}

// ── Actualiza solo el cursor sin redibujar toda la grilla ────
void updatePuzzleCursor() {
  // Redibujar todas las celdas para limpiar cursor anterior
  for (int fila = 0; fila < GRID_ROWS; fila++) {
    for (int col = 0; col < GRID_COLS; col++) {
      int x = GRID_OFFSET_X + col * (GRID_CELL_SIZE + 5);
      int y = GRID_OFFSET_Y + fila * (GRID_CELL_SIZE + 5);
      // Verificar si esta celda ya fue seleccionada (marcarla en azul)
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
  // Redibujar cursor en nueva posición
  int cx, cy;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    cx = gd.cursorX;
    cy = gd.cursorY;
    xSemaphoreGive(stateMutex);
  }
  drawCursor(cx, cy);

  // Mostrar progreso de ingreso
  tft.setTextColor(TFT_WHITE);
  char progreso[20];
  int ic = gd.inputCount;
  sprintf(progreso, "%d/%d", ic, PATTERN_LENGTH);
  tft.fillRect(250, 5, 70, 20, TFT_BLACK);
  tft.drawString(progreso, 255, 7, 2);
}

// ── Pantalla de éxito ─────────────────────────────────────────
void DrawSuccess() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("CORRECTO!", 65, 70, 6);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Alarma apagada", 60, 150, 2);
}

// ── Pantalla de fallo ─────────────────────────────────────────
void DrawFail() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED);
  tft.drawString("INCORRECTO", 55, 70, 4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Nuevo patron...", 65, 140, 2);
}

// ─── UTILIDADES ──────────────────────────────────────────────

void sendRenderCommand(RenderCommand cmd) {
  xQueueSend(renderQueue, &cmd, 0);
}

int Tiempotranscurrido() {
  return millis() - gd.LastTiempotranscurrido;
}
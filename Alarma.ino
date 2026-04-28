//PINES Y HARDWARE
//  PANTALLA
//    CS 15
//    RST 4
//    SCK 18
//    DC 2
//    SDA 23
//  BOTONES 
//    UP 19
//    DOWN 21
//    SELECT 25
//LIBRERIAS
#include <TFT_eSPI.h>
#include <WiFi.h> 
#include "time.h"
#include "BluetoothSerial.h"
#include <DFRobotDFPlayerMini.h>

//CONSTANTES
#define NAV_COOLDOWN_MS 100
#define DEBOUNCE_MS     200
#define SCREEN_WIDTH 320
#define BTNUP_PIN 19
#define BTNDOWN_PIN 21
#define BTNSEL_PIN 25
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5 * 3600;
const int   daylightOffset_sec = 0;

//ENUMERACIONES 
typedef enum : uint8_t {
  STATE_MENU,
  STATE_WF,
  STATE_BT,
  STATE_ALARMCONF,
} ProgrammState;

typedef enum : uint8_t {
  RENDER_MENU,
  RENDER_WIFI_CONECTANDO,
  RENDER_WIFI_CONECTADO,
  RENDER_CONECTANDO_BT,
  RENDER_BT_CONECTADO,
  RENDER_BT_SSID,
  RENDER_BT_PASSWORD
} RenderCommand;

typedef struct {
  ProgrammState currentState;
  int menuSelection;
  bool Bluethoot;
  bool wifi;
  bool Conectando; 
  int hora;
  int minuto;
  int LastTiempotranscurrido;
  bool RepCancion;
  bool BTinit;
} AppData;

//INICIACION APP
volatile AppData gd;
DFRobotDFPlayerMini myDFPlayer;
BluetoothSerial SerialBT;
TFT_eSPI tft = TFT_eSPI();
String SSID     = "";
String password = "";
String buffer   = "";

// FREERTOS
TaskHandle_t taskHandle;
SemaphoreHandle_t stateMutex = NULL;
QueueHandle_t renderQueue    = NULL; 

// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1); 

  gd.currentState          = STATE_MENU;
  gd.menuSelection         = 0;
  gd.wifi                  = false;
  gd.Bluethoot             = false;
  gd.LastTiempotranscurrido = 0;
  gd.BTinit                = false;
  gd.Conectando             = false;

   Serial2.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  if (!myDFPlayer.begin(Serial2)) {
    Serial.println("DFPlayer no encontrado!");
  } else {
    Serial.println("DFPlayer listo");
    myDFPlayer.volume(20);  // Volumen 0-30
  }


  renderQueue = xQueueCreate(3, sizeof(RenderCommand));
  stateMutex  = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(logicTask, "LogicTask", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(drawTask,  "DrawTask",  8192, NULL, 1, &taskHandle, 0);

  pinMode(BTNUP_PIN,   INPUT_PULLUP);
  pinMode(BTNDOWN_PIN, INPUT_PULLUP);
  pinMode(BTNSEL_PIN,  INPUT_PULLUP);

  sendRenderCommand(RENDER_MENU);
}

void loop() {}

// ─────────────────────────────────────────────
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
      case STATE_MENU:      handleMenu();      break;
      case STATE_WF:        handleWifi();      break;
      case STATE_BT:        handleBluethoot(); break;
      case STATE_ALARMCONF:                    break;
      default: vTaskDelay(pdMS_TO_TICKS(10));  break;
    }
  }
}

// ─────────────────────────────────────────────
void drawTask(void *p) {
  struct tm timeinfo;
  RenderCommand cmd;
  static int lastSel = -1;
  if(SSID != ""){
    Serial.println(lastSel);
  }

  for (;;) {
    if (xQueueReceive(renderQueue, &cmd, portMAX_DELAY)) {

      if (cmd == RENDER_MENU) {
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
      }
      else if (cmd == RENDER_WIFI_CONECTANDO) { DrawWifi(0); }
      else if (cmd == RENDER_WIFI_CONECTADO)  { DrawWifi(1); }
      else if (cmd == RENDER_CONECTANDO_BT)   { DrawBT(0);   }
      else if (cmd == RENDER_BT_SSID)         { DrawBT(1);   }
      else if (cmd == RENDER_BT_PASSWORD)     { DrawBT(2);   }
    }
  }
}

// ─────────────────────────────────────────────
// FUNCIONES DE DIBUJADO
// ─────────────────────────────────────────────
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

  const char *items[3] = {
    "Poner Alarma",
    "Activar Wifi",
    "Activar BlueThoot"
  };

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
    tft.drawString("Conectado!",          160 - (10*6), 180, 2);
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
      tft.drawString("Configurar WiFi",     160 - (15*6),  20, 2);
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
      tft.drawString("Configurar WiFi",     160 - (15*6),  20, 2);
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

// ─────────────────────────────────────────────
// HANDLERS
// ─────────────────────────────────────────────
void handleMenu() {
  struct tm timeinfo;
  static bool     navLocked   = false;
  static uint32_t navLockTime = 0;
  int horaN   = -1;
  int minutoN = -1;

  if (Tiempotranscurrido() > 20000) {
    gd.LastTiempotranscurrido = millis();
    if (gd.wifi) {
      if (getLocalTime(&timeinfo)) {
        horaN   = timeinfo.tm_hour;
        minutoN = timeinfo.tm_min;
      }
    }
  }

  if (navLocked && (millis() - navLockTime > NAV_COOLDOWN_MS))
    navLocked = false;

  bool changed = false;

 uint32_t notif = ulTaskNotifyTake(pdTRUE, 0);

  if(notif > 0){
    changed =true;
  }
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (horaN != -1 && (horaN != gd.hora || minutoN != gd.minuto))
      changed = true;
    xSemaphoreGive(stateMutex);
  }

  if (!navLocked) {
    navLocked   = true;
    navLockTime = millis();

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (!digitalRead(BTNUP_PIN) && gd.menuSelection < 2) {
        Serial.println("arriba");

        gd.menuSelection++;
        changed = true;
      } else if (!digitalRead(BTNDOWN_PIN) && gd.menuSelection > 0) {
        gd.menuSelection--;
        Serial.println("abajo");
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

  vTaskDelay(pdMS_TO_TICKS(50));
}

void handleWifi() {
  static bool yaConectado = false;

  // ── DEBUG — ver qué tiene SSID y password ──
  Serial.println("=== handleWifi ===");
  Serial.print("SSID: '");     Serial.print(SSID);     Serial.println("'");
  Serial.print("Password: '"); Serial.print(password); Serial.println("'");
  Serial.print("Conectando: "); Serial.println(gd.Conectando);
  Serial.print("WiFi status: "); Serial.println(WiFi.status());
  // WL_IDLE_STATUS=0, WL_NO_SSID_AVAIL=1, WL_CONNECTED=3, WL_CONNECT_FAILED=4, WL_DISCONNECTED=6

  // ── Ya conectado → volver al menú ──
  if (gd.wifi) {
    if (!yaConectado) {
      yaConectado = true;
      sendRenderCommand(RENDER_WIFI_CONECTADO);
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_MENU);
    yaConectado = false;
    return;
  }

  // ── Iniciar conexión ──
  if (!gd.Conectando && SSID != "" && password != "") {
    sendRenderCommand(RENDER_WIFI_CONECTANDO);
    Serial.println("Iniciando WiFi.begin...");
    WiFi.begin(SSID.c_str(), password.c_str());

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.Conectando = true;   // ← dentro del mutex
      xSemaphoreGive(stateMutex);
    }
  }

  // ── Detectar conexión ──
  if (gd.Conectando && WiFi.status() == WL_CONNECTED) {
    Serial.println("Conexion detectada!");
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.wifi       = true;
      gd.Conectando = false;  // ← dentro del mutex
      xSemaphoreGive(stateMutex);
    }
    return;
  }

  // ── Sin SSID o password ──
  if (SSID == "" || password == "") {
    Serial.println("SSID o password vacios, volviendo al menu");
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

  // ── Paso 1: Iniciar BT solo una vez ──
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

  // ── Paso 3: Marcar como conectado ──
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
          SSID   = buffer;
          buffer = "";
          Serial.println("SSID: " + SSID);
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
          buffer   = "";
          Serial.println("Password: " + password);
        }
      } else {
        buffer += c;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    return;
  }

  // ── Paso 6: Tenemos todo → volver al menú ──
  if (password != "" && SSID != "") {
    Serial.println("chamo2");
    pantallaSSID     = false;
    pantallaPassword = false;
    iniciando        = false;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      Serial.println("chamo3");
      xTaskNotifyGive(taskHandle);
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_MENU);
    if(gd.currentState == STATE_MENU){
      Serial.println("chamo4");}

  }

  vTaskDelay(pdMS_TO_TICKS(50));
}

// ─────────────────────────────────────────────
void sendRenderCommand(RenderCommand cmd) {
  xQueueSend(renderQueue, &cmd, 0);
}

int Tiempotranscurrido() {
  return millis() - gd.LastTiempotranscurrido;
}
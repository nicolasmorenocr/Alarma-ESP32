// ═══════════════════════════════════════════════════════════════
//  Alarma.ino — Merge completo
// ═══════════════════════════════════════════════════════════════

// ─── LIBRERÍAS ───────────────────────────────────────────────
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "time.h"
#include "BluetoothSerial.h"
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

// ─── PINES ───────────────────────────────────────────────────
#define BUZZER_PIN    33
#define NEO_PIN       5
#define BTNUP_PIN     19
#define BTNDOWN_PIN   21
#define BTNSEL_PIN    25
#define FPSerial Serial1
// ─── CONSTANTES ──────────────────────────────────────────────
#define NEO_COUNT         24
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
  STATE_SHOW_PATTERN,
  STATE_WAIT_INPUT,
  STATE_SUCCESS,
  STATE_FAIL,
  STATE_ALARM
} ProgrammState;

typedef enum : uint8_t {
  RENDER_MENU,
  RENDER_WIFI_CONECTANDO,
  RENDER_WIFI_CONECTADO,
  RENDER_CONECTANDO_BT,
  RENDER_BT_CONECTADO,
  RENDER_BT_SSID,
  RENDER_BT_PASSWORD,
  RENDER_ALARM_CONF,
  RENDER_ALARM_ACTIVE
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
  bool alarmprevent;
  bool BTinit;
  int alarmaHora   = ALARM_HOUR;
  int alarmaMinuto = ALARM_MINUTE;
  bool alarmareconfig;
  bool horaconfigend;
} AppData;

// ─── VARIABLES GLOBALES ──────────────────────────────────────
Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
volatile bool neoRunning = false;
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


void DrawMenu(int sel);
void DrawWifi(int etapa);
void DrawBT(int fase);

void sendRenderCommand(RenderCommand cmd);
int  Tiempotranscurrido();
void handleAlarmConf();
void DrawAlarmConf(int campo, int hh, int mm);
// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  
  Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
  volatile bool neoRunning = false; 
  FPSerial.begin(9600, SERIAL_8N1, 16, 17);
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
  gd.alarmareconfig         = false;
  gd.alarmprevent           = false;
  gd.horaconfigend          = false;

  if (!myDFPlayer.begin(FPSerial)) {
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


  xTaskCreatePinnedToCore(logicTask, "LogicTask", 8192, NULL, 1, NULL,        1);
  xTaskCreatePinnedToCore(drawTask,  "DrawTask",  8192, NULL, 1, &taskHandle, 0);
  xTaskCreate(neoPixelTask, "neoPixelTask", 8192,NULL, 2,NULL);

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
      case STATE_ALARMCONF:{    
      handleAlarmConf();    
      break;
      }
      case STATE_ALARM:        handleAlarm();         break;
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
          if (sel != lastSel
          || (gd.wifi && (horaN != gd.hora || minutoN != gd.minuto)) 
          ||  gd.alarmareconfig == true 
          || gd.horaconfigend ) {
            if(gd.wifi && (horaN != gd.hora || minutoN != gd.minuto)){
                gd.alarmprevent = false;
            }
              
              gd.alarmareconfig = false;
              gd.horaconfigend = false;
            if (gd.wifi) {
              gd.hora   = horaN;
              gd.minuto = minutoN;
            }

            DrawMenu(sel);
            lastSel = sel;
          }
          break;
        }
        case RENDER_ALARM_CONF: {
          int c, h, m;
          // campo: 0=hora, 1=minuto — lo recuperamos del estado global
          // hora/minuto vienen de gd (usados como buffer de edición)
          if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            h = gd.hora;
            m = gd.minuto;
            xSemaphoreGive(stateMutex);
          }
          // campo se pasa implícitamente: está en handleAlarmConf (static)
          // Para desacoplarlo limpiamente, usa la misma lógica de render:
          // el campo 0 muestra HORA en amarillo si alarmaHora != h (editando)
          // Simplificación: siempre renderizamos con lo que hay en gd
          c = (gd.alarmaHora == h && gd.alarmaMinuto != m) ? 1 : 0;
          DrawAlarmConf(c, h, m);
          break;
        }
        case RENDER_WIFI_CONECTANDO: DrawWifi(0);          break;
        case RENDER_WIFI_CONECTADO:  DrawWifi(1);          break;
        case RENDER_CONECTANDO_BT:   DrawBT(0);            break;
        case RENDER_BT_SSID:         DrawBT(1);            break;
        case RENDER_BT_PASSWORD:     DrawBT(2);            break;
        case RENDER_ALARM_ACTIVE: DrawAlarmActive();        break;
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
  bool alarmaconfig = false;

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

      if(gd.alarmareconfig == true){
        Serial.println("Se configuro alarma");
       changed = true;

    }

  // Chequear alarma y actualizar hora en gd
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if(gd.hora == gd.alarmaHora && gd.minuto == gd.alarmaMinuto){
      if(!gd.alarmprevent){
        gd.currentState = STATE_ALARM;
      }
    }
    // ← Guardar hora en gd y marcar changed
    if (horaN != -1 && minutoN -1 && (horaN != gd.hora || minutoN != gd.minuto)) {
      changed   = true;
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
          case 0:{ 
            gd.currentState = STATE_ALARMCONF; 
            Serial.println("en el menu se eligio alarmconf");
            break;
            }
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


// ═══════════════════════════════════════════════════════════════
//  PUZZLE
// ═══════════════════════════════════════════════════════════════

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




// ═══════════════════════════════════════════════════════════════
//  UTILIDADES
// ═══════════════════════════════════════════════════════════════
void sendRenderCommand(RenderCommand cmd) {
  xQueueSend(renderQueue, &cmd, 0);
}

int Tiempotranscurrido() {
  return millis() - gd.LastTiempotranscurrido;
}

// ═══════════════════════════════════════════════════════════════
//  HANDLER: CONFIGURAR ALARMA
// ═══════════════════════════════════════════════════════════════
void handleAlarmConf() {

  // campo 0 = hora, campo 1 = minuto, campo 2 = confirmado
  static int  campo      = 0;
  static int  editHora   = 0;
  static int  editMin    = 0;
  static bool iniciado   = false;
  static bool btnLocked  = false;
  static uint32_t lockTime = 0;

  // Inicializar con valores actuales al entrar
  if (!iniciado) {
    iniciado  = true;
    campo     = 0;
    editHora  = gd.alarmaHora;
    editMin   = gd.alarmaMinuto;
    sendRenderCommand(RENDER_ALARM_CONF);
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  // Debounce
  if (btnLocked && (millis() - lockTime > DEBOUNCE_MS))
    btnLocked = false;
  if (btnLocked) { vTaskDelay(pdMS_TO_TICKS(200));}

  bool changed = false;

  if (!digitalRead(BTNUP_PIN)) {
    btnLocked = true; lockTime = millis();
    if (campo == 0) { editHora = (editHora + 1) % 24; }
    else            { editMin  = (editMin  + 1) % 60; }
    changed = true;
  }
  else if (!digitalRead(BTNDOWN_PIN)) {
    btnLocked = true; lockTime = millis();
    if (campo == 0) { editHora = (editHora + 23) % 24; }
    else            { editMin  = (editMin  + 59) % 60; }
    changed = true;
  }
  else if (!digitalRead(BTNSEL_PIN)) {
    btnLocked = true; lockTime = millis();
    if (campo < 1) {
      campo++;
      Serial.println(campo);          // avanzar al siguiente campo
      changed = true;
    } else {
      Serial.println(campo);  
      // Confirmar: guardar alarma y volver al menú
      gd.alarmaHora   = editHora;
      gd.alarmaMinuto = editMin;
      Serial.printf("[ALARMA] Configurada para %02d:%02d\n", gd.alarmaHora, gd.alarmaMinuto);

      iniciado = false;   // resetear para la próxima vez
      if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Serial.println("Volviendo al menu");
        vTaskDelay(pdMS_TO_TICKS(200));
        gd.alarmareconfig = true;
        gd.currentState = STATE_MENU;
        xSemaphoreGive(stateMutex);
      }
      sendRenderCommand(RENDER_MENU);
      return;
    }
  }

  if (changed) {
    // Pasar valores al draw via variables temporales (sin tocar gd)
    // DrawAlarmConf los lee directamente de los statics a través del comando
    // Usamos una pequeña trampa: los guardamos en gd temporalmente para el render
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      gd.hora   = editHora;   // reutilizamos gd.hora/minuto como buffer de edición
      gd.minuto = editMin;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_ALARM_CONF);
  }

  vTaskDelay(pdMS_TO_TICKS(30));
}
void DrawAlarmConf(int campo, int hh, int mm) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.drawString("Configurar Alarma", 60, 10, 2);
  tft.drawFastHLine(20, 32, 280, TFT_CYAN);

  // Hora
  uint16_t colHora = (campo == 0) ? TFT_YELLOW : TFT_WHITE;
  tft.setTextColor(colHora);
  char bufH[3]; sprintf(bufH, "%02d", hh);
  tft.drawString(bufH, 80, 90, 7);

  // Separador
  tft.setTextColor(TFT_WHITE);
  tft.drawString(":", 148, 90, 7);

  // Minuto
  uint16_t colMin = (campo == 1) ? TFT_YELLOW : TFT_WHITE;
  tft.setTextColor(colMin);
  char bufM[3]; sprintf(bufM, "%02d", mm);
  tft.drawString(bufM, 175, 90, 7);

  // Instrucciones
  tft.setTextColor(TFT_GREEN);
  tft.drawString("UP/DOWN: cambiar valor", 20, 170, 2);
  tft.drawString("SELECT:  siguiente / OK", 20, 192, 2);

  // Indicador de campo activo
  tft.setTextColor(TFT_YELLOW);
  const char* labels[] = { "< Ajustando HORA >", "< Ajustando MINUTO >" };
  tft.drawString(labels[campo], 160 - (strlen(labels[campo]) * 3), 215, 2);
}
// ═══════════════════════════════════════════════════════════════
//  PLACEHOLDERS DE MÚSICA
// ═══════════════════════════════════════════════════════════════
void MusicaOn() {
  // TODO: implementar sonido de alarma
  return;
}

void MusicaOff() {
  // TODO: detener sonido
  return;
}

// ═══════════════════════════════════════════════════════════════
//  NEOPIXEL
// ═══════════════════════════════════════════════════════════════
void NeopixelOn()  { neoRunning = true; }

void NeopixelOff() {
  neoRunning = false;
  strip.clear();
  strip.show();
}

// Tarea FreeRTOS dedicada al NeoPixel — anima cuando neoRunning == true
void neoPixelTask(void *p) {
  const uint32_t palette[] = {
    strip.Color(  0, 255, 180),   // cian verdoso
    strip.Color(  0, 200, 255),   // azul cian
    strip.Color(  0,  80, 255),   // azul puro
    strip.Color(  0, 255, 100),   // verde azulado
    strip.Color( 20, 255, 220),   // turquesa
    strip.Color(  0, 150, 255),   // azul claro
    strip.Color(  0, 255,  50),   // verde brillante
    strip.Color(  0, 100, 200),   // azul medio
  };
  const int palSize = 8;
  uint32_t lastUpdate = 0;
  int step = 0;

  for (;;) {
    Serial.println("neopixel task");
    if (neoRunning) {
      uint32_t now = millis();
      if (now - lastUpdate >= 40) {        // ~25 fps = efecto frenético
        lastUpdate = now;
        for (int i = 0; i < NEO_COUNT; i++) {
          // Cada 3 LEDs uno se apaga para dar sensación de parpadeo caótico
          if ((step + i) % 3 == 0) {
            strip.setPixelColor(i, 0);
          } else {
            int idx = (step + i * 3) % palSize;
            strip.setPixelColor(i, palette[idx]);
          }
        }
        strip.show();
        step = (step + 1) % (palSize * 6);
      }
    }
    else{
      for (int i = 0; i < NEO_COUNT; i++) {
        strip.setPixelColor(i, strip.Color(255, 0, 0));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ═══════════════════════════════════════════════════════════════
//  HANDLER: ALARMA ACTIVA
// ═══════════════════════════════════════════════════════════════
void handleAlarm() {
  static bool     iniciado        = false;
  static bool     esperandoSoltar = true;
  static uint32_t lastBlink       = 0;


  if (!iniciado) {
    iniciado        = true;
    esperandoSoltar = true;
    lastBlink       = millis();
    MusicaOn();
    NeopixelOn();
    sendRenderCommand(RENDER_ALARM_ACTIVE);
  }

  // Parpadeo azul/verde en pantalla cada 400 ms
  if (millis() - lastBlink >= 400) {
    lastBlink = millis();
    sendRenderCommand(RENDER_ALARM_ACTIVE);
  }

  // Esperar a que SELECT esté suelto (evita detección instantánea al entrar)
  if (esperandoSoltar) {
    if (digitalRead(BTNSEL_PIN)) esperandoSoltar = false;   // HIGH = suelto (PULLUP)
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }

  // SELECT presionado → apagar todo y volver al menú
  if (!digitalRead(BTNSEL_PIN)) {
    MusicaOff();
    NeopixelOff();
    gd.alarmprevent = true;
    iniciado        = false;
    esperandoSoltar = true;
    gd.alarmareconfig = true;
    
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      gd.currentState = STATE_MENU;
      xSemaphoreGive(stateMutex);
    }
    sendRenderCommand(RENDER_MENU);
    vTaskDelay(pdMS_TO_TICKS(300));
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(30));
}

// ═══════════════════════════════════════════════════════════════
//  DRAW: ALARMA ACTIVA
// ═══════════════════════════════════════════════════════════════
void DrawAlarmActive() {
  static bool toggle = false;
  toggle = !toggle;

  // Alterna fondo azul oscuro / verde oscuro con cada llamada
  uint16_t bg  = toggle ? tft.color565(0, 0, 80)   : tft.color565(0, 60, 20);
  uint16_t fg  = toggle ? tft.color565(0, 220, 255) : tft.color565(0, 255, 100);
  uint16_t brd = toggle ? tft.color565(0, 100, 255) : tft.color565(0, 255, 80);

  tft.fillScreen(bg);
  tft.drawRect(4,  4, SCREEN_WIDTH - 8,  SCREEN_HEIGHT - 8,  brd);
  tft.drawRect(8,  8, SCREEN_WIDTH - 16, SCREEN_HEIGHT - 16, brd);

  tft.setTextColor(fg);
  tft.drawString("!! ALARMA !!", 120 - (12 * 6), 30, 4);
  tft.drawFastHLine(20, 75, 280, brd);

  // Ícono tipo "onda de alerta"
  tft.drawCircle(160, 130, 40, brd);
  tft.drawCircle(160, 130, 28, fg);
  tft.fillCircle(160, 130, 14, fg);

  tft.drawFastHLine(20, 175, 280, brd);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("PRESIONE SELECT",  160 - (15 * 6), 170, 2);
  tft.drawString("PARA TERMINARLA", 160 - (15 * 6), 200, 2);
}
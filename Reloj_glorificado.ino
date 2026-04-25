//LIBRERIAS
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "time.h"
//CONSTANTES
#define NAV_COOLDOWN_MS 300
#define DEBOUNCE_MS     200
#define SCREEN_WIDTH 320
#define BTNUP_PIN 19
#define BTNDOWN_PIN 21
#define BTNSEL_PIN 25
const char* ssid     = "Nico";
const char* password = "pipitariano";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5 * 3600; // -18000 seconds
const int   daylightOffset_sec = 0;    // No daylight saving time
//ENUMERACIONES 
typedef enum : uint8_t {
  STATE_MENU,
  STATE_WF,
  STATE_BT,
  STATE_ALARMCONF
} ProgrammState;

typedef enum : uint8_t {
  RENDER_MENU,
  RENDER_WIFI
} RenderCommand;

typedef struct {
  ProgrammState currentState;
  int menuSelection;
  bool Bluethoot;
  bool wifi;
  int hora;
  int minuto;
} AppData;

//INICIACION APP
volatile AppData gd;
TFT_eSPI tft = TFT_eSPI();

// FREERTOS
SemaphoreHandle_t stateMutex = NULL;
QueueHandle_t renderQueue = NULL;

// ─────────────────────────────────────────────
void setup() {
//iniciación
  Serial.begin(115200);
  Serial.println("Chamo");
  tft.init();
  tft.setRotation(1); 

//valores iniciales
  gd.currentState = STATE_MENU;
  gd.menuSelection = 0;
  gd.wifi = false;
  gd.Bluethoot = false;

//FreeRTOS
  renderQueue = xQueueCreate(3, sizeof(RenderCommand));
  stateMutex  = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(logicTask, "LogicTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(drawTask,  "DrawTask",  4096, NULL, 1, NULL, 0);

  pinMode(BTNUP_PIN, INPUT_PULLUP);
  pinMode(BTNDOWN_PIN, INPUT_PULLUP);
  pinMode(BTNSEL_PIN, INPUT_PULLUP);

  // 🔥 render inicial
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
    }

    switch (state) {
      case STATE_MENU:
        handleMenu();
        break;
      case STATE_WF:
        handleWifi();
        break;
    }
  }
}

// ─────────────────────────────────────────────
void drawTask(void *p) {
  struct tm timeinfo;
  RenderCommand cmd;
  static int lastSel = -1;  // 🔥 anti-flicker

  for (;;) {
    if (xQueueReceive(renderQueue, &cmd, portMAX_DELAY)) {

      if (cmd == RENDER_MENU) {
        int sel;
        int horaN;
        int minutoN;
        if (getLocalTime(&timeinfo)) { 
          horaN = timeinfo.tm_hour; 
          minutoN = timeinfo.tm_min;
        }
        if (xSemaphoreTake(stateMutex, 10)) {
          sel = gd.menuSelection;
          xSemaphoreGive(stateMutex);
        }

        // 🔥 SOLO dibuja si cambió
        if (sel != lastSel || horaN != gd.hora || minutoN != gd.minuto ) {
          gd.hora = horaN;
          gd.minuto = minutoN; 
          DrawMenu(sel);
          lastSel = sel;
        }
      }
      else if (cmd == RENDER_WIFI){
        DrawWifi();
      }
    }
  }
}

// FUNCIONES DE DIBUJADO─────────────────────────────────────────────
void DrawMenu(int sel){

  
  tft.fillScreen(TFT_WHITE);  // 🔥 ahora sí, pero controlado
  
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(2);
    if (gd.wifi == true) {
  char buffer[10];
  sprintf(buffer, "%02d:%02d", gd.hora, gd.minuto);
  tft.drawString(buffer, 20, 20, 7);
    }
    else{
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

void DrawWifi(){
  tft.fillScreen(TFT_WHITE);
  tft.drawString("Chamo",10,46,4);
}


// ─────────────────────────────────────────────
void handleMenu() {
  struct tm timeinfo;
  static bool navLocked = false;
  static uint32_t navLockTime = 0;
  int horaN;
  int minutoN;

  if (getLocalTime(&timeinfo)) {
    horaN = timeinfo.tm_hour;
    minutoN = timeinfo.tm_min;
  }
  if (navLocked && (millis() - navLockTime > NAV_COOLDOWN_MS))
    navLocked = false;

  bool changed = false;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE ) {

        if(horaN != gd.hora || minutoN != gd.minuto){
          changed = true;
        }
        xSemaphoreGive(stateMutex);
  }
  if (!navLocked && (digitalRead(BTNUP_PIN) == HIGH || digitalRead(BTNDOWN_PIN) == HIGH)) {
    navLocked   = true;
    navLockTime = millis();

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE ) {
      


      if(!digitalRead(BTNUP_PIN) && (gd.menuSelection < 2)){
        gd.menuSelection += 1;
        changed = true;
      }

      if(!digitalRead(BTNDOWN_PIN) && (gd.menuSelection > 0)){
        gd.menuSelection += -1;
        changed = true;
      }
      
      if(!digitalRead(BTNSEL_PIN)){
        switch(gd.menuSelection){
          case 0:
          gd.currentState = STATE_ALARMCONF;
          break;
          case 1:
          gd.currentState = STATE_WF;
          break;
          case 2: 
          gd.currentState = STATE_BT;
          break;
        } ;
      }

      xSemaphoreGive(stateMutex);
    }
  }

  // 🔥 SOLO manda render si cambió
  if (changed) {
    sendRenderCommand(RENDER_MENU);
  }

  vTaskDelay(pdMS_TO_TICKS(50));
}
void handleWifi(){

static bool FirstTime = true;
static bool Conectando = false;
static bool yaConectado = false; 

if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      //  ───── Comprobar si la conexion ya esta establecida ─────
      if(gd.wifi == true){
  
        
        Serial.println("por Aqui");
        sendRenderCommand(RENDER_MENU);
        gd.currentState = STATE_MENU;
        xSemaphoreGive(stateMutex);
      }
      // ───── iniciar conexión ─────
      if(gd.wifi == false && FirstTime == true && Conectando == false){
        sendRenderCommand(RENDER_WIFI);
        WiFi.begin(ssid, password);
        Conectando = true;
        FirstTime = false;   
      }

      // ───── detectar conexión SOLO UNA VEZ ─────
      if (WiFi.status() == WL_CONNECTED && !yaConectado) {
        Serial.println("Conectado!");

        Conectando = false;
        gd.wifi = true;
        yaConectado = true;   // 🔥 evita repetir
      }
      if(gd.wifi == true){
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        struct tm timeinfo;
      }

  // ───── mientras conecta ─────
      if(Conectando){
        Serial.print(".");
      }

  xSemaphoreGive(stateMutex);
  }
  if(gd.wifi == true){
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
}
// ─────────────────────────────────────────────
void sendRenderCommand(RenderCommand cmd) {
  xQueueSend(renderQueue, &cmd, 0);
}
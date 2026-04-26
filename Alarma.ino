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

//CONSTANTES

#define NAV_COOLDOWN_MS 100
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
//ESTADOS DEL PROGRAMA
typedef enum : uint8_t {
  STATE_MENU,
  STATE_WF,
  STATE_BT,
  STATE_ALARMCONF,
} ProgrammState;
//PETICIONES DE RENDERIZADO PARA LA TASK QUE DIBUJA

typedef enum : uint8_t {
  RENDER_MENU,
  RENDER_WIFI_CONECTANDO,
  RENDER_WIFI_CONECTADO,
  RENDER_CONECTANDO_BT
} RenderCommand;

// ESTRUCTURA QUE TIENE LAS VARIABLES QUE DEFINEN ESTADOS DE DIFERENTES PARTES DEL PROGRAMA

typedef struct {
  ProgrammState currentState;
  int menuSelection;
  bool Bluethoot;
  bool wifi;
  bool Conectando; 
  int hora;
  int minuto;
  int LastTiempotranscurrido;
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

//VALORES INICIALES
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

  // RENDER INICIAL
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
  static int lastSel = -1; 

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

        if (sel != lastSel || horaN != gd.hora || minutoN != gd.minuto ) {
          gd.hora = horaN;
          gd.minuto = minutoN; 
          DrawMenu(sel);
          lastSel = sel;
        }
      }
      else if (cmd == RENDER_WIFI_CONECTANDO){
        DrawWifi(0);
      }
      else if (cmd == RENDER_WIFI_CONECTADO){
        DrawWifi(1);
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

void DrawWifi(int modo){
  //CONECTANDO A WIFI 
  if(modo == 0){
    tft.fillScreen(TFT_WHITE);
    tft.drawString("Conectando",10,46,4);
  }
  //CONEXION ESTABLECIDA
  else{
    tft.fillScreen(TFT_WHITE);
    tft.drawString("Conectado con exito!",10,46,4);
    tft.drawString("Regresando al menu",10,46,4);
  }
}


// ─────────────────────────────────────────────
void handleMenu() {
  struct tm timeinfo;
  static bool navLocked = false;
  static uint32_t navLockTime = 0;
  int horaN;
  int minutoN;

  if (Tiempotranscurrido() > 60000  && getLocalTime(&timeinfo)) {
    horaN = timeinfo.tm_hour;
    minutoN = timeinfo.tm_min;
    gd.LastTiempotranscurrido = millis();
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

  if(!navLocked ){
    navLocked = true;
    navLockTime = millis();

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(1000)) == pdTRUE ) {
      
      

      if(!digitalRead(BTNUP_PIN) && (gd.menuSelection < 2)){
        gd.menuSelection += 1;
        Serial.println("arriba");
        changed = true;
      }

      else if(!digitalRead(BTNDOWN_PIN) && (gd.menuSelection > 0)){
        Serial.println("Abajo");
        gd.menuSelection += -1;
        changed = true;
      }
      
      else if(!digitalRead(BTNSEL_PIN)){
        Serial.println("select");
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

}

void handleWifi(){

  static bool FirstTime = true;



  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      //  ───── Comprobar si la conexion ya esta establecida ─────
      if(gd.wifi == true){
        sendRenderCommand(RENDER_WIFI_CONECTADO);
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        struct tm timeinfo;
        vTaskDelay(pdMS_TO_TICKS(1000));
        gd.currentState = STATE_MENU;
        xSemaphoreGive(stateMutex);
        sendRenderCommand(RENDER_MENU);
        
      }
      // ───── iniciar conexión ─────
      else if(gd.wifi == false){
        sendRenderCommand(RENDER_WIFI_CONECTANDO);
        WiFi.begin(ssid, password);
      }

      // ───── detectar conexión SOLO UNA VEZ ─────
      else if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Conectado!");
        gd.wifi = true;
      }

  // ───── mientras conecta ─────

    xSemaphoreGive(stateMutex);
  }
  if(gd.wifi == true){
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
}

handleBluethoot(){  
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE){
   if(!gd.Bluethoot){
     SerialBT.begin("ALARMADIBAR");
     sendRenderCommand(RENDER_CONECTANDO_BT);
   }
   
   else if(gd.Bluethoot){
    sendRenderCommand(RENDER_CONECTANDO_BT);
   }
   


  }
  

  }
// ─────────────────────────────────────────────
void sendRenderCommand(RenderCommand cmd) {
  xQueueSend(renderQueue, &cmd, 0);
  }

//utils
int Tiempotranscurrido(){
  int temp = millis() - gd.LastTiempotranscurrido;
  return temp;
}
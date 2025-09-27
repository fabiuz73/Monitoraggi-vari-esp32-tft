#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "weather_images.h" // Assicurati di avere questo file e le immagini LVGL

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// ---- METEO VARS ----
const char* ssid = "TUO_SSID";
const char* password = "TUO_PASSWORD";
String latitude = "41.14961";
String longitude = "-8.61099";
String location = "Porto";
String timezone = "Europe/Lisbon";

String current_date;
String last_weather_update;
String temperature;
String humidity;
int is_day;
int weather_code = 0;
String weather_description;

#define TEMP_CELSIUS 1

#if TEMP_CELSIUS
  String temperature_unit = "";
  const char degree_symbol[] = "\u00B0C";
#else
  String temperature_unit = "&temperature_unit=fahrenheit";
  const char degree_symbol[] = "\u00B0F";
#endif

// ---- GAS VARS ----
#define EEPROM_SIZE 512
#define ADDR_MQ7 0
#define ADDR_MQ135 4
#define ADDR_STORICO 16
#define STORICO_SIZE 60
#define MQ7_PIN 34
#define MQ135_PIN 35
#define BUZZER_PIN 27

enum StatoSistema { FERMATO, PRERISCALDAMENTO, ATTIVO, ALLARME };
StatoSistema stato_sistema = FERMATO;
unsigned long preriscaldamento_start = 0;
bool allarme_attivo = false;

lv_obj_t *mq7_label = NULL;
lv_obj_t *mq135_label = NULL;
lv_obj_t *slider_co, *slider_gas, *slider_co_val, *slider_gas_val;
lv_obj_t *icon_attivo = NULL;
bool icon_blink_state = false;
unsigned long lastIconBlink = 0;

int mq7_threshold = 400;
int mq135_threshold = 400;

uint16_t storico_mq7[STORICO_SIZE] = {0};
uint16_t storico_mq135[STORICO_SIZE] = {0};
uint8_t storico_index = 0;

unsigned long lastStorico = 0;
unsigned long lastRead = 0;
unsigned long lastStoricoUpdate = 0;
unsigned long buzzer_test_start = 0;
bool buzzer_test_active = false;

// ---- ENUM PAGINE ----
enum Pagina { MENU_PRINCIPALE, METEO, GAS, MENU_OPZIONI, MODIFICA_SOGLIE, TEST_ALLARME, STORICO };
Pagina pagina_corrente = MENU_PRINCIPALE;

// ---- TOUCH LVGL DRIVER ----
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    int x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    int y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ==== MENU PRINCIPALE ====
void menu_principale_create() {
  pagina_corrente = MENU_PRINCIPALE;
  lv_obj_clean(lv_screen_active());

  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "SELEZIONA FUNZIONE");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

  // Pulsante METEO
  lv_obj_t *btn_meteo = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_meteo, 120, 60);
  lv_obj_align(btn_meteo, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_t *label_meteo = lv_label_create(btn_meteo);
  lv_label_set_text(label_meteo, "METEO");
  lv_obj_center(label_meteo);
  lv_obj_add_event_cb(btn_meteo, meteo_create_event_cb, LV_EVENT_CLICKED, NULL);

  // Pulsante GAS
  lv_obj_t *btn_gas = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_gas, 120, 60);
  lv_obj_align(btn_gas, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_t *label_gas = lv_label_create(btn_gas);
  lv_label_set_text(label_gas, "MONITORAGGIO GAS");
  lv_obj_center(label_gas);
  lv_obj_add_event_cb(btn_gas, gas_home_create_event_cb, LV_EVENT_CLICKED, NULL);
}

// ==== METEO ====
void meteo_create_event_cb(lv_event_t * e) { meteo_create(); }
static lv_obj_t * weather_image;
static lv_obj_t * text_label_date;
static lv_obj_t * text_label_temperature;
static lv_obj_t * text_label_humidity;
static lv_obj_t * text_label_weather_description;
static lv_obj_t * text_label_time_location;

void timer_cb(lv_timer_t * timer){
  LV_UNUSED(timer);
  get_weather_data();
  get_weather_description(weather_code);
  lv_label_set_text(text_label_date, current_date.c_str());
  lv_label_set_text(text_label_temperature, String("      " + temperature + degree_symbol).c_str());
  lv_label_set_text(text_label_humidity, String("   " + humidity + "%").c_str());
  lv_label_set_text(text_label_weather_description, weather_description.c_str());
  lv_label_set_text(text_label_time_location, String("Last Update: " + last_weather_update + "  |  " + location).c_str());
}

void meteo_create() {
  pagina_corrente = METEO;
  lv_obj_clean(lv_screen_active());

  LV_IMAGE_DECLARE(image_weather_sun);
  LV_IMAGE_DECLARE(image_weather_cloud);
  LV_IMAGE_DECLARE(image_weather_rain);
  LV_IMAGE_DECLARE(image_weather_thunder);
  LV_IMAGE_DECLARE(image_weather_snow);
  LV_IMAGE_DECLARE(image_weather_night);
  LV_IMAGE_DECLARE(image_weather_temperature);
  LV_IMAGE_DECLARE(image_weather_humidity);

  get_weather_data();
  weather_image = lv_image_create(lv_screen_active());
  lv_obj_align(weather_image, LV_ALIGN_CENTER, -80, -20);
  get_weather_description(weather_code);

  text_label_date = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_date, current_date.c_str());
  lv_obj_align(text_label_date, LV_ALIGN_CENTER, 70, -70);
  lv_obj_set_style_text_font(text_label_date, &lv_font_montserrat_26, 0);
  lv_obj_set_style_text_color(text_label_date, lv_palette_main(LV_PALETTE_TEAL), 0);

  lv_obj_t * weather_image_temperature = lv_image_create(lv_screen_active());
  lv_image_set_src(weather_image_temperature, &image_weather_temperature);
  lv_obj_align(weather_image_temperature, LV_ALIGN_CENTER, 30, -25);
  text_label_temperature = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_temperature, String("      " + temperature + degree_symbol).c_str());
  lv_obj_align(text_label_temperature, LV_ALIGN_CENTER, 70, -25);
  lv_obj_set_style_text_font(text_label_temperature, &lv_font_montserrat_22, 0);

  lv_obj_t * weather_image_humidity = lv_image_create(lv_screen_active());
  lv_image_set_src(weather_image_humidity, &image_weather_humidity);
  lv_obj_align(weather_image_humidity, LV_ALIGN_CENTER, 30, 20);
  text_label_humidity = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_humidity, String("   " + humidity + "%").c_str());
  lv_obj_align(text_label_humidity, LV_ALIGN_CENTER, 70, 20);
  lv_obj_set_style_text_font(text_label_humidity, &lv_font_montserrat_22, 0);

  text_label_weather_description = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_weather_description, weather_description.c_str());
  lv_obj_align(text_label_weather_description, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_text_font(text_label_weather_description, &lv_font_montserrat_18, 0);

  text_label_time_location = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_time_location, String("Last Update: " + last_weather_update + "  |  " + location).c_str());
  lv_obj_align(text_label_time_location, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_text_font(text_label_time_location, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(text_label_time_location, lv_palette_main(LV_PALETTE_GREY), 0);

  lv_timer_t * timer = lv_timer_create(timer_cb, 600000, NULL);
  lv_timer_ready(timer);

  // Pulsante back in alto a destra con simbolo LVGL
  lv_obj_t *btn_back = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_back, 48, 48);
  lv_obj_align(btn_back, LV_ALIGN_TOP_RIGHT, -8, 8); // In alto a destra
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xCC0000), LV_PART_MAIN);
  lv_obj_set_style_radius(btn_back, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back, LV_SYMBOL_LEFT); // Simbolo freccia indietro
  lv_obj_center(label_back);
  lv_obj_add_event_cb(btn_back, menu_principale_create_event_cb, LV_EVENT_CLICKED, NULL);
}

void menu_principale_create_event_cb(lv_event_t * e) { menu_principale_create(); }

void get_weather_description(int code) {
  switch (code) {
    case 0:
      if(is_day==1) { lv_image_set_src(weather_image, &image_weather_sun); }
      else { lv_image_set_src(weather_image, &image_weather_night); }
      weather_description = "CLEAR SKY";
      break;
    case 1: 
      if(is_day==1) { lv_image_set_src(weather_image, &image_weather_sun); }
      else { lv_image_set_src(weather_image, &image_weather_night); }
      weather_description = "MAINLY CLEAR";
      break;
    case 2: 
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "PARTLY CLOUDY";
      break;
    case 3:
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "OVERCAST";
      break;
    case 45:
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "FOG";
      break;
    case 48:
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "DEPOSITING RIME FOG";
      break;
    case 51:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "DRIZZLE LIGHT INTENSITY";
      break;
    case 53:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "DRIZZLE MODERATE INTENSITY";
      break;
    case 55:
      lv_image_set_src(weather_image, &image_weather_rain); 
      weather_description = "DRIZZLE DENSE INTENSITY";
      break;
    case 56:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "FREEZING DRIZZLE LIGHT";
      break;
    case 57:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "FREEZING DRIZZLE DENSE";
      break;
    case 61:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "RAIN SLIGHT INTENSITY";
      break;
    case 63:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "RAIN MODERATE INTENSITY";
      break;
    case 65:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "RAIN HEAVY INTENSITY";
      break;
    case 66:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "FREEZING RAIN LIGHT INTENSITY";
      break;
    case 67:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "FREEZING RAIN HEAVY INTENSITY";
      break;
    case 71:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "SNOW FALL SLIGHT INTENSITY";
      break;
    case 73:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "SNOW FALL MODERATE INTENSITY";
      break;
    case 75:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "SNOW FALL HEAVY INTENSITY";
      break;
    case 77:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "SNOW GRAINS";
      break;
    case 80:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "RAIN SHOWERS SLIGHT";
      break;
    case 81:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "RAIN SHOWERS MODERATE";
      break;
    case 82:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "RAIN SHOWERS VIOLENT";
      break;
    case 85:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "SNOW SHOWERS SLIGHT";
      break;
    case 86:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "SNOW SHOWERS HEAVY";
      break;
    case 95:
      lv_image_set_src(weather_image, &image_weather_thunder);
      weather_description = "THUNDERSTORM";
      break;
    case 96:
      lv_image_set_src(weather_image, &image_weather_thunder);
      weather_description = "THUNDERSTORM SLIGHT HAIL";
      break;
    case 99:
      lv_image_set_src(weather_image, &image_weather_thunder);
      weather_description = "THUNDERSTORM HEAVY HAIL";
      break;
    default: 
      weather_description = "UNKNOWN WEATHER CODE";
      break;
  }
}

void get_weather_data() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String("http://api.open-meteo.com/v1/forecast?latitude=" + latitude + "&longitude=" + longitude + "&current=temperature_2m,relative_humidity_2m,is_day,precipitation,rain,weather_code" + temperature_unit + "&timezone=" + timezone + "&forecast_days=1");
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          const char* datetime = doc["current"]["time"];
          temperature = String(doc["current"]["temperature_2m"]);
          humidity = String(doc["current"]["relative_humidity_2m"]);
          is_day = String(doc["current"]["is_day"]).toInt();
          weather_code = String(doc["current"]["weather_code"]).toInt();
          String datetime_str = String(datetime);
          int splitIndex = datetime_str.indexOf('T');
          current_date = datetime_str.substring(0, splitIndex);
          last_weather_update = datetime_str.substring(splitIndex + 1, splitIndex + 9);
        }
      }
    }
    http.end();
  }
}

// ==== GAS ====
// Tutte le tue funzioni gas: homepage_create, menu_opzioni_create, storico_chart_create ecc.
// Devi solo aggiungere il tasto "back" in alto a sinistra

void gas_home_create_event_cb(lv_event_t * e) { homepage_create(); }

// --- Funzione Lettura Touch LVGL ---
// (già definita sopra come touchscreen_read)

// --- Salva storico ---
void salva_storico(uint16_t mq7, uint16_t mq135) {
  storico_mq7[storico_index] = mq7;
  storico_mq135[storico_index] = mq135;
  int addr = ADDR_STORICO + storico_index * 4;
  EEPROM.put(addr, mq7);
  EEPROM.put(addr+2, mq135);
  EEPROM.commit();
  storico_index = (storico_index + 1) % STORICO_SIZE;
}

// --- Carica storico all'avvio ---
void carica_storico() {
  for (uint8_t i = 0; i < STORICO_SIZE; i++) {
    int addr = ADDR_STORICO + i * 4;
    uint16_t val1, val2;
    EEPROM.get(addr, val1);
    EEPROM.get(addr+2, val2);
    storico_mq7[i] = val1;
    storico_mq135[i] = val2;
  }
}

// --- Calcola min/max/media su array ---
void calc_stats(const uint16_t *arr, uint8_t len, uint16_t *out_min, uint16_t *out_max, float *out_media) {
  if (len == 0) { *out_min=0; *out_max=0; *out_media=0; return; }
  uint16_t minv = arr[0], maxv = arr[0];
  uint32_t sum = arr[0];
  for (uint8_t i=1; i<len; i++) {
    if (arr[i] < minv) minv = arr[i];
    if (arr[i] > maxv) maxv = arr[i];
    sum += arr[i];
  }
  *out_min = minv;
  *out_max = maxv;
  *out_media = (float)sum / len;
}

// --- Visualizzazione stato sistema con icona lampeggiante (in alto a destra) ---
void mostra_icona_attivo(lv_obj_t *parent) {
  if (icon_attivo) {
    lv_obj_del(icon_attivo);
    icon_attivo = NULL;
  }
  if (stato_sistema == ATTIVO) {
    icon_attivo = lv_obj_create(parent);
    lv_obj_set_size(icon_attivo, 28, 28);
    lv_obj_set_style_radius(icon_attivo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon_attivo, icon_blink_state ? lv_color_hex(0x00CC44) : lv_color_hex(0xF7F7F7), LV_PART_MAIN);
    lv_obj_set_style_border_color(icon_attivo, lv_color_hex(0x00CC44), LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_attivo, 2, LV_PART_MAIN);
    lv_obj_align(icon_attivo, LV_ALIGN_TOP_RIGHT, -16, 16);
  }
}

void menu_opzioni_create_event_cb(lv_event_t * e) { menu_opzioni_create(); }
void storico_chart_create_event_cb(lv_event_t * e) { storico_chart_create(); }
void homepage_create_event_cb(lv_event_t * e) { homepage_create(); }
void conferma_soglie_event_cb(lv_event_t * e) {
  EEPROM.put(ADDR_MQ7, mq7_threshold);
  EEPROM.put(ADDR_MQ135, mq135_threshold);
  EEPROM.commit();
  menu_opzioni_create();
}
void modifica_soglie_event_cb(lv_event_t * e) { modifica_soglie_create(); }
void test_allarme_event_cb(lv_event_t * e) {
  pagina_corrente = TEST_ALLARME;
  digitalWrite(BUZZER_PIN, HIGH);
  buzzer_test_start = millis();
  buzzer_test_active = true;

  lv_obj_clean(lv_screen_active());
  lv_obj_t *title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "TEST ALLARME");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *msg = lv_label_create(lv_screen_active());
  lv_label_set_text(msg, "Il buzzer suona per 2 secondi...");
  lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *btn_back = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_back, 90, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xCC0000), LV_PART_MAIN);
  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back, "INDIETRO");
  lv_obj_center(label_back);
  lv_obj_add_event_cb(btn_back, homepage_create_event_cb, LV_EVENT_CLICKED, NULL);
}

// --- Homepage con icona attivo lampeggiante e valori sempre visibili ---
void homepage_create() {
  pagina_corrente = GAS;
  mq7_label = NULL;
  mq135_label = NULL;
  lv_obj_clean(lv_screen_active());

  // Allarme visivo: schermo rosso in allarme
  if (stato_sistema == ALLARME) {
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xFF3333), LV_PART_MAIN);
    lv_obj_t *alarm_label = lv_label_create(lv_screen_active());
    lv_label_set_text(alarm_label, "!!! ALLARME GAS !!!");
    lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(alarm_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(alarm_label, LV_ALIGN_TOP_MID, 0, 54);
  } else {
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xF7F7F7), LV_PART_MAIN);
  }

  // Titolo
  lv_obj_t *title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "ALLARME GAS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xAA0000), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

  // Icona attivo in alto a destra, lampeggia se attivo
  mostra_icona_attivo(lv_screen_active());

  // Labels valori sensori: sempre visibili con valori in tempo reale
  mq7_label = lv_label_create(lv_screen_active());
  lv_label_set_text(mq7_label, "CO (Monossido di Carbonio): ---");
  lv_obj_align(mq7_label, LV_ALIGN_LEFT_MID, 12, -30);

  mq135_label = lv_label_create(lv_screen_active());
  lv_label_set_text(mq135_label, "Gas Tossici: ---");
  lv_obj_align(mq135_label, LV_ALIGN_LEFT_MID, 12, 20);

  // Preriscaldamento: messaggio animato
  if (stato_sistema == PRERISCALDAMENTO) {
    lv_obj_t *preheat = lv_label_create(lv_screen_active());
    lv_label_set_text(preheat, "Preriscaldamento sensori in corso...");
    lv_obj_set_style_text_font(preheat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(preheat, lv_color_hex(0xFF9900), 0);
    lv_obj_align(preheat, LV_ALIGN_CENTER, 0, 40);
  }

  // Pulsanti
  lv_obj_t *btn_start = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_start, 90, 44);
  lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, 16, -16);
  lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x00CC44), LV_PART_MAIN);
  lv_obj_t *label_start = lv_label_create(btn_start);
  lv_label_set_text(label_start, "AVVIO");
  lv_obj_center(label_start);
  lv_obj_add_event_cb(btn_start, start_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_stop = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_stop, 90, 44);
  lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0xCC0000), LV_PART_MAIN);
  lv_obj_t *label_stop = lv_label_create(btn_stop);
  lv_label_set_text(label_stop, "STOP");
  lv_obj_center(label_stop);
  lv_obj_add_event_cb(btn_stop, stop_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_options = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_options, 90, 44);
  lv_obj_align(btn_options, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
  lv_obj_set_style_bg_color(btn_options, lv_color_hex(0x0055CC), LV_PART_MAIN);
  lv_obj_t *label_options = lv_label_create(btn_options);
  lv_label_set_text(label_options, "OPZIONI");
  lv_obj_center(label_options);
  lv_obj_add_event_cb(btn_options, options_event_cb, LV_EVENT_CLICKED, NULL);

  // Pulsante back in alto a sinistra con simbolo LVGL
  lv_obj_t *btn_back = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_back, 48, 48);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 8); // In alto a sinistra
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xCC0000), LV_PART_MAIN);
  lv_obj_set_style_radius(btn_back, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back, LV_SYMBOL_LEFT);
  lv_obj_center(label_back);
  lv_obj_add_event_cb(btn_back, menu_principale_create_event_cb, LV_EVENT_CLICKED, NULL);
}

// --- il resto del codice rimane invariato rispetto alla tua versione precedente: tutte le funzioni gas, opzioni, storico, soglie, setup, loop, ecc.

// --- Menu Opzioni principale ---
void menu_opzioni_create() {
  pagina_corrente = MENU_OPZIONI;
  mq7_label = NULL; mq135_label = NULL;
  lv_obj_clean(lv_screen_active());

  lv_obj_t *title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "MENU OPZIONI");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 14);

  lv_obj_t *btn_modifica_soglie = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_modifica_soglie, 140, 44);
  lv_obj_align(btn_modifica_soglie, LV_ALIGN_TOP_LEFT, 16, 54);
  lv_obj_set_style_bg_color(btn_modifica_soglie, lv_color_hex(0x00AEEF), LV_PART_MAIN);
  lv_obj_t *label_modifica = lv_label_create(btn_modifica_soglie);
  lv_label_set_text(label_modifica, "MODIFICA SOGLIE");
  lv_obj_center(label_modifica);
  lv_obj_add_event_cb(btn_modifica_soglie, modifica_soglie_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_test_allarme = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_test_allarme, 140, 44);
  lv_obj_align(btn_test_allarme, LV_ALIGN_TOP_LEFT, 16, 110);
  lv_obj_set_style_bg_color(btn_test_allarme, lv_color_hex(0xFF9900), LV_PART_MAIN);
  lv_obj_t *label_test = lv_label_create(btn_test_allarme);
  lv_label_set_text(label_test, "TEST ALLARME");
  lv_obj_center(label_test);
  lv_obj_add_event_cb(btn_test_allarme, test_allarme_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_storico = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_storico, 140, 44);
  lv_obj_align(btn_storico, LV_ALIGN_TOP_LEFT, 16, 166);
  lv_obj_set_style_bg_color(btn_storico, lv_color_hex(0x00CC44), LV_PART_MAIN);
  lv_obj_t *label_storico = lv_label_create(btn_storico);
  lv_label_set_text(label_storico, "STORICO");
  lv_obj_center(label_storico);
  lv_obj_add_event_cb(btn_storico, storico_chart_create_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_back = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_back, 90, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xCC0000), LV_PART_MAIN);
  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back, "INDIETRO");
  lv_obj_center(label_back);
  lv_obj_add_event_cb(btn_back, homepage_create_event_cb, LV_EVENT_CLICKED, NULL);
}

void options_event_cb(lv_event_t * e) { menu_opzioni_create(); }

void start_event_cb(lv_event_t * e) {
  stato_sistema = PRERISCALDAMENTO;
  preriscaldamento_start = millis();
  allarme_attivo = false;
  digitalWrite(BUZZER_PIN, LOW);
  homepage_create();
}
void stop_event_cb(lv_event_t * e) {
  stato_sistema = FERMATO;
  allarme_attivo = false;
  digitalWrite(BUZZER_PIN, LOW);
  homepage_create();
}

// --- Modifica soglie ---
void modifica_soglie_create() {
  pagina_corrente = MODIFICA_SOGLIE;
  mq7_label = NULL; mq135_label = NULL;
  lv_obj_clean(lv_screen_active());

  lv_obj_t *title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "MODIFICA SOGLIE");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *label_co = lv_label_create(lv_screen_active());
  lv_label_set_text(label_co, "Soglia CO:");
  lv_obj_align(label_co, LV_ALIGN_LEFT_MID, 10, -40);

  slider_co = lv_slider_create(lv_screen_active());
  lv_slider_set_range(slider_co, 100, 1000);
  lv_slider_set_value(slider_co, mq7_threshold, LV_ANIM_OFF);
  lv_obj_set_width(slider_co, 140);
  lv_obj_align(slider_co, LV_ALIGN_LEFT_MID, 10, -10);
  lv_obj_add_event_cb(slider_co, slider_co_cb, LV_EVENT_VALUE_CHANGED, NULL);

  slider_co_val = lv_label_create(lv_screen_active());
  char buf[12]; sprintf(buf, "%d", mq7_threshold);
  lv_label_set_text(slider_co_val, buf);
  lv_obj_align_to(slider_co_val, slider_co, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  lv_obj_t *label_gas = lv_label_create(lv_screen_active());
  lv_label_set_text(label_gas, "Soglia Gas Tossici:");
  lv_obj_align(label_gas, LV_ALIGN_LEFT_MID, 10, 30);

  slider_gas = lv_slider_create(lv_screen_active());
  lv_slider_set_range(slider_gas, 100, 1000);
  lv_slider_set_value(slider_gas, mq135_threshold, LV_ANIM_OFF);
  lv_obj_set_width(slider_gas, 140);
  lv_obj_align(slider_gas, LV_ALIGN_LEFT_MID, 10, 60);
  lv_obj_add_event_cb(slider_gas, slider_gas_cb, LV_EVENT_VALUE_CHANGED, NULL);

  slider_gas_val = lv_label_create(lv_screen_active());
  sprintf(buf, "%d", mq135_threshold);
  lv_label_set_text(slider_gas_val, buf);
  lv_obj_align_to(slider_gas_val, slider_gas, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  lv_obj_t *btn_save = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_save, 90, 36);
  lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x00CC44), LV_PART_MAIN);
  lv_obj_t *label_save = lv_label_create(btn_save);
  lv_label_set_text(label_save, "CONFERMA");
  lv_obj_center(label_save);
  lv_obj_add_event_cb(btn_save, conferma_soglie_event_cb, LV_EVENT_CLICKED, NULL);
}

void slider_co_cb(lv_event_t * e) {
  mq7_threshold = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  char buf[12]; sprintf(buf, "%d", mq7_threshold);
  lv_label_set_text(slider_co_val, buf);
}
void slider_gas_cb(lv_event_t * e) {
  mq135_threshold = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  char buf[12]; sprintf(buf, "%d", mq135_threshold);
  lv_label_set_text(slider_gas_val, buf);
}

// --- Visualizzazione statistiche storico ---
void storico_chart_create() {
  pagina_corrente = STORICO;
  mq7_label = NULL; mq135_label = NULL;
  lv_obj_clean(lv_screen_active());

  lv_obj_t *title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "STATISTICHE STORICO GAS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  const uint8_t n_stat = 10;
  uint16_t buf_mq7[n_stat], buf_mq135[n_stat];
  for (uint8_t i = 0; i < n_stat; i++) {
    uint8_t idx = (storico_index + STORICO_SIZE - n_stat + i) % STORICO_SIZE;
    buf_mq7[i] = storico_mq7[idx];
    buf_mq135[i] = storico_mq135[idx];
  }

  uint16_t min_mq7, max_mq7, min_mq135, max_mq135;
  float media_mq7, media_mq135;
  calc_stats(buf_mq7, n_stat, &min_mq7, &max_mq7, &media_mq7);
  calc_stats(buf_mq135, n_stat, &min_mq135, &max_mq135, &media_mq135);

  lv_obj_t *label_co = lv_label_create(lv_screen_active());
  lv_label_set_text(label_co, "CO (Monossido di Carbonio)");
  lv_obj_set_style_text_font(label_co, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(label_co, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(label_co, LV_ALIGN_TOP_LEFT, 16, 48);

  char val_min[32], val_max[32], val_media[32];
  snprintf(val_min, sizeof(val_min), "Minimo: %u", min_mq7);
  snprintf(val_max, sizeof(val_max), "Massimo: %u", max_mq7);
  snprintf(val_media, sizeof(val_media), "Media: %.1f", media_mq7);

  lv_obj_t *label_min_co = lv_label_create(lv_screen_active());
  lv_label_set_text(label_min_co, val_min);
  lv_obj_set_style_text_color(label_min_co, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(label_min_co, LV_ALIGN_TOP_LEFT, 32, 80);

  lv_obj_t *label_max_co = lv_label_create(lv_screen_active());
  lv_label_set_text(label_max_co, val_max);
  lv_obj_set_style_text_color(label_max_co, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(label_max_co, LV_ALIGN_TOP_LEFT, 32, 104);

  lv_obj_t *label_media_co = lv_label_create(lv_screen_active());
  lv_label_set_text(label_media_co, val_media);
  lv_obj_set_style_text_color(label_media_co, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(label_media_co, LV_ALIGN_TOP_LEFT, 32, 128);

  lv_obj_t *label_gas = lv_label_create(lv_screen_active());
  lv_label_set_text(label_gas, "Gas Tossici (MQ-135)");
  lv_obj_set_style_text_font(label_gas, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(label_gas, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_align(label_gas, LV_ALIGN_TOP_LEFT, 16, 164);

  snprintf(val_min, sizeof(val_min), "Minimo: %u", min_mq135);
  snprintf(val_max, sizeof(val_max), "Massimo: %u", max_mq135);
  snprintf(val_media, sizeof(val_media), "Media: %.1f", media_mq135);

  lv_obj_t *label_min_gas = lv_label_create(lv_screen_active());
  lv_label_set_text(label_min_gas, val_min);
  lv_obj_set_style_text_color(label_min_gas, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_align(label_min_gas, LV_ALIGN_TOP_LEFT, 32, 196);

  lv_obj_t *label_max_gas = lv_label_create(lv_screen_active());
  lv_label_set_text(label_max_gas, val_max);
  lv_obj_set_style_text_color(label_max_gas, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_align(label_max_gas, LV_ALIGN_TOP_LEFT, 32, 220);

  lv_obj_t *label_media_gas = lv_label_create(lv_screen_active());
  lv_label_set_text(label_media_gas, val_media);
  lv_obj_set_style_text_color(label_media_gas, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_align(label_media_gas, LV_ALIGN_TOP_LEFT, 32, 244);

  lv_obj_t *btn_back = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_back, 90, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xCC0000), LV_PART_MAIN);
  lv_obj_t *label_back = lv_label_create(btn_back);
  lv_label_set_text(label_back, "INDIETRO");
  lv_obj_center(label_back);
  lv_obj_add_event_cb(btn_back, menu_opzioni_create_event_cb, LV_EVENT_CLICKED, NULL);
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  delay(500);

  WiFi.begin(ssid, password);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lv_init();

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);

  lv_display_t *disp;
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  carica_storico();

  menu_principale_create();
}

// ==== LOOP ====
void loop() {
  lv_task_handler();
  lv_tick_inc(5);

  // --- Gas: gestione blink icona, storico, allarme, ecc. ---
  if (pagina_corrente == GAS && stato_sistema == ATTIVO) {
    if (millis() - lastIconBlink > 500) {
      icon_blink_state = !icon_blink_state;
      mostra_icona_attivo(lv_screen_active());
      lastIconBlink = millis();
    }
  }

  // Preriscaldamento
  if (pagina_corrente == GAS && stato_sistema == PRERISCALDAMENTO) {
    if (millis() - preriscaldamento_start >= 10000) {
      stato_sistema = ATTIVO;
      homepage_create();
    }
    return;
  }

  // Visualizza sempre i valori in tempo reale sulla homepage gas
  if (pagina_corrente == GAS && mq7_label && mq135_label && millis() - lastRead > 200) {
    int mq7_value = analogRead(MQ7_PIN);
    int mq135_value = analogRead(MQ135_PIN);

    char buf[64];
    snprintf(buf, sizeof(buf), "CO (Monossido di Carbonio): %d", mq7_value);
    lv_label_set_text(mq7_label, buf);

    snprintf(buf, sizeof(buf), "Gas Tossici: %d", mq135_value);
    lv_label_set_text(mq135_label, buf);

    // Allarme solo se attivo
    if (stato_sistema == ATTIVO) {
      if (mq7_value > mq7_threshold || mq135_value > mq135_threshold) {
        stato_sistema = ALLARME;
        allarme_attivo = true;
        digitalWrite(BUZZER_PIN, HIGH);
        homepage_create();
      } else {
        digitalWrite(BUZZER_PIN, LOW);
      }
    }
    lastRead = millis();
  }

  if (stato_sistema == ALLARME) {
    digitalWrite(BUZZER_PIN, HIGH);
  }

  if (millis() - lastStorico > 10000) {
    int mq7_value = analogRead(MQ7_PIN);
    int mq135_value = analogRead(MQ135_PIN);
    salva_storico(mq7_value, mq135_value);
    lastStorico = millis();

    if (pagina_corrente == STORICO) {
      storico_chart_create();
      lastStoricoUpdate = millis();
    }
  }
  if (pagina_corrente == STORICO && millis() - lastStoricoUpdate > 10000) {
    storico_chart_create();
    lastStoricoUpdate = millis();
  }

  if (buzzer_test_active) {
    if (millis() - buzzer_test_start > 2000) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzer_test_active = false;
    }
  }
}
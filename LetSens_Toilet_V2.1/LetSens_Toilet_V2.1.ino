/*
  =====================================================================
  LETSENS - AIoT TOILET MONITORING SYSTEM
  Firmware v2.1 (ESP32)
  =====================================================================

  Fitur yang ditambahkan pada versi ini:
  - WiFiManager: konfigurasi WiFi via portal (auto-AP jika belum ada
    kredensial / gagal konek). AP name: "LETSENS-Setup"
  - Koneksi ke MQTT Broker HiveMQ Cloud (TLS, port 8883)
  - Sensor Gas (MQ135) & Suhu/Kelembapan (DHT11)     -> data REAL
  - Sensor Intensitas Cahaya (lux) via BH1750FVI     -> data REAL
  - Sensor PIR (kehadiran + durasi di toilet)        -> data DUMMY (simulasi)
  - Web server lokal (dashboard) yang menampilkan seluruh data sensor
    beserta status pengiriman MQTT (berhasil/gagal)
  - Endpoint /resetwifi untuk menghapus kredensial WiFi & membuka
    kembali portal konfigurasi

  Perubahan di v2:
  - Interval publish MQTT diubah dari 5 detik -> 30 detik (standar
    umum untuk data monitoring lingkungan yang non-realtime-critical)
  - Payload MQTT ditambahkan field "timestamp" (unix epoch UTC) dan
    "time" (format WIB "YYYY-MM-DD HH:MM:SS"), disinkronkan via NTP
  - Device ID disederhanakan jadi "LETSENS-01" (manual, bukan dari
    MAC address lagi)

  Perubahan di v2.1:
  - Sensor Intensitas Cahaya sekarang pakai modul BH1750FVI asli
    (I2C, alamat default 0x23), menggantikan simulasi random().
    Modul ini berbagi bus I2C yang sama dengan OLED (SDA=21, SCL=22).
  - Ditambahkan flag "lightOk" untuk mendeteksi kalau BH1750 gagal
    diinisialisasi / tidak terbaca, supaya dashboard & payload MQTT
    tidak diam-diam menampilkan angka lux yang salah.
  - Fungsi simulateLight() dihapus, diganti readLight().

  LIBRARY YANG DIPERLUKAN (install via Library Manager):
  - WiFiManager          by tzapu
  - PubSubClient         by Nick O'Leary
  - ArduinoJson          by Benoit Blanchon (v6/v7)
  - Adafruit GFX Library
  - Adafruit SSD1306
  - DHT sensor library   by Adafruit  (+ Adafruit Unified Sensor)
  - BH1750               by Christopher Laws (search "BH1750" di Library Manager)
  - WiFiClientSecure & WebServer sudah termasuk di ESP32 core

  CATATAN KEAMANAN TLS:
  - Kode ini memakai wifiClientSecure.setInsecure() supaya proses
    handshake TLS ke HiveMQ Cloud tidak perlu certificate pinning.
    Ini umum dipakai untuk prototipe/development. Untuk produksi,
    sebaiknya pin root CA certificate HiveMQ (ISRG Root X1 / Let's
    Encrypt) agar koneksi tervalidasi penuh.

  PIC   : Ibrahim
  Proyek: LETSENS - AIoT TOILET MONITORING SYSTEM
  =====================================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>          // tzapu/WiFiManager
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>              // mDNS - akses dashboard via hostname .local (sudah termasuk di ESP32 core)
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <BH1750.h>               // BH1750FVI - sensor intensitas cahaya (I2C)
#include <time.h>                 // untuk sinkronisasi waktu NTP (timestamp payload)


// =====================================================
// PIN DEFINITION
// =====================================================

#define DHT_PIN 4
#define DHT_TYPE DHT11

#define MQ135_PIN 34

// -----------------------------------------------------
// MQ135 - PEMBAGI TEGANGAN EKSTERNAL DI JALUR AOUT
// -----------------------------------------------------
// Rangkaian yang dipakai:  AOUT --[R1=20k]-- GPIO34 --[R2=10k]-- GND
// Tujuannya menurunkan tegangan AOUT (yang bisa mendekati VCC 5V modul)
// supaya aman masuk ke ADC ESP32 (maks aman ~3.3V).
const float MQ135_DIVIDER_R1 = 20000.0;   // ohm, dari AOUT ke GPIO34
const float MQ135_DIVIDER_R2 = 10000.0;   // ohm, dari GPIO34 ke GND
// Faktor pengali untuk mengembalikan tegangan AOUT asli dari tegangan yang terbaca di GPIO34
const float MQ135_DIVIDER_FACTOR = (MQ135_DIVIDER_R1 + MQ135_DIVIDER_R2) / MQ135_DIVIDER_R2; // = 3.0

const float MQ135_VCC        = 5.0;     // tegangan supply modul MQ135 (V) - cek modul, umumnya 5V
const float ADC_VREF         = 3.3;     // tegangan referensi ADC ESP32 (V)
const float ADC_RESOLUTION   = 4095.0;  // ESP32 ADC 12-bit (0-4095)

// PENTING: RL_VALUE = resistor beban BAWAAN MODUL MQ135 (di PCB modul), BUKAN R1/R2 pembagi di atas!
// Untuk modul MQ135 "Flying Fish" (breakout LM393 yang umum dijual), RL bawaan biasanya 1kΩ
// (SMD resistor kecil dekat sensor, kadang tertulis kode "102" = 1kΩ). Nilai default di bawah
// sudah sesuai untuk tipe ini. Kalau modul Anda beda merk/tipe, cek label di PCB atau ukur
// langsung pakai multimeter (modul dalam kondisi mati/tanpa VCC) untuk hasil ppm yang akurat.
const float MQ135_RL_VALUE = 1.0;   // dalam kOhm - default untuk modul Flying Fish MQ135

// Rasio Rs/Ro standar MQ135 saat berada di udara bersih (nilai baku dari datasheet)
const float MQ135_RO_CLEAN_AIR_FACTOR = 3.6;

// Konstanta kurva regresi NH3 (amonia) MQ135: ppm = a * (Rs/Ro)^b
// Diambil dari hasil fitting kurva grafik datasheet yang umum dipakai komunitas/tutorial —
// bukan angka presisi pabrikan per unit. Kalibrasi ulang dengan gas NH3 konsentrasi diketahui
// kalau butuh akurasi tinggi (misal pakai gas kalibrasi/analyzer pembanding).
const float MQ135_NH3_CURVE_A = 102.2;
const float MQ135_NH3_CURVE_B = -2.473;

#define LED_GREEN 25
#define LED_YELLOW 26
#define LED_RED 27

// Placeholder pin untuk sensor yang masih dummy.
// Nanti tinggal sambungkan sensor asli & ganti fungsi
// simulatePIR() dengan pembacaan pin ini.
#define PIR_PIN 32     // belum dipakai (masih dummy)

// BH1750FVI (sensor cahaya) - I2C, berbagi bus yang sama dengan OLED
// (SDA=21, SCL=22, lihat OLED_SDA/OLED_SCL di bawah).
// Alamat default 0x23 (pin ADDR sensor ke GND / mengambang).
// Kalau pin ADDR ditarik ke VCC, alamatnya jadi 0x5C -> ganti di BH1750_I2C_ADDR.
#define BH1750_I2C_ADDR 0x23

// Tombol BOOT (GPIO0) - ditahan saat power on untuk
// menghapus kredensial WiFi & membuka portal konfigurasi ulang.
#define WIFI_RESET_BUTTON 0

// OLED
#define OLED_SDA 21
#define OLED_SCL 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT_TYPE);
BH1750 lightMeter(BH1750_I2C_ADDR);


// =====================================================
// IDENTITAS DEVICE
// =====================================================

// Device ID dibuat simple & manual (bukan dari MAC address lagi).
// Kalau nanti ada lebih dari 1 device/toilet, ganti angka di
// belakang secara manual per device (LETSENS-01, LETSENS-02, dst)
// supaya tetap unik satu sama lain.
const char* DEVICE_ID = "LETSENS-01";


// =====================================================
// mDNS (DNS LOKAL)
// =====================================================

// Hostname untuk akses dashboard lokal, contoh: http://letsens.local
// Tidak perlu tahu IP device lagi selama masih di jaringan WiFi yang sama.
// Kalau device lebih dari 1, ganti jadi unik per device (letsens01, letsens02, dst)
const char* MDNS_HOSTNAME = "letsens";


// =====================================================
// WIFI MANAGER
// =====================================================

WiFiManager wm;
const char* WIFI_AP_NAME = "LETSENS-Setup";
const char* WIFI_AP_PASSWORD = "letsens123";   // min 8 karakter


// =====================================================
// NTP (SINKRONISASI WAKTU) - untuk timestamp payload
// =====================================================

const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 7 * 3600;   // WIB = UTC+7
const int   DAYLIGHT_OFFSET_SEC = 0;


// =====================================================
// KONFIGURASI MQTT (HiveMQ Cloud)
// =====================================================

const char* MQTT_HOST = "fcb0ad941e3f418f8dc1d16332c8fcb9.s1.eu.hivemq.cloud";
const int   MQTT_PORT = 8883;                  // TLS
const char* MQTT_USERNAME = "letsens";
const char* MQTT_PASSWORD = "letsens123";

const char* MQTT_TOPIC_DATA   = "letsens/toilet/sensordata";
const char* MQTT_TOPIC_STATUS = "letsens/toilet/status";      // LWT (online/offline)

String mqttClientId;   // dibuat otomatis dari MAC address saat setup()

WiFiClientSecure wifiClientSecure;
PubSubClient mqttClient(wifiClientSecure);

bool mqttPublishSuccess = false;
String mqttLastError = "-";
unsigned long mqttLastPublishMillis = 0;


// =====================================================
// WEB SERVER LOKAL
// =====================================================

WebServer server(80);


// =====================================================
// PARAMETER THRESHOLD
// =====================================================

const float TEMP_WARNING  = 33.0;

// Ambang batas amonia (NH3) dalam ppm.
// Referensi umum: bau mulai tercium manusia sekitar ~5 ppm; batas paparan kerja 8 jam
// (OSHA PEL time-weighted average) ada di ~25 ppm. Sesuaikan dengan kondisi lapangan Anda.
const float GAS_WARNING_PPM   = 5.0;
const float GAS_CRITICAL_PPM  = 25.0;


// =====================================================
// DATA SENSOR (GLOBAL STATE)
// =====================================================

struct SensorData {
  // DHT11
  float temperature = 0;
  float humidity = 0;
  bool  dhtOk = false;

  // MQ135 (Gas Amonia / NH3)
  int   gasRaw = 0;       // nilai ADC mentah (0-4095) di GPIO34, SUDAH melalui pembagi tegangan
  float gasVoltage = 0;   // tegangan asli AOUT sensor (V), setelah dikoreksi pembagi tegangan
  float gasRs = 0;        // resistansi sensor saat ini (kOhm)
  float gasRatio = 0;     // Rs/Ro
  float gasPPM = 0;       // estimasi konsentrasi amonia / NH3 (ppm)

  // PIR (dummy)
  bool  pirPresence = false;
  unsigned long pirDurationSec = 0;

  // Light (BH1750FVI - REAL)
  float lightLux = 0;
  bool  lightOk = false;

  // Status akhir
  String status = "NORMAL";
};

SensorData sensorData;

float mq135_Ro = 0;   // kOhm, hasil kalibrasi baseline (Rs di udara bersih / faktor clean-air)


// =====================================================
// TIMER NON-BLOCKING
// =====================================================

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000;      // baca sensor tiap 2 detik

unsigned long lastMqttPublish = 0;
const unsigned long MQTT_PUBLISH_INTERVAL = 30000; // publish tiap 30 detik (standar industri untuk data monitoring non-realtime-critical)

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

unsigned long previousMillisBlink = 0;
const unsigned long BLINK_INTERVAL = 500;
bool redState = false;


// =====================================================
// PIR (DUMMY) STATE
// =====================================================

unsigned long pirSessionStart = 0;


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("================================");
  Serial.println("      ESP32 TOILET MONITOR v2.1");
  Serial.println("================================");

  // -------------------------
  // DHT11
  // -------------------------
  dht.begin();

  // -------------------------
  // LED
  // -------------------------
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  // -------------------------
  // TOMBOL RESET WIFI
  // -------------------------
  pinMode(WIFI_RESET_BUTTON, INPUT_PULLUP);

  // -------------------------
  // ADC
  // -------------------------
  analogReadResolution(12);

  // -------------------------
  // RANDOM SEED (untuk dummy sensor)
  // -------------------------
  randomSeed(analogRead(35));

  // -------------------------
  // I2C BUS (dipakai bersama OLED & BH1750)
  // -------------------------
  Wire.begin(OLED_SDA, OLED_SCL);

  // -------------------------
  // BH1750FVI (sensor cahaya)
  // -------------------------
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    sensorData.lightOk = true;
    Serial.println("BH1750 siap.");
  } else {
    sensorData.lightOk = false;
    Serial.println("ERROR: BH1750 tidak ditemukan! Cek wiring/alamat I2C.");
  }

  // -------------------------
  // OLED
  // -------------------------
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED tidak ditemukan!");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 5);
  display.println("TOILET");
  display.setCursor(20, 30);
  display.println("MONITOR");
  display.display();
  delay(1500);

  // -------------------------
  // CEK TOMBOL RESET WIFI
  // -------------------------
  if (digitalRead(WIFI_RESET_BUTTON) == LOW) {
    Serial.println("Tombol BOOT ditahan -> menghapus kredensial WiFi...");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("Reset WiFi settings...");
    display.display();
    wm.resetSettings();
    delay(1000);
  }

  // -------------------------
  // WIFI MANAGER
  // -------------------------
  setupWiFiManager();

  // -------------------------
  // ID PERANGKAT (SIMPLE, MANUAL)
  // -------------------------
  mqttClientId = DEVICE_ID;

  Serial.print("Device ID  : ");
  Serial.println(mqttClientId);

  // -------------------------
  // SINKRONISASI WAKTU (NTP)
  // -------------------------
  setupTime();

  // -------------------------
  // SETUP MQTT
  // -------------------------
  setupMQTT();

  // -------------------------
  // SETUP WEB SERVER
  // -------------------------
  setupWebServer();

  // -------------------------
  // MQ135 WARM-UP
  // -------------------------
  Serial.println();
  Serial.println("MQ135 sedang dipanaskan... tunggu 60 detik.");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MQ135 WARM-UP");
  display.setCursor(0, 20);
  display.println("Please wait...");
  display.setCursor(0, 40);
  display.println("60 seconds");
  display.display();

  // Selama warm-up tetap layani web server & mqtt loop
  // supaya tidak benar-benar "buta" 60 detik.
  unsigned long warmupStart = millis();
  while (millis() - warmupStart < 60000) {
    server.handleClient();
    mqttClient.loop();
    delay(20);
  }

  // -------------------------
  // KALIBRASI MQ135
  // -------------------------
  calibrateMQ135();

  Serial.println();
  Serial.println("Kalibrasi selesai. Monitoring dimulai.");
  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // Web server & MQTT harus selalu diproses tiap iterasi
  server.handleClient();

  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnectAttempt = now;
      connectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // LED merah berkedip non-blocking saat CRITICAL
  if (sensorData.status == "CRITICAL") {
    blinkRed();
  }

  // Baca sensor & update tampilan tiap SENSOR_INTERVAL
  unsigned long now = millis();
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    readAllSensors();
    updateStatusAndLED();
    showOLED();
    printSerial();
  }

  // Publish MQTT tiap MQTT_PUBLISH_INTERVAL
  if (now - lastMqttPublish >= MQTT_PUBLISH_INTERVAL) {
    lastMqttPublish = now;
    publishSensorData();
  }
}


// =====================================================
// WIFI MANAGER SETUP
// =====================================================

void setupWiFiManager() {

  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(180);   // portal aktif max 3 menit

  Serial.println("Menghubungkan ke WiFi tersimpan...");

  bool connected = wm.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);

  if (!connected) {
    Serial.println("Gagal konek WiFi & portal timeout. Restart...");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("WiFi gagal.");
    display.setCursor(0, 32);
    display.println("Restarting...");
    display.display();
    delay(2000);
    ESP.restart();
  }

  Serial.println("WiFi terhubung!");
  Serial.print("IP Address : ");
  Serial.println(WiFi.localIP());

  // -------------------------
  // mDNS (DNS LOKAL)
  // -------------------------
  // Setelah ini dashboard bisa diakses via http://letsens.local
  // dari perangkat lain yang satu jaringan WiFi, tanpa perlu tahu IP-nya.
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS aktif   : http://");
    Serial.print(MDNS_HOSTNAME);
    Serial.println(".local");
  } else {
    Serial.println("mDNS gagal diaktifkan (fallback: pakai IP).");
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Connected");
  display.setCursor(0, 16);
  display.print(MDNS_HOSTNAME);
  display.println(".local");
  display.setCursor(0, 32);
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

// Dipanggil WiFiManager saat masuk mode Access Point (config portal)
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Masuk mode konfigurasi WiFi (Access Point).");
  Serial.print("Hubungkan ke SSID: ");
  Serial.println(WIFI_AP_NAME);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WIFI SETUP MODE");
  display.setCursor(0, 16);
  display.println("Connect to AP:");
  display.setCursor(0, 28);
  display.println(WIFI_AP_NAME);
  display.setCursor(0, 40);
  display.print("Pass: ");
  display.println(WIFI_AP_PASSWORD);
  display.setCursor(0, 52);
  display.println("Open 192.168.4.1");
  display.display();
}


// =====================================================
// NTP / WAKTU
// =====================================================

void setupTime() {

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  Serial.print("Sinkronisasi waktu NTP");

  struct tm timeinfo;
  int retry = 0;

  while (!getLocalTime(&timeinfo) && retry < 10) {
    Serial.print(".");
    delay(500);
    retry++;
  }

  Serial.println();

  if (retry >= 10) {
    Serial.println("Gagal sinkronisasi NTP (timestamp mungkin belum akurat, akan retry di background).");
  } else {
    Serial.print("Waktu tersinkronisasi (WIB): ");
    Serial.println(getFormattedTime());
  }
}

// Format waktu lokal (WIB) yang mudah dibaca, contoh: "2026-09-04 09:15:23"
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01 00:00:00";
  }
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// Unix epoch time (detik sejak 1 Jan 1970 UTC) - format standar
// untuk disimpan di database/time-series (mis. PostgreSQL/TDEx)
unsigned long getEpochTime() {
  time_t now;
  time(&now);
  return (unsigned long)now;
}


// =====================================================
// MQTT SETUP & FUNCTIONS
// =====================================================

void setupMQTT() {
  // Development: skip validasi certificate chain.
  // Untuk produksi, ganti dengan wifiClientSecure.setCACert(rootCA)
  wifiClientSecure.setInsecure();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(512);   // JSON payload lebih besar dari default 256 byte
  mqttClient.setKeepAlive(30);

  connectMQTT();
}

void connectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.print("Menghubungkan ke MQTT broker HiveMQ... ");

  bool ok = mqttClient.connect(
    mqttClientId.c_str(),
    MQTT_USERNAME,
    MQTT_PASSWORD,
    MQTT_TOPIC_STATUS,   // LWT topic
    0,                    // LWT QoS
    true,                 // LWT retain
    "offline"             // LWT payload
  );

  if (ok) {
    Serial.println("BERHASIL");
    mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
    mqttLastError = "-";
  } else {
    Serial.print("GAGAL, rc=");
    Serial.println(mqttClient.state());
    mqttLastError = mqttStateToString(mqttClient.state());
  }
}

String mqttStateToString(int state) {
  switch (state) {
    case -4: return "Timeout koneksi ke broker";
    case -3: return "Koneksi terputus";
    case -2: return "Koneksi ke broker gagal";
    case -1: return "Client terputus (disconnected)";
    case  0: return "Terhubung";
    case  1: return "Versi protokol MQTT tidak didukung broker";
    case  2: return "Client ID ditolak broker";
    case  3: return "Server MQTT tidak tersedia";
    case  4: return "Username/password salah";
    case  5: return "Tidak diizinkan (unauthorized)";
    default: return "Unknown error";
  }
}

void publishSensorData() {

  StaticJsonDocument<512> doc;

  doc["device_id"]        = mqttClientId;
  doc["timestamp"]        = getEpochTime();       // Unix epoch (detik, UTC) - untuk database
  doc["time"]              = getFormattedTime();    // format mudah dibaca, WIB (UTC+7)
  doc["uptime_sec"]       = millis() / 1000;
  doc["wifi_rssi"]        = WiFi.RSSI();

  if (sensorData.dhtOk) {
    doc["temperature_c"]    = sensorData.temperature;
    doc["humidity_percent"] = sensorData.humidity;
  } else {
    doc["temperature_c"]    = (char*)0;   // null di JSON
    doc["humidity_percent"] = (char*)0;   // null di JSON
  }

  doc["gas_raw"]   = sensorData.gasRaw;
  doc["gas_index"] = round(sensorData.gasPPM * 100) / 100.0;   // nama field tetap "gas_index" (kompatibel web), isinya tetap estimasi NH3 dalam ppm

  doc["pir_presence"]     = sensorData.pirPresence;
  doc["pir_duration_sec"] = sensorData.pirDurationSec;

  if (sensorData.lightOk) {
    doc["light_lux"] = round(sensorData.lightLux * 10) / 10.0;
  } else {
    doc["light_lux"] = (char*)0;   // null di JSON
  }
  doc["light_ok"] = sensorData.lightOk;

  doc["status"]           = sensorData.status;

  char payload[512];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (!mqttClient.connected()) {
    mqttPublishSuccess = false;
    mqttLastError = "Tidak terhubung ke broker";
    Serial.println("MQTT: publish dibatalkan (tidak terhubung).");
    return;
  }

  bool ok = mqttClient.publish(MQTT_TOPIC_DATA, payload, len);

  mqttPublishSuccess = ok;
  mqttLastPublishMillis = millis();

  if (ok) {
    mqttLastError = "-";
    Serial.println("MQTT: publish BERHASIL.");
  } else {
    mqttLastError = "Publish gagal (cek buffer size / koneksi)";
    Serial.println("MQTT: publish GAGAL.");
  }
}


// =====================================================
// PEMBACAAN SEMUA SENSOR
// =====================================================

void readAllSensors() {
  readDHT();
  readMQ135();
  simulatePIR();
  readLight();
}

void readDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    sensorData.dhtOk = false;
    Serial.println("ERROR: DHT11 tidak terbaca!");
  } else {
    sensorData.dhtOk = true;
    sensorData.temperature = t;
    sensorData.humidity = h;
  }
}

// Menghitung resistansi sensor (Rs) MQ135 dari pembacaan ADC mentah.
// Sudah termasuk koreksi pembagi tegangan eksternal (R1=20k, R2=10k) di jalur AOUT->GPIO34.
float mq135CalculateRs(int adcRaw) {
  float vAdc = (adcRaw / ADC_RESOLUTION) * ADC_VREF;   // tegangan yang benar-benar masuk ke GPIO34
  float vSensorOut = vAdc * MQ135_DIVIDER_FACTOR;      // tegangan AOUT asli sensor (sebelum dibagi)

  if (vSensorOut < 0.001) vSensorOut = 0.001;          // hindari pembagian oleh nol

  float rs = ((MQ135_VCC - vSensorOut) / vSensorOut) * MQ135_RL_VALUE;  // rumus standar Rs sensor gas
  if (rs < 0) rs = 0;
  return rs;   // satuan kOhm (mengikuti satuan MQ135_RL_VALUE)
}

void readMQ135() {
  sensorData.gasRaw = analogRead(MQ135_PIN);

  sensorData.gasVoltage = (sensorData.gasRaw / ADC_RESOLUTION) * ADC_VREF * MQ135_DIVIDER_FACTOR;
  sensorData.gasRs = mq135CalculateRs(sensorData.gasRaw);

  if (mq135_Ro > 0) {
    sensorData.gasRatio = sensorData.gasRs / mq135_Ro;
    sensorData.gasPPM   = MQ135_NH3_CURVE_A * pow(sensorData.gasRatio, MQ135_NH3_CURVE_B);
  } else {
    sensorData.gasRatio = 0;
    sensorData.gasPPM   = 0;
  }
}

// -----------------------------------------------------
// DUMMY: Sensor PIR (kehadiran manusia + durasi)
// Gantilah isi fungsi ini dengan pembacaan digitalRead(PIR_PIN)
// saat sensor PIR asli sudah terpasang.
// -----------------------------------------------------
void simulatePIR() {

  if (!sensorData.pirPresence) {
    // 25% kemungkinan ada orang masuk toilet
    if (random(0, 100) < 25) {
      sensorData.pirPresence = true;
      pirSessionStart = millis();
      sensorData.pirDurationSec = 0;
    }
  } else {
    // sedang "ada orang" -> update durasi
    sensorData.pirDurationSec = (millis() - pirSessionStart) / 1000;

    // 15% kemungkinan orang keluar toilet tiap siklus baca
    if (random(0, 100) < 15) {
      sensorData.pirPresence = false;
      sensorData.pirDurationSec = 0;
    }
  }
}

// -----------------------------------------------------
// REAL: Sensor Intensitas Cahaya (lux) - BH1750FVI, via I2C
// -----------------------------------------------------
void readLight() {
  // Catatan: tidak semua versi library BH1750 punya measurementReady().
  // Karena mode CONTINUOUS_HIGH_RES_MODE sudah update otomatis tiap
  // ~120ms (jauh lebih cepat dari SENSOR_INTERVAL kita yang 2 detik),
  // aman untuk langsung baca nilai terbaru tiap siklus tanpa dicek dulu.
  float lux = lightMeter.readLightLevel();

  // readLightLevel() akan bernilai negatif kalau pembacaan gagal
  // (misal sensor terlepas / gangguan I2C di tengah operasi).
  if (lux < 0) {
    sensorData.lightOk = false;
    Serial.println("ERROR: BH1750 gagal dibaca!");
  } else {
    sensorData.lightOk = true;
    sensorData.lightLux = lux;
  }
}


// =====================================================
// STATUS + LED
// =====================================================

void updateStatusAndLED() {

  bool temperatureWarning = sensorData.dhtOk && (sensorData.temperature > TEMP_WARNING);
  bool gasWarning  = sensorData.gasPPM >= GAS_WARNING_PPM;
  bool gasCritical = sensorData.gasPPM >= GAS_CRITICAL_PPM;

  if (!sensorData.dhtOk) {
    sensorData.status = "ERROR";
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, HIGH);
    return;
  }

  if (gasCritical) {
    sensorData.status = "CRITICAL";
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    // LED merah dikendalikan oleh blinkRed() di loop()
  }
  else if (temperatureWarning || gasWarning) {
    sensorData.status = "WARNING";
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_RED, LOW);
    redState = false;
  }
  else {
    sensorData.status = "NORMAL";
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    redState = false;
  }
}

void blinkRed() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisBlink >= BLINK_INTERVAL) {
    previousMillisBlink = currentMillis;
    redState = !redState;
    digitalWrite(LED_RED, redState);
  }
}


// =====================================================
// OLED DISPLAY
// =====================================================

void showOLED() {

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Status besar
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(sensorData.status);

  display.setTextSize(1);

  if (!sensorData.dhtOk) {
    display.setCursor(0, 25);
    display.println("DHT11 tidak terbaca!");
  } else {
    display.setCursor(0, 20);
    display.print("Temp : ");
    display.print(sensorData.temperature, 1);
    display.println(" C");

    display.setCursor(0, 30);
    display.print("Hum  : ");
    display.print(sensorData.humidity, 1);
    display.println(" %");
  }

  display.setCursor(0, 40);
  display.print("NH3: ");
  display.print(sensorData.gasPPM, 1);

  display.setCursor(64, 40);
  display.print("Lux:");
  if (sensorData.lightOk) {
    display.print((int)sensorData.lightLux);
  } else {
    display.print("ERR");
  }

  display.setCursor(0, 50);
  display.print("PIR  : ");
  display.print(sensorData.pirPresence ? "ADA (" : "KOSONG");
  if (sensorData.pirPresence) {
    display.print(sensorData.pirDurationSec);
    display.print("s)");
  }

  // Indikator MQTT di pojok kanan bawah
  display.setCursor(100, 0);
  display.setTextSize(1);
  display.print(mqttClient.connected() ? "MQ:OK" : "MQ:X");

  display.display();
}


// =====================================================
// KALIBRASI MQ135
// =====================================================

void calibrateMQ135() {

  const int samples = 100;
  long total = 0;

  Serial.println("Mengambil baseline MQ135 (Ro) di udara bersih...");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MQ135 CALIBRATION");
  display.display();

  for (int i = 0; i < samples; i++) {
    total += analogRead(MQ135_PIN);
    // tetap layani server & mqtt selama kalibrasi
    server.handleClient();
    mqttClient.loop();
    delay(100);
  }

  int avgRaw = total / samples;
  float rsClean = mq135CalculateRs(avgRaw);
  mq135_Ro = rsClean / MQ135_RO_CLEAN_AIR_FACTOR;

  Serial.print("Rs udara bersih = ");
  Serial.print(rsClean, 2);
  Serial.println(" kOhm");
  Serial.print("Ro (baseline)   = ");
  Serial.print(mq135_Ro, 2);
  Serial.println(" kOhm");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MQ135 CALIBRATION");
  display.setCursor(0, 20);
  display.print("Ro: ");
  display.print(mq135_Ro, 2);
  display.println(" kOhm");
  display.setCursor(0, 45);
  display.println("Done!");
  display.display();

  delay(1500);
}


// =====================================================
// SERIAL DEBUG
// =====================================================

void printSerial() {
  Serial.println("--------------------------------");
  Serial.print("Status     : ");
  Serial.println(sensorData.status);

  if (sensorData.dhtOk) {
    Serial.print("Suhu       : ");
    Serial.print(sensorData.temperature, 1);
    Serial.println(" C");

    Serial.print("Kelembapan : ");
    Serial.print(sensorData.humidity, 1);
    Serial.println(" %");
  } else {
    Serial.println("Suhu/Kelembapan: ERROR");
  }

  Serial.print("MQ135 RAW      : ");
  Serial.println(sensorData.gasRaw);
  Serial.print("MQ135 Voltage  : ");
  Serial.print(sensorData.gasVoltage, 3);
  Serial.println(" V");
  Serial.print("MQ135 Rs/Ro    : ");
  Serial.println(sensorData.gasRatio, 3);
  Serial.print("Gas NH3        : ");
  Serial.print(sensorData.gasPPM, 2);
  Serial.println(" ppm");

  Serial.print("PIR        : ");
  Serial.print(sensorData.pirPresence ? "ADA ORANG" : "KOSONG");
  if (sensorData.pirPresence) {
    Serial.print(" (");
    Serial.print(sensorData.pirDurationSec);
    Serial.print(" detik)");
  }
  Serial.println();

  Serial.print("Lux        : ");
  if (sensorData.lightOk) {
    Serial.println(sensorData.lightLux, 1);
  } else {
    Serial.println("ERROR (BH1750 tidak terbaca)");
  }

  Serial.print("MQTT       : ");
  Serial.println(mqttClient.connected() ? "Terhubung" : "Terputus");

  Serial.print("Publish    : ");
  Serial.println(mqttPublishSuccess ? "BERHASIL" : "GAGAL/BELUM");

  Serial.println("--------------------------------");
  Serial.println();
}


// =====================================================
// WEB SERVER (DASHBOARD LOKAL)
// =====================================================

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/resetwifi", HTTP_GET, handleResetWifi);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
  Serial.println("Web server dashboard aktif di port 80.");
  Serial.print("Buka dashboard di: http://");
  Serial.print(MDNS_HOSTNAME);
  Serial.println(".local");
}

void handleData() {
  StaticJsonDocument<640> doc;

  doc["device_id"]  = mqttClientId;
  doc["time"]        = getFormattedTime();
  doc["hostname"]   = String(MDNS_HOSTNAME) + ".local";
  doc["ip"]         = WiFi.localIP().toString();
  doc["wifi_rssi"]  = WiFi.RSSI();
  doc["uptime_sec"] = millis() / 1000;

  doc["dht_ok"]         = sensorData.dhtOk;
  doc["temperature_c"]  = sensorData.temperature;
  doc["humidity_pct"]   = sensorData.humidity;

  doc["gas_raw"]   = sensorData.gasRaw;
  doc["gas_ro"]    = mq135_Ro;
  doc["gas_ratio"] = sensorData.gasRatio;
  doc["gas_ppm"]   = sensorData.gasPPM;

  doc["pir_presence"]     = sensorData.pirPresence;
  doc["pir_duration_sec"] = sensorData.pirDurationSec;

  doc["light_lux"] = sensorData.lightLux;
  doc["light_ok"]  = sensorData.lightOk;

  doc["status"] = sensorData.status;

  doc["mqtt_connected"]    = mqttClient.connected();
  doc["mqtt_last_publish"] = mqttPublishSuccess;
  doc["mqtt_last_error"]   = mqttLastError;
  doc["mqtt_last_publish_sec_ago"] =
    mqttLastPublishMillis == 0 ? -1 : (millis() - mqttLastPublishMillis) / 1000;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleResetWifi() {
  server.send(200, "text/plain",
    "Kredensial WiFi dihapus. Perangkat akan restart dan membuka portal konfigurasi WiFi (AP: LETSENS-Setup).");
  delay(500);
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

void handleRoot() {

  String html;
  html.reserve(4096);

  html += F(R"HTML(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LetSens - Toilet Monitoring</title>
<style>
  body { font-family: Arial, Helvetica, sans-serif; background:#0f172a; color:#e2e8f0; margin:0; padding:20px; }
  h1 { font-size: 1.3rem; margin-bottom:4px; }
  .sub { color:#94a3b8; margin-bottom:20px; font-size:0.85rem; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:14px; }
  .card { background:#1e293b; border-radius:10px; padding:16px; box-shadow:0 2px 6px rgba(0,0,0,0.3); }
  .card h2 { font-size:0.8rem; text-transform:uppercase; letter-spacing:1px; color:#94a3b8; margin:0 0 8px 0; }
  .val { font-size:1.6rem; font-weight:bold; }
  .unit { font-size:0.9rem; color:#94a3b8; margin-left:4px; }
  .badge { display:inline-block; padding:3px 10px; border-radius:20px; font-size:0.75rem; font-weight:bold; }
  .badge-ok { background:#166534; color:#bbf7d0; }
  .badge-bad { background:#7f1d1d; color:#fecaca; }
  .badge-warn { background:#78350f; color:#fde68a; }
  .status-NORMAL { color:#4ade80; }
  .status-WARNING { color:#facc15; }
  .status-CRITICAL { color:#f87171; }
  .status-ERROR { color:#f87171; }
  .footer { margin-top:20px; font-size:0.75rem; color:#64748b; }
  a.btn { display:inline-block; margin-top:16px; background:#334155; color:#e2e8f0; text-decoration:none; padding:8px 14px; border-radius:6px; font-size:0.85rem; }
  a.btn:hover { background:#475569; }
</style>
</head>
<body>
  <h1>LETSENS &mdash; AIoT Toilet Monitoring</h1>
  <div class="sub" id="deviceInfo">Memuat...</div>

  <div class="grid">
    <div class="card">
      <h2>Status Sistem</h2>
      <div class="val" id="statusVal">-</div>
    </div>

    <div class="card">
      <h2>Suhu</h2>
      <div class="val"><span id="tempVal">-</span><span class="unit">&deg;C</span></div>
    </div>

    <div class="card">
      <h2>Kelembapan</h2>
      <div class="val"><span id="humVal">-</span><span class="unit">%</span></div>
    </div>

    <div class="card">
      <h2>Gas Amonia / NH3 (MQ135)</h2>
      <div class="val"><span id="gasVal">-</span><span class="unit">ppm</span></div>
      <div class="sub">Rs/Ro: <span id="gasRatioVal">-</span> &middot; Raw ADC: <span id="gasRawVal">-</span></div>
    </div>

    <div class="card">
      <h2>Sensor PIR (dummy)</h2>
      <div class="val" id="pirVal">-</div>
      <div class="sub">Durasi di toilet: <span id="pirDurVal">-</span> detik</div>
    </div>

    <div class="card">
      <h2>Intensitas Cahaya (BH1750)</h2>
      <div class="val"><span id="luxVal">-</span><span class="unit">lux</span></div>
    </div>

    <div class="card">
      <h2>Koneksi WiFi</h2>
      <div class="val" style="font-size:1.1rem" id="hostnameVal">-</div>
      <div class="sub">IP: <span id="ipVal">-</span></div>
      <div class="sub">RSSI: <span id="rssiVal">-</span> dBm</div>
    </div>

    <div class="card">
      <h2>Status MQTT (HiveMQ)</h2>
      <div><span class="badge" id="mqttConnBadge">-</span></div>
      <div class="sub" style="margin-top:8px;">Publish terakhir:
        <span class="badge" id="mqttPubBadge">-</span>
      </div>
      <div class="sub" style="margin-top:6px;">Terakhir publish: <span id="mqttAgoVal">-</span> detik lalu</div>
      <div class="sub">Pesan: <span id="mqttErrVal">-</span></div>
    </div>
  </div>

  <a class="btn" href="/resetwifi" onclick="return confirm('Hapus kredensial WiFi & restart perangkat?');">Ganti Konfigurasi WiFi</a>

  <div class="footer">Auto-refresh tiap 3 detik &middot; LetSens Firmware v2.1</div>

<script>
async function refreshData() {
  try {
    const res = await fetch('/data');
    const d = await res.json();

    document.getElementById('deviceInfo').innerText =
      'Device: ' + d.device_id + ' | Waktu: ' + d.time + ' | Uptime: ' + d.uptime_sec + 's';

    const statusEl = document.getElementById('statusVal');
    statusEl.innerText = d.status;
    statusEl.className = 'val status-' + d.status;

    document.getElementById('tempVal').innerText = d.dht_ok ? d.temperature_c.toFixed(1) : 'ERR';
    document.getElementById('humVal').innerText = d.dht_ok ? d.humidity_pct.toFixed(1) : 'ERR';

    document.getElementById('gasVal').innerText = d.gas_ppm.toFixed(1);
    document.getElementById('gasRatioVal').innerText = d.gas_ratio.toFixed(2);
    document.getElementById('gasRawVal').innerText = d.gas_raw;

    document.getElementById('pirVal').innerText = d.pir_presence ? 'ADA ORANG' : 'KOSONG';
    document.getElementById('pirDurVal').innerText = d.pir_duration_sec;

    document.getElementById('luxVal').innerText = d.light_ok ? d.light_lux.toFixed(1) : 'ERR';

    document.getElementById('hostnameVal').innerText = d.hostname;
    document.getElementById('ipVal').innerText = d.ip;
    document.getElementById('rssiVal').innerText = d.wifi_rssi;

    const mqttConnBadge = document.getElementById('mqttConnBadge');
    mqttConnBadge.innerText = d.mqtt_connected ? 'TERHUBUNG' : 'TERPUTUS';
    mqttConnBadge.className = 'badge ' + (d.mqtt_connected ? 'badge-ok' : 'badge-bad');

    const mqttPubBadge = document.getElementById('mqttPubBadge');
    mqttPubBadge.innerText = d.mqtt_last_publish ? 'BERHASIL' : 'GAGAL';
    mqttPubBadge.className = 'badge ' + (d.mqtt_last_publish ? 'badge-ok' : 'badge-bad');

    document.getElementById('mqttAgoVal').innerText =
      d.mqtt_last_publish_sec_ago < 0 ? '-' : d.mqtt_last_publish_sec_ago;
    document.getElementById('mqttErrVal').innerText = d.mqtt_last_error;

  } catch (e) {
    console.error('Gagal ambil data:', e);
  }
}
refreshData();
setInterval(refreshData, 3000);
</script>
</body>
</html>
)HTML");

  server.send(200, "text/html", html);
}

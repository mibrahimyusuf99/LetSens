/*
  =====================================================================
  LETSENS - AIoT TOILET MONITORING SYSTEM
  Firmware v2.3 (ESP32)
  =====================================================================

  Fitur yang ditambahkan pada versi ini:
  - WiFiManager: konfigurasi WiFi via portal (auto-AP jika belum ada
    kredensial / gagal konek). AP name: "LETSENS-Setup"
  - Koneksi ke MQTT Broker (lihat perubahan v2.3 di bawah untuk detail terkini)
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

  Perubahan di v2.2:
  - Ditambahkan Task Watchdog Timer (TWDT), timeout 30 detik. Kalau
    loop() macet/hang (misal sensor nge-hang, bug, dsb), ESP32 akan
    restart otomatis sendiri tanpa perlu campur tangan manual.
  - Watchdog baru diaktifkan SETELAH WiFiManager selesai konek, supaya
    portal konfigurasi WiFi (yang bisa aktif sampai 180 detik) tidak
    ikut ke-restart paksa oleh watchdog.
  - Loop warm-up MQ135 (60 detik) & loop kalibrasi turut diberi umpan
    watchdog secara berkala supaya tidak trigger restart palsu.
  - publishSensorData() sekarang retry maksimal 3x kalau publish gagal
    (jeda 300ms antar percobaan). PENTING: ini retry berbasis status
    kirim TCP dari PubSubClient, BUKAN QoS 1 asli dengan konfirmasi
    PUBACK dari broker (PubSubClient tidak mendukung itu untuk publish).

  Perubahan di v2.3 (menyesuaikan dashboard letsens.web.id):
  - Broker MQTT diganti dari HiveMQ Cloud (TLS, port 8883) ke
    broker.emqx.io (broker publik, plain, port 1883, anonymous -
    tanpa username/password). Topik tetap sama: letsens/toilet/sensordata
  - Payload MQTT dirombak total supaya SAMA PERSIS dengan format yang
    diminta dashboard: kode_perangkat, amonia, suhu, rh, PIR, cahaya,
    RSSI, Baterai. Field lama (device_id, temperature_c, gas_index,
    dst) SUDAH TIDAK dikirim lagi ke broker - hanya dipakai di
    dashboard lokal ESP32 (/data) untuk keperluan debugging saja.
  - Kode perangkat (DEVICE_ID) diganti jadi "NODE-T-A2-G" sesuai yang
    terdaftar di menu Perangkat dashboard.
  - Ditambahkan field "Baterai" (dummy, random 80-90%) karena belum
    ada sensor tegangan baterai asli terpasang.
  - RSSI WiFi (real, dari WiFi.RSSI()) sekarang ikut dikirim ke
    broker dengan nama field "RSSI" sesuai format dashboard.

  LIBRARY YANG DIPERLUKAN (install via Library Manager):
  - WiFiManager          by tzapu
  - PubSubClient         by Nick O'Leary
  - ArduinoJson          by Benoit Blanchon (v6/v7)
  - Adafruit GFX Library
  - Adafruit SSD1306
  - DHT sensor library   by Adafruit  (+ Adafruit Unified Sensor)
  - BH1750               by Christopher Laws (search "BH1750" di Library Manager)
  - WiFiClient & WebServer sudah termasuk di ESP32 core

  CATATAN KEAMANAN (v2.3):
  - broker.emqx.io adalah broker MQTT PUBLIK tanpa TLS & tanpa autentikasi
    (anonymous). Cocok untuk testing/development, TAPI siapapun di
    internet bisa subscribe ke topik "letsens/toilet/sensordata" dan
    melihat data ini, atau bahkan publish data palsu ke topik yang sama.
    Untuk deployment produksi jangka panjang, sebaiknya pindah ke
    broker privat (self-hosted / cloud) dengan TLS + autentikasi.

  Perubahan di v2.4:
  - Dashboard web didesain ulang (tampilan lebih modern & rapi: ikon per
    kartu, warna aksen, indikator sinyal WiFi berbentuk bar, progress bar
    baterai, pewarnaan nilai suhu/gas otomatis sesuai ambang batas),
    TANPA mengubah skema data JSON di endpoint /data (tetap sama persis).
  - Ditambahkan fitur "Serial Monitor" ringan di dashboard: log yang
    biasa muncul di Serial Monitor Arduino IDE sekarang juga bisa
    dilihat lewat browser (endpoint baru /log & /log/clear).
    Implementasi memakai ring buffer kecil (~2.2KB RAM) yang menyalin
    karakter dari setiap Serial.print/println yang SUDAH ADA di kode
    ini (lewat macro, tanpa mengubah baris Serial.print manapun).
    Polling dari browser HANYA berjalan selagi panel dibuka, supaya
    tidak menambah beban kerja ke ESP32.

  PIC   : Ibrahim
  Proyek: LETSENS - AIoT TOILET MONITORING SYSTEM
  =====================================================================
*/

#include <HardwareSerial.h>       // dibutuhkan untuk logger Serial Monitor ringan di bawah
#include <WiFi.h>
#include <WiFiManager.h>          // tzapu/WiFiManager
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>              // mDNS - akses dashboard via hostname .local (sudah termasuk di ESP32 core)
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <BH1750.h>               // BH1750FVI - sensor intensitas cahaya (I2C)
#include <esp_task_wdt.h>         // Task Watchdog Timer (WDT) - auto-restart kalau program hang
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

// Kode perangkat/Node ID hardware ESP32 - HARUS SAMA PERSIS dengan
// yang didaftarkan di dashboard web (menu Perangkat), karena dashboard
// mencocokkan data masuk berdasarkan field "kode_perangkat" ini.
// Kalau nanti ada node lain, ganti sesuai kode perangkat yang terdaftar
// (contoh: NODE-T-A1-F, NODE-T-A2-G, dst - jangan sampai sama/dobel).
const char* DEVICE_ID = "NODE-T-A2-G";


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
// KONFIGURASI MQTT (broker.emqx.io - broker publik, tanpa TLS)
// =====================================================
// CATATAN: broker.emqx.io adalah broker publik untuk testing/development,
// siapapun bisa terhubung & publish/subscribe ke topik yang sama.
// Untuk produksi jangka panjang, sebaiknya pindah ke broker privat
// (self-hosted / cloud) supaya data tidak bercampur dengan pengguna lain.

const char* MQTT_HOST = "broker.emqx.io";
const int   MQTT_PORT = 1883;                  // plain (tanpa TLS)

const char* MQTT_TOPIC_DATA   = "letsens/toilet/sensordata";
const char* MQTT_TOPIC_STATUS = "letsens/toilet/status";      // LWT (online/offline)

String mqttClientId;   // diisi dari DEVICE_ID saat setup()

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

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

// -----------------------------------------------------
// WATCHDOG (TWDT)
// -----------------------------------------------------
// Kalau loop() macet/hang lebih lama dari ini tanpa sempat
// "memberi makan" watchdog, ESP32 akan restart otomatis sendiri.
const uint32_t WDT_TIMEOUT_SEC = 30;

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

  // Baterai (dummy - device ini disuplai dari step-down/adaptor,
  // belum ada sensor tegangan baterai asli. Nilai dibuat random
  // di rentang 80-90% supaya tampilan dashboard tetap masuk akal
  // tanpa terkesan baterai device lemah/kritis).
  int batteryPercent = 85;

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
// SERIAL LOGGER RINGAN (untuk fitur "Serial Monitor" di dashboard)
// =====================================================
// Tujuan: dashboard web bisa menampilkan log yang sama seperti yang
// muncul di Serial Monitor Arduino IDE, TANPA menambah beban kerja
// berarti ke ESP32 dan TANPA mengubah satupun baris Serial.print()
// yang sudah ada di kode ini.
//
// Caranya: semua Serial.print/println yang sudah ada tetap dipanggil
// apa adanya, tapi lewat "gLogger" (lihat #define Serial di bawah).
// gLogger meneruskan setiap karakter ke port serial fisik seperti
// biasa (RealSerial), SEKALIGUS menyalinnya ke buffer RAM kecil
// (ring buffer, ~2KB). Menyalin karakter ke array itu kerjanya
// sangat ringan (tidak ada delay/blocking), jadi tidak menambah
// beban loop(). Dashboard-lah yang mengambil isi buffer ini secara
// berkala lewat endpoint /log - device tidak melakukan apapun kalau
// tidak ada yang minta.
HardwareSerial* const RealSerial = &Serial;   // simpan referensi Serial ASLI, sebelum macro di bawah aktif

class DebugLogger : public Print {
  public:
    void begin(unsigned long baud) {
      RealSerial->begin(baud);
    }

    size_t write(uint8_t c) override {
      RealSerial->write(c);
      pushChar((char)c);
      return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
      RealSerial->write(buffer, size);
      for (size_t i = 0; i < size; i++) pushChar((char)buffer[i]);
      return size;
    }

    // Ambil isi log urut dari yang paling lama -> paling baru.
    String getLogText() {
      String out;
      out.reserve(bufSize + 8);
      if (!full) {
        for (int i = 0; i < head; i++) out += buf[i];
      } else {
        for (int i = head; i < bufSize; i++) out += buf[i];
        for (int i = 0; i < head; i++) out += buf[i];
      }
      return out;
    }

    void clearLog() {
      head = 0;
      full = false;
    }

  private:
    static const int bufSize = 2200;   // ~2.2KB RAM - cukup untuk beberapa puluh baris log terakhir
    char buf[bufSize];
    int head = 0;
    bool full = false;

    void pushChar(char c) {
      buf[head] = c;
      head++;
      if (head >= bufSize) {
        head = 0;
        full = true;
      }
    }
};

DebugLogger gLogger;

// Mulai dari titik ini, SEMUA "Serial.xxx" di file ini otomatis
// lewat gLogger (tetap tampil normal di Serial Monitor Arduino IDE,
// DAN tersimpan di buffer untuk ditampilkan di dashboard web).
#define Serial gLogger


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("================================");
  Serial.println("      ESP32 TOILET MONITOR v2.4");
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
  // WATCHDOG (TWDT)
  // -------------------------
  // Dinyalakan SETELAH WiFiManager, supaya portal konfigurasi WiFi
  // (yang bisa berjalan sampai 180 detik kalau user belum input WiFi)
  // tidak ikut ke-restart paksa oleh watchdog.
  setupWatchdog();

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
    esp_task_wdt_reset();   // beri makan watchdog, loop ini 60 detik jadi wajib di-reset berkala
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

  // Beri makan watchdog tiap iterasi - selama loop() ini masih jalan
  // normal (tidak macet/hang), ESP32 tidak akan di-restart paksa.
  esp_task_wdt_reset();

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
// WATCHDOG (TASK WATCHDOG TIMER)
// =====================================================

void setupWatchdog() {
  // API esp_task_wdt untuk Arduino-ESP32 core versi 3.x (berbasis ESP-IDF 5.x).
  // Kalau kamu pakai core versi lama (2.x), signature-nya beda:
  //   esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  //   esp_task_wdt_add(NULL);
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,     // tidak watch core idle task, cukup task loop() kita
    .trigger_panic = true,   // true = benar-benar restart ESP32 kalau timeout
  };

  esp_task_wdt_init(&wdtConfig);
  esp_task_wdt_add(NULL);    // daftarkan task saat ini (loop() jalan di sini) ke watchdog

  Serial.print("Watchdog aktif, timeout: ");
  Serial.print(WDT_TIMEOUT_SEC);
  Serial.println(" detik.");
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
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(512);   // JSON payload lebih besar dari default 256 byte
  mqttClient.setKeepAlive(30);

  connectMQTT();
}

void connectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.print("Menghubungkan ke MQTT broker (broker.emqx.io)... ");

  // broker.emqx.io broker publik, tidak butuh username/password (anonymous).
  bool ok = mqttClient.connect(
    mqttClientId.c_str(),
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

  // -----------------------------------------------------------------
  // PAYLOAD DISESUAIKAN PERSIS DENGAN FORMAT YANG DIMINTA DASHBOARD
  // (letsens.web.id -> menu Pengaturan/Perangkat, contoh payload JSON).
  // Field & nama key JANGAN diubah sembarangan, karena dashboard
  // mem-parsing berdasarkan nama key ini persis:
  //   kode_perangkat, amonia, suhu, rh, PIR, cahaya, RSSI, Baterai
  // -----------------------------------------------------------------
  StaticJsonDocument<512> doc;

  doc["kode_perangkat"] = mqttClientId;   // contoh: "NODE-T-A2-G"

  doc["amonia"] = round(sensorData.gasPPM * 100) / 100.0;   // ppm (MQ135)

  if (sensorData.dhtOk) {
    doc["suhu"] = round(sensorData.temperature * 10) / 10.0;
    doc["rh"]   = round(sensorData.humidity * 10) / 10.0;
  } else {
    doc["suhu"] = (char*)0;   // null di JSON kalau DHT gagal baca
    doc["rh"]   = (char*)0;
  }

  doc["PIR"] = sensorData.pirPresence;

  if (sensorData.lightOk) {
    doc["cahaya"] = round(sensorData.lightLux * 10) / 10.0;
  } else {
    doc["cahaya"] = (char*)0;   // null di JSON kalau BH1750 gagal baca
  }

  doc["RSSI"] = WiFi.RSSI();               // dBm, real dari WiFi ESP32

  doc["Baterai"] = sensorData.batteryPercent;   // % (masih dummy, 80-90%)

  char payload[512];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (!mqttClient.connected()) {
    mqttPublishSuccess = false;
    mqttLastError = "Tidak terhubung ke broker";
    Serial.println("MQTT: publish dibatalkan (tidak terhubung).");
    return;
  }

  // -----------------------------------------------------------------
  // RETRY PUBLISH (maksimal 3x)
  // -----------------------------------------------------------------
  // CATATAN JUJUR: PubSubClient tidak mendukung QoS 1 asli untuk publish.
  // mqttClient.publish() cuma memberi tahu apakah data berhasil DITULIS
  // ke socket TCP saat itu juga - bukan konfirmasi PUBACK dari broker.
  // Jadi retry di bawah ini adalah "best effort" berdasarkan status kirim
  // TCP, BUKAN jaminan broker benar-benar menerima pesannya. Kalau nanti
  // butuh QoS 1 asli (tunggu PUBACK betulan), library ini perlu diganti
  // (misal ke espMqttClient / arduino-mqtt yang punya callback onPublish).
  const int MQTT_PUBLISH_MAX_RETRY = 3;
  bool ok = false;

  for (int attempt = 1; attempt <= MQTT_PUBLISH_MAX_RETRY; attempt++) {

    if (!mqttClient.connected()) {
      mqttLastError = "Koneksi terputus saat retry publish";
      ok = false;
      break;
    }

    ok = mqttClient.publish(MQTT_TOPIC_DATA, payload, len);

    if (ok) {
      break;   // berhasil ditulis ke socket, tidak perlu retry lagi
    }

    Serial.print("MQTT: publish percobaan ke-");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(MQTT_PUBLISH_MAX_RETRY);
    Serial.println(" gagal.");

    if (attempt < MQTT_PUBLISH_MAX_RETRY) {
      // Jeda singkat sebelum retry, sambil tetap kasih makan watchdog
      // & layani web server / mqtt loop supaya tidak macet selama nunggu.
      unsigned long retryWaitStart = millis();
      while (millis() - retryWaitStart < 300) {
        esp_task_wdt_reset();
        mqttClient.loop();
        server.handleClient();
        delay(10);
      }
    }
  }

  mqttPublishSuccess = ok;
  mqttLastPublishMillis = millis();

  if (ok) {
    mqttLastError = "-";
    Serial.println("MQTT: publish BERHASIL.");
  } else {
    if (mqttLastError != "Koneksi terputus saat retry publish") {
      mqttLastError = "Publish gagal setelah " + String(MQTT_PUBLISH_MAX_RETRY) + "x percobaan (cek buffer size / koneksi)";
    }
    Serial.println("MQTT: publish GAGAL setelah retry.");
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
  readBattery();
}

// -----------------------------------------------------
// DUMMY: Kapasitas baterai
// Belum ada sensor tegangan baterai asli terpasang di device ini.
// Nilai dibuat random di rentang 80-90% tiap siklus baca sensor.
// Gantilah isi fungsi ini dengan pembacaan tegangan baterai asli
// (lewat pembagi tegangan ke pin ADC) kalau sudah pakai baterai
// dengan sensor pemantau tegangan.
// -----------------------------------------------------
void readBattery() {
  sensorData.batteryPercent = random(80, 91);   // 80-90% (91 exclusive di random())
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
    esp_task_wdt_reset();   // jaga-jaga, walau total durasi loop ini masih di bawah timeout watchdog
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
  server.on("/log", HTTP_GET, handleLog);
  server.on("/log/clear", HTTP_GET, handleLogClear);
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

  doc["battery_percent"] = sensorData.batteryPercent;

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

void handleLog() {
  // text/plain sengaja dipilih (bukan JSON) supaya ESP32 tidak perlu
  // escape karakter / build objek JSON - lebih ringan untuk isi log
  // yang bisa cukup panjang.
  server.send(200, "text/plain; charset=utf-8", gLogger.getLogText());
}

void handleLogClear() {
  gLogger.clearLog();
  server.send(200, "text/plain", "OK");
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
  :root{
    --bg:#0b1220; --panel:#141c2e; --panel-2:#182338; --border:#233047;
    --text:#e9eefb; --muted:#8b96b3; --muted-2:#647089;
    --green:#22c55e; --green-bg:rgba(34,197,94,.14);
    --yellow:#facc15; --yellow-bg:rgba(250,204,21,.14);
    --red:#f87171; --red-bg:rgba(248,113,113,.14);
    --blue:#38bdf8; --accent:#818cf8;
  }
  *{box-sizing:border-box;}
  body{
    font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
    background:
      radial-gradient(900px 400px at 10% -10%, rgba(56,189,248,.08), transparent),
      radial-gradient(700px 400px at 100% 0%, rgba(129,140,248,.08), transparent),
      var(--bg);
    color:var(--text); margin:0; padding:22px 20px 40px; min-height:100vh;
  }
  .topbar{ display:flex; align-items:flex-start; justify-content:space-between; flex-wrap:wrap; gap:10px; margin-bottom:22px; }
  .topbar h1{ font-size:1.35rem; margin:0 0 6px 0; display:flex; align-items:center; gap:8px; letter-spacing:.2px; }
  .topbar h1 .logo{ font-size:1.2rem; }
  .meta{ color:var(--muted); font-size:.82rem; display:flex; flex-wrap:wrap; gap:4px 10px; align-items:center; }
  .meta .dot-sep{ color:var(--muted-2); }
  .live{ display:inline-flex; align-items:center; gap:6px; background:var(--panel); border:1px solid var(--border); padding:6px 12px; border-radius:20px; font-size:.78rem; color:var(--muted); }
  .live .pulse{ width:8px; height:8px; border-radius:50%; background:var(--green); box-shadow:0 0 0 0 rgba(34,197,94,.6); animation:pulse 1.8s infinite; }
  @keyframes pulse{ 0%{box-shadow:0 0 0 0 rgba(34,197,94,.55);} 70%{box-shadow:0 0 0 7px rgba(34,197,94,0);} 100%{box-shadow:0 0 0 0 rgba(34,197,94,0);} }

  .grid{ display:grid; grid-template-columns:repeat(auto-fit,minmax(215px,1fr)); gap:14px; }
  .card{
    background:linear-gradient(180deg,var(--panel),rgba(20,28,46,.6));
    border:1px solid var(--border); border-radius:14px; padding:16px 16px 15px;
    position:relative; overflow:hidden; transition:transform .15s ease, border-color .15s ease;
  }
  .card:hover{ transform:translateY(-2px); border-color:#324066; }
  .card::before{ content:""; position:absolute; left:0; top:0; bottom:0; width:3px; background:var(--card-accent,var(--accent)); }
  .card h2{ font-size:.72rem; text-transform:uppercase; letter-spacing:1px; color:var(--muted); margin:0 0 10px 0; display:flex; flex-wrap:wrap; align-items:center; row-gap:2px; gap:7px; font-weight:600; }
  .card h2 .sub-label{ text-transform:none; letter-spacing:0; opacity:.65; font-weight:400; font-size:.68rem; }
  .card h2 .ic{ font-size:.95rem; }
  .val{ font-size:1.75rem; font-weight:700; line-height:1.1; }
  .unit{ font-size:.85rem; color:var(--muted); margin-left:4px; font-weight:500; }
  .sub{ color:var(--muted); margin-top:8px; font-size:.78rem; line-height:1.5; }
  .sub b{ color:#c3cbe0; font-weight:600; }

  .good{ color:var(--green); } .warn{ color:var(--yellow); } .bad{ color:var(--red); }
  .status-NORMAL{ color:var(--green); } .status-WARNING{ color:var(--yellow); }
  .status-CRITICAL{ color:var(--red); } .status-ERROR{ color:var(--red); }
  .card.c-status{ --card-accent:var(--green); }

  .badge{ display:inline-flex; align-items:center; gap:5px; padding:4px 11px; border-radius:20px; font-size:.72rem; font-weight:700; letter-spacing:.3px; }
  .badge::before{ content:""; width:6px; height:6px; border-radius:50%; background:currentColor; }
  .badge-ok{ background:var(--green-bg); color:var(--green); }
  .badge-bad{ background:var(--red-bg); color:var(--red); }
  .badge-warn{ background:var(--yellow-bg); color:var(--yellow); }

  .bars{ display:flex; align-items:flex-end; gap:3px; height:16px; margin-right:8px; }
  .bars i{ width:5px; background:var(--muted-2); border-radius:1px; display:inline-block; }
  .bars i:nth-child(1){ height:35%; } .bars i:nth-child(2){ height:58%; }
  .bars i:nth-child(3){ height:78%; } .bars i:nth-child(4){ height:100%; }
  .bars.lv1 i:nth-child(1){ background:var(--yellow); }
  .bars.lv2 i:nth-child(1),.bars.lv2 i:nth-child(2){ background:var(--yellow); }
  .bars.lv3 i:nth-child(1),.bars.lv3 i:nth-child(2),.bars.lv3 i:nth-child(3){ background:var(--green); }
  .bars.lv4 i{ background:var(--green); }
  .row-inline{ display:flex; align-items:center; }

  .meterbar{ height:6px; border-radius:4px; background:#20293f; overflow:hidden; margin-top:9px; }
  .meterbar > span{ display:block; height:100%; background:var(--green); border-radius:4px; transition:width .4s ease; }

  .footer{ margin-top:22px; font-size:.75rem; color:var(--muted-2); text-align:center; }
  a.btn, button.btn{
    display:inline-flex; align-items:center; gap:6px; margin-top:18px; background:var(--panel); border:1px solid var(--border);
    color:var(--text); text-decoration:none; padding:9px 15px; border-radius:9px; font-size:.82rem; cursor:pointer; font-family:inherit;
  }
  a.btn:hover, button.btn:hover{ background:#1c2740; border-color:#37456b; }

  .logcard{ grid-column:1/-1; }
  .logcard .lc-head{ display:flex; align-items:center; justify-content:space-between; flex-wrap:wrap; gap:8px; margin-bottom:0; }
  .logcard .lc-actions{ display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
  .logcard .lc-actions button{ margin-top:0; padding:6px 12px; font-size:.75rem; }
  .logcard label.chk{ font-size:.75rem; color:var(--muted); display:flex; align-items:center; gap:5px; cursor:pointer; user-select:none; }
  #logBox{
    display:none; margin-top:12px; background:#080d18; border:1px solid var(--border); border-radius:10px;
    padding:10px 12px; height:230px; overflow-y:auto; font-family:"SFMono-Regular",Consolas,"Liberation Mono",Menlo,monospace;
    font-size:.76rem; line-height:1.55; color:#a9e6c4; white-space:pre-wrap; word-break:break-word;
  }
  #logBox.open{ display:block; }
  .lc-info{ font-size:.72rem; color:var(--muted-2); margin-top:8px; }
</style>
</head>
<body>

  <div class="topbar">
    <div>
      <h1><span class="logo">&#128703;</span> LETSENS &mdash; AIoT Toilet Monitoring</h1>
      <div class="meta">
        <span id="deviceInfo">Memuat...</span>
      </div>
    </div>
    <div class="live"><span class="pulse"></span> LIVE</div>
  </div>

  <div class="grid">
    <div class="card c-status">
      <h2><span class="ic">&#9889;</span> Status Sistem</h2>
      <div class="val" id="statusVal">-</div>
    </div>

    <div class="card" style="--card-accent:#fb923c">
      <h2><span class="ic">&#127777;&#65039;</span> Suhu</h2>
      <div class="val"><span id="tempVal">-</span><span class="unit">&deg;C</span></div>
    </div>

    <div class="card" style="--card-accent:#38bdf8">
      <h2><span class="ic">&#128167;</span> Kelembapan</h2>
      <div class="val"><span id="humVal">-</span><span class="unit">%</span></div>
    </div>

    <div class="card" style="--card-accent:#4ade80">
      <h2><span class="ic">&#129514;</span> Gas Amonia / NH3</h2>
      <div class="val"><span id="gasVal">-</span><span class="unit">ppm</span></div>
      <div class="sub">Rs/Ro: <b id="gasRatioVal">-</b> &middot; Raw ADC: <b id="gasRawVal">-</b></div>
    </div>

    <div class="card" style="--card-accent:#f472b6">
      <h2><span class="ic">&#128694;</span> Sensor PIR <span class="sub-label">(dummy)</span></h2>
      <div class="val" id="pirVal">-</div>
      <div class="sub">Durasi di toilet: <b id="pirDurVal">-</b> detik</div>
    </div>

    <div class="card" style="--card-accent:#facc15">
      <h2><span class="ic">&#128161;</span> Intensitas Cahaya</h2>
      <div class="val"><span id="luxVal">-</span><span class="unit">lux</span></div>
    </div>

    <div class="card" style="--card-accent:#2dd4bf">
      <h2><span class="ic">&#128267;</span> Baterai <span class="sub-label">(dummy)</span></h2>
      <div class="val"><span id="battVal">-</span><span class="unit">%</span></div>
      <div class="meterbar"><span id="battBar" style="width:0%"></span></div>
    </div>

    <div class="card" style="--card-accent:#60a5fa">
      <h2><span class="ic">&#128225;</span> Koneksi WiFi</h2>
      <div class="row-inline">
        <div class="bars" id="wifiBars"><i></i><i></i><i></i><i></i></div>
        <div class="val" style="font-size:1.1rem" id="hostnameVal">-</div>
      </div>
      <div class="sub">IP: <b id="ipVal">-</b> &middot; RSSI: <b id="rssiVal">-</b> dBm</div>
    </div>

    <div class="card" style="--card-accent:#818cf8">
      <h2><span class="ic">&#128225;</span> Status MQTT <span class="sub-label">(broker.emqx.io)</span></h2>
      <div><span class="badge" id="mqttConnBadge">-</span></div>
      <div class="sub" style="margin-top:9px;">Publish terakhir: <span class="badge" id="mqttPubBadge">-</span></div>
      <div class="sub">Terakhir publish: <b id="mqttAgoVal">-</b> detik lalu &middot; Pesan: <span id="mqttErrVal">-</span></div>
    </div>

    <div class="card logcard" style="--card-accent:#94a3b8">
      <div class="lc-head">
        <h2><span class="ic">&#128187;</span> Serial Monitor</h2>
        <div class="lc-actions">
          <label class="chk"><input type="checkbox" id="autoScrollChk" checked> Auto-scroll</label>
          <button class="btn" id="pauseLogBtn" onclick="togglePauseLog()">Jeda</button>
          <button class="btn" onclick="clearLog()">Bersihkan</button>
          <button class="btn" onclick="toggleLog()" id="toggleLogBtn">Buka</button>
        </div>
      </div>
      <div id="logBox"></div>
      <div class="lc-info" id="logInfo">Serial monitor tertutup. Buka untuk melihat log terbaru dari ESP32 (polling ringan, hanya aktif saat panel terbuka).</div>
    </div>
  </div>

  <a class="btn" href="/resetwifi" onclick="return confirm('Hapus kredensial WiFi & restart perangkat?');">&#128268; Ganti Konfigurasi WiFi</a>

  <div class="footer">Auto-refresh tiap 3 detik &middot; LetSens Firmware v2.4</div>

<script>
function fmtUptime(sec){
  sec = Math.floor(sec);
  const h = Math.floor(sec/3600), m = Math.floor((sec%3600)/60), s = sec%60;
  if(h>0) return h+'j '+m+'m';
  if(m>0) return m+'m '+s+'d';
  return s+'d';
}

async function refreshData() {
  try {
    const res = await fetch('/data');
    const d = await res.json();

    document.getElementById('deviceInfo').innerText =
      'Device: ' + d.device_id + '  |  ' + d.time + '  |  Uptime ' + fmtUptime(d.uptime_sec);

    const statusEl = document.getElementById('statusVal');
    statusEl.innerText = d.status;
    statusEl.className = 'val status-' + d.status;

    const tempEl = document.getElementById('tempVal');
    tempEl.innerText = d.dht_ok ? d.temperature_c.toFixed(1) : 'ERR';
    tempEl.className = d.dht_ok ? (d.temperature_c >= 33 ? 'warn' : 'good') : 'bad';

    document.getElementById('humVal').innerText = d.dht_ok ? d.humidity_pct.toFixed(1) : 'ERR';

    const gasEl = document.getElementById('gasVal');
    gasEl.innerText = d.gas_ppm.toFixed(1);
    gasEl.className = d.gas_ppm >= 25 ? 'bad' : (d.gas_ppm >= 5 ? 'warn' : 'good');
    document.getElementById('gasRatioVal').innerText = d.gas_ratio.toFixed(2);
    document.getElementById('gasRawVal').innerText = d.gas_raw;

    document.getElementById('pirVal').innerText = d.pir_presence ? 'ADA ORANG' : 'KOSONG';
    document.getElementById('pirDurVal').innerText = d.pir_duration_sec;

    document.getElementById('luxVal').innerText = d.light_ok ? d.light_lux.toFixed(1) : 'ERR';

    document.getElementById('battVal').innerText = d.battery_percent;
    document.getElementById('battBar').style.width = d.battery_percent + '%';

    document.getElementById('hostnameVal').innerText = d.hostname;
    document.getElementById('ipVal').innerText = d.ip;
    document.getElementById('rssiVal').innerText = d.wifi_rssi;

    const bars = document.getElementById('wifiBars');
    let lv = 1;
    if (d.wifi_rssi >= -60) lv = 4;
    else if (d.wifi_rssi >= -70) lv = 3;
    else if (d.wifi_rssi >= -80) lv = 2;
    bars.className = 'bars lv' + lv;

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

// ------------------------------------------------------
// SERIAL MONITOR (sederhana & ringan)
// Polling /log HANYA berjalan selagi panel dibuka & tidak dijeda,
// supaya tidak menambah beban ke ESP32 saat panel ditutup.
// ------------------------------------------------------
let logOpen = false, logPaused = false, logTimer = null;

async function fetchLog(){
  try {
    const res = await fetch('/log');
    const text = await res.text();
    const box = document.getElementById('logBox');
    box.textContent = text.length ? text : '(log masih kosong)';
    const lines = (text.match(/\n/g) || []).length;
    document.getElementById('logInfo').innerText =
      lines + ' baris tersimpan di buffer \u00b7 diperbarui tiap 2 detik';
    if (document.getElementById('autoScrollChk').checked) box.scrollTop = box.scrollHeight;
  } catch(e) {
    document.getElementById('logInfo').innerText = 'Gagal mengambil log.';
  }
}

function toggleLog(){
  logOpen = !logOpen;
  const box = document.getElementById('logBox');
  const btn = document.getElementById('toggleLogBtn');
  if (logOpen){
    box.classList.add('open');
    btn.innerText = 'Tutup';
    fetchLog();
    logTimer = setInterval(() => { if(!logPaused) fetchLog(); }, 2000);
  } else {
    box.classList.remove('open');
    btn.innerText = 'Buka';
    clearInterval(logTimer);
    document.getElementById('logInfo').innerText = 'Serial monitor tertutup. Buka untuk melihat log terbaru dari ESP32 (polling ringan, hanya aktif saat panel terbuka).';
  }
}

function togglePauseLog(){
  logPaused = !logPaused;
  document.getElementById('pauseLogBtn').innerText = logPaused ? 'Lanjut' : 'Jeda';
}

async function clearLog(){
  await fetch('/log/clear');
  fetchLog();
}
</script>
</body>
</html>
)HTML");

  server.send(200, "text/html", html);
}

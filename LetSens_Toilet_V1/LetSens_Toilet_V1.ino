/*
 * =====================================================================
 *   LetSens Toilet - Toilet Environment Monitoring System
 * =====================================================================
 *
 *   Produk      : LetSens Toilet
 *   Versi       : 1.0
 *   Firmware    : v1.0
 *   Dibuat oleh : M. Ibrahim Yusuf
 *
 * -----------------------------------------------------------------------
 *   DESKRIPSI SENSOR & KOMPONEN
 * -----------------------------------------------------------------------
 *
 *   1. DHT11 (Pin GPIO4)
 *      Sensor suhu dan kelembapan udara. Digunakan untuk memantau
 *      kondisi termal di dalam toilet. Jika suhu melebihi ambang batas
 *      (TEMP_WARNING), sistem akan menampilkan status WARNING.
 *
 *   2. MQ135 (Pin GPIO34 - Analog)
 *      Sensor gas kualitas udara, mendeteksi keberadaan gas seperti
 *      amonia, CO2, dan senyawa organik volatil lainnya (indikator bau
 *      tidak sedap / kualitas udara buruk). Pembacaan mentah (raw) akan
 *      dibandingkan dengan nilai baseline hasil kalibrasi untuk
 *      menghasilkan "Gas Index". Semakin tinggi Gas Index, semakin
 *      buruk kualitas udara dibanding kondisi normal (baseline).
 *        - Gas Index >= GAS_WARNING  -> status WARNING
 *        - Gas Index >= GAS_CRITICAL -> status CRITICAL
 *
 *   3. LED Indikator
 *      - LED_GREEN  (Pin GPIO25) : Menyala saat kondisi NORMAL
 *                                  (suhu & gas dalam batas aman).
 *      - LED_YELLOW (Pin GPIO26) : Menyala saat kondisi WARNING
 *                                  (suhu tinggi dan/atau gas mulai
 *                                  terdeteksi di atas ambang WARNING).
 *      - LED_RED    (Pin GPIO27) : Berkedip (blink) saat kondisi
 *                                  CRITICAL (gas index mencapai level
 *                                  berbahaya) atau saat sensor DHT11
 *                                  gagal terbaca (indikasi error).
 *
 * =====================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// =====================================================
// PIN
// =====================================================

#define DHT_PIN 4
#define DHT_TYPE DHT11

#define MQ135_PIN 34

#define LED_GREEN 25
#define LED_YELLOW 26
#define LED_RED 27

// OLED
#define OLED_SDA 21
#define OLED_SCL 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

DHT dht(DHT_PIN, DHT_TYPE);


// =====================================================
// PARAMETER SUHU
// =====================================================

const float TEMP_WARNING = 33.0;


// =====================================================
// PARAMETER GAS
// =====================================================

const float GAS_WARNING = 1.50;
const float GAS_CRITICAL = 2.00;


// =====================================================
// MQ135
// =====================================================

float gasBaseline = 0;


// =====================================================
// BLINK LED MERAH
// =====================================================

unsigned long previousMillis = 0;

const unsigned long BLINK_INTERVAL = 500;

bool redState = false;


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

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
  // ADC
  // -------------------------

  analogReadResolution(12);


  // -------------------------
  // OLED
  // -------------------------

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {

    Serial.println("OLED tidak ditemukan!");

    while (true) {
      delay(1000);
    }
  }


  // Tampilan awal OLED

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);

  display.setCursor(10, 5);
  display.println("TOILET");

  display.setCursor(20, 30);
  display.println("MONITOR");

  display.display();

  delay(2000);


  // -------------------------
  // MQ135 WARM-UP
  // -------------------------

  Serial.println();
  Serial.println("================================");
  Serial.println("      ESP32 TOILET MONITOR");
  Serial.println("================================");

  Serial.println();
  Serial.println("MQ135 sedang dipanaskan...");
  Serial.println("Tunggu 60 detik.");

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("MQ135 WARM-UP");

  display.setCursor(0, 20);
  display.println("Please wait...");

  display.setCursor(0, 40);
  display.println("60 seconds");

  display.display();


  delay(60000);


  // -------------------------
  // KALIBRASI
  // -------------------------

  calibrateMQ135();


  Serial.println();
  Serial.println("Kalibrasi selesai.");
  Serial.println("Monitoring dimulai.");
  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // ===================================================
  // DHT11
  // ===================================================

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();


  // ===================================================
  // CEK DHT11
  // ===================================================

  if (isnan(temperature) || isnan(humidity)) {

    Serial.println("ERROR: DHT11 tidak terbaca!");

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);

    blinkRed();

    displayError();

    delay(2000);

    return;
  }


  // ===================================================
  // MQ135
  // ===================================================

  int mqRaw = analogRead(MQ135_PIN);


  // ===================================================
  // GAS INDEX
  // ===================================================

  float gasIndex = mqRaw / gasBaseline;


  // ===================================================
  // STATUS
  // ===================================================

  bool temperatureWarning =
    temperature > TEMP_WARNING;

  bool gasWarning =
    gasIndex >= GAS_WARNING;

  bool gasCritical =
    gasIndex >= GAS_CRITICAL;


  // ===================================================
  // STATUS + LED
  // ===================================================

  if (gasCritical) {

    // -------------------------
    // CRITICAL
    // -------------------------

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);

    blinkRed();

    showOLED(
      temperature,
      humidity,
      mqRaw,
      gasIndex,
      "CRITICAL"
    );

    Serial.println("STATUS : CRITICAL");

  }

  else if (temperatureWarning || gasWarning) {

    // -------------------------
    // WARNING
    // -------------------------

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_RED, LOW);

    redState = false;

    showOLED(
      temperature,
      humidity,
      mqRaw,
      gasIndex,
      "WARNING"
    );

    Serial.println("STATUS : WARNING");

  }

  else {

    // -------------------------
    // NORMAL
    // -------------------------

    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);

    redState = false;

    showOLED(
      temperature,
      humidity,
      mqRaw,
      gasIndex,
      "NORMAL"
    );

    Serial.println("STATUS : NORMAL");
  }


  // ===================================================
  // SERIAL MONITOR
  // ===================================================

  Serial.println("--------------------------------");

  Serial.print("Suhu       : ");
  Serial.print(temperature, 1);
  Serial.println(" C");

  Serial.print("Kelembapan : ");
  Serial.print(humidity, 1);
  Serial.println(" %");

  Serial.print("MQ135 RAW  : ");
  Serial.println(mqRaw);

  Serial.print("Baseline   : ");
  Serial.println(gasBaseline, 1);

  Serial.print("Gas Index  : ");
  Serial.println(gasIndex, 2);

  Serial.println("--------------------------------");
  Serial.println();


  delay(2000);
}


// =====================================================
// OLED DISPLAY
// =====================================================

void showOLED(
  float temperature,
  float humidity,
  int mqRaw,
  float gasIndex,
  const char* status
) {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);


  // -------------------------
  // STATUS
  // -------------------------

  display.setTextSize(2);

  display.setCursor(0, 0);

  display.println(status);


  // -------------------------
  // SUHU
  // -------------------------

  display.setTextSize(1);

  display.setCursor(0, 25);

  display.print("Temp : ");
  display.print(temperature, 1);
  display.println(" C");


  // -------------------------
  // HUMIDITY
  // -------------------------

  display.setCursor(0, 35);

  display.print("Hum  : ");
  display.print(humidity, 1);
  display.println(" %");


  // -------------------------
  // GAS
  // -------------------------

  display.setCursor(0, 45);

  display.print("Gas  : ");
  display.print(gasIndex, 2);


  // -------------------------
  // RAW
  // -------------------------

  display.setCursor(0, 55);

  display.print("RAW  : ");
  display.print(mqRaw);


  display.display();
}


// =====================================================
// OLED ERROR
// =====================================================

void displayError() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);

  display.setCursor(0, 0);
  display.println("ERROR");

  display.setTextSize(1);

  display.setCursor(0, 30);
  display.println("DHT11 tidak");

  display.setCursor(0, 42);
  display.println("terbaca!");

  display.display();
}


// =====================================================
// KALIBRASI MQ135
// =====================================================

void calibrateMQ135() {

  const int samples = 100;

  long total = 0;

  Serial.println("Mengambil baseline MQ135...");

  for (int i = 0; i < samples; i++) {

    int value = analogRead(MQ135_PIN);

    total += value;

    delay(100);
  }

  gasBaseline = (float)total / samples;

  Serial.print("Baseline MQ135 = ");
  Serial.println(gasBaseline);


  // OLED

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("MQ135 CALIBRATION");

  display.setCursor(0, 20);
  display.print("Baseline: ");

  display.println(gasBaseline, 0);

  display.setCursor(0, 45);
  display.println("Done!");

  display.display();

  delay(2000);
}


// =====================================================
// BLINK LED MERAH
// =====================================================

void blinkRed() {

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= BLINK_INTERVAL) {

    previousMillis = currentMillis;

    redState = !redState;

    digitalWrite(LED_RED, redState);
  }
}

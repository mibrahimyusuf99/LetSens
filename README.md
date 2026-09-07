# LetSens Toilet

Sistem monitoring lingkungan toilet berbasis ESP32 (AIoT) — memantau suhu, kelembapan, kualitas udara, kehadiran pengguna, dan intensitas cahaya secara real-time. Data dikirim ke cloud lewat MQTT, ditampilkan lokal lewat OLED & web dashboard, dan diberi indikator status melalui LED.

**Versi Produk:** 1.0
**Versi Firmware:** 2.0
**Dibuat oleh:** M. Ibrahim Yusuf

---

## Daftar Isi

- [Tentang Proyek](#tentang-proyek)
- [Fitur](#fitur)
- [Komponen Hardware](#komponen-hardware)
- [Skema Pin](#skema-pin)
- [Arsitektur Sistem](#arsitektur-sistem)
- [Cara Kerja](#cara-kerja)
- [Instalasi](#instalasi)
- [Konfigurasi WiFi (WiFiManager)](#konfigurasi-wifi-wifimanager)
- [Konfigurasi MQTT](#konfigurasi-mqtt)
- [Format Data (Payload MQTT)](#format-data-payload-mqtt)
- [Web Dashboard Lokal](#web-dashboard-lokal)
- [Konfigurasi Threshold](#konfigurasi-threshold)
- [Status Indikator](#status-indikator)
- [Roadmap](#roadmap)
- [Troubleshooting](#troubleshooting)

---

## Tentang Proyek

LetSens Toilet adalah alat monitoring kondisi toilet menggunakan ESP32 sebagai mikrokontroler utama. Alat ini membaca suhu, kelembapan, dan kualitas udara (deteksi gas) secara langsung dari sensor fisik, serta mensimulasikan kehadiran pengguna (PIR) dan intensitas cahaya (lux) sebagai placeholder sebelum sensor fisiknya dipasang. Seluruh data dikirim berkala ke MQTT broker (HiveMQ Cloud) untuk diproses lebih lanjut oleh backend/dashboard cloud, sekaligus bisa dipantau langsung dari layar OLED perangkat maupun web dashboard lokal.

## Fitur

- Pembacaan suhu & kelembapan real-time  
- Deteksi kualitas udara / gas (MQ135) dengan kalibrasi baseline otomatis saat startup —
- Deteksi kehadiran pengguna & durasi di toilet (PIR) — **saat ini masih data simulasi/dummy**, siap diganti sensor asli
- Estimasi intensitas cahaya (lux) 
- **WiFiManager**: konfigurasi WiFi tanpa hardcode — kalau belum ada/gagal konek WiFi, ESP32 otomatis jadi Access Point untuk setup ulang
- **Koneksi MQTT ke HiveMQ Cloud** (TLS) — publish data sensor otomatis setiap 30 detik
- **Timestamp tersinkron NTP** pada setiap data yang dikirim (format epoch & waktu lokal WIB)
- **mDNS (DNS lokal)** — dashboard bisa diakses lewat `http://letsens.local`, tidak perlu tahu/hafal IP device
- **Web dashboard lokal** menampilkan seluruh data sensor + status koneksi/pengiriman MQTT secara real-time
- Indikator status visual melalui 3 LED (Hijau / Kuning / Merah)
- Tampilan status detail di layar OLED
- Log data ke Serial Monitor untuk debugging/monitoring

## Komponen Hardware

| Komponen | Fungsi | Status |
|---|---|---|
| ESP32 DevKit | Mikrokontroler utama | Terpasang |
| DHT11 | Sensor suhu & kelembapan | Terpasang, data asli |
| MQ135 | Sensor kualitas udara / gas | Terpasang, data asli |
| PIR Motion Sensor | Deteksi kehadiran pengguna | **Belum terpasang** — data dummy |
| Sensor Cahaya (BH1750/LDR) | Intensitas cahaya (lux) | **Belum terpasang** — data dummy |
| LED Hijau, Kuning, Merah | Indikator status kondisi | Terpasang |
| OLED SSD1306 128x64 (I2C) | Tampilan data & status lokal | Terpasang |

## Skema Pin

| Pin ESP32 | Fungsi |
|---|---|
| GPIO4  | DHT11 (Data) |
| GPIO34 | MQ135 (Analog Out) |
| GPIO25 | LED Hijau |
| GPIO26 | LED Kuning |
| GPIO27 | LED Merah |
| GPIO21 | OLED SDA |
| GPIO22 | OLED SCL |
| GPIO32 | PIR (placeholder — belum dipakai, masih dummy) |
| GPIO35 | Sensor Cahaya (placeholder — belum dipakai, masih dummy) |
| GPIO0  | Tombol BOOT — ditahan saat power-on untuk reset WiFi & buka portal setup |

## Arsitektur Sistem

```
[Sensor: DHT11, MQ135, PIR*, Light*]
              |
              v
        [ESP32 DevKit]
       /      |       \
   [OLED]  [3x LED]  [WiFiManager]
              |             |
       [Web Server Lokal]  [WiFi Rumah/Kantor]
       [+ mDNS letsens.local]    |
                                  v
                    [MQTT Broker: HiveMQ Cloud (TLS)]
                                  |
                                  v
                    [Backend / Dashboard Cloud]

* PIR & Sensor Cahaya: data masih simulasi (dummy), belum sensor fisik
```

## Cara Kerja

1. **Startup** — ESP32 menginisialisasi OLED, LED, dan mencoba konek ke WiFi tersimpan lewat WiFiManager.
2. **WiFi belum ada/gagal konek** — ESP32 otomatis membuka Access Point (`LETSENS-Setup`) untuk konfigurasi WiFi lewat portal browser (lihat [Konfigurasi WiFi](#konfigurasi-wifi-wifimanager)).
3. **Sinkronisasi waktu (NTP)** — setelah WiFi terhubung, ESP32 sinkronisasi jam lewat NTP (zona WIB) untuk timestamp data.
4. **Aktivasi mDNS** — setelah WiFi terhubung, ESP32 mendaftarkan hostname `letsens.local` di jaringan lokal, sehingga dashboard bisa diakses tanpa perlu tahu IP.
5. **Koneksi MQTT** — ESP32 connect ke HiveMQ Cloud broker via TLS, mempublikasikan status `online` (dengan Last Will `offline` kalau device terputus tiba-tiba).
6. **Web server lokal aktif** — dashboard bisa diakses dari `http://letsens.local` (atau IP lokal ESP32 sebagai cadangan) di jaringan yang sama.
7. **Warm-up MQ135** — sensor gas dipanaskan selama 60 detik agar pembacaan stabil (wajib untuk sensor jenis MQ).
8. **Kalibrasi baseline** — sistem mengambil 100 sampel pembacaan MQ135 untuk menentukan nilai baseline udara normal di lokasi pemasangan.
9. **Monitoring loop** — setiap 2 detik, sistem membaca seluruh sensor (real & dummy), menentukan status (`NORMAL`/`WARNING`/`CRITICAL`/`ERROR`), lalu update OLED, LED, dan web dashboard.
   - `Gas Index = Nilai MQ135 saat ini / Baseline`
10. **Publish MQTT** — setiap 30 detik, seluruh data sensor dikirim sebagai payload JSON ke topic `letsens/toilet/sensordata` (lihat [Format Data](#format-data-payload-mqtt)).

## Instalasi

1. Install [Arduino IDE](https://www.arduino.cc/en/software) dan board package **ESP32**.
2. Install library berikut melalui Library Manager:
   - `DHT sensor library` (Adafruit)
   - `Adafruit Unified Sensor`
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
   - `WiFiManager` (by tzapu)
   - `PubSubClient` (by Nick O'Leary)
   - `ArduinoJson` (by Benoit Blanchon, v6/v7)
   - *(`WiFi.h`, `WiFiClientSecure.h`, `WebServer.h`, `ESPmDNS.h`, `time.h` sudah termasuk di ESP32 core, tidak perlu install manual)*
3. Hubungkan seluruh komponen sesuai [Skema Pin](#skema-pin).
4. Buka file `firmwareletsens_v2.ino`, pilih board **ESP32 Dev Module**, lalu upload.
5. Buka Serial Monitor (baud rate `115200`) untuk melihat log data & status koneksi.

## Konfigurasi WiFi (WiFiManager)

Perangkat **tidak menyimpan SSID/password WiFi langsung di kode**. Cara setup:

1. Nyalakan device. Kalau belum pernah dikonfigurasi (atau kredensial lama gagal konek), OLED akan menampilkan mode setup dan device membuka WiFi hotspot bernama **`LETSENS-Setup`** (password: `letsens123`).
2. Dari HP/laptop, konek ke WiFi `LETSENS-Setup` tersebut.
3. Portal konfigurasi akan otomatis terbuka (captive portal). Kalau tidak muncul otomatis, buka manual `192.168.4.1` di browser — mDNS belum aktif pada tahap ini karena device belum terhubung ke jaringan WiFi utama.
4. Pilih WiFi rumah/kantor yang ingin dipakai, masukkan passwordnya, lalu simpan.
5. Device akan restart otomatis dan konek ke WiFi yang baru dimasukkan. Setelah terhubung, dashboard langsung bisa diakses via `http://letsens.local`.

**Untuk mengganti WiFi di kemudian hari**, ada dua cara:
- Buka web dashboard lokal (lihat [Web Dashboard Lokal](#web-dashboard-lokal)) → klik tombol **"Ganti Konfigurasi WiFi"**, atau
- Tahan tombol **BOOT (GPIO0)** saat menyalakan device, kredensial lama akan terhapus dan portal setup terbuka lagi.

## Konfigurasi MQTT

| Parameter | Nilai |
|---|---|
| Broker | HiveMQ Cloud |
| Host | `fcb0ad941e3f418f8dc1d16332c8fcb9.s1.eu.hivemq.cloud` |
| Port | `8883` (MQTT over TLS) |
| Username | `letsens` |
| Password | `letsens123` |
| Client ID | `LETSENS-01` |
| Topic data sensor | `letsens/toilet/sensordata` |
| Topic status device | `letsens/toilet/status` (`online`/`offline`, retained) |
| Interval publish | 30 detik |

> Dokumentasi lebih lengkap untuk integrasi backend/subscriber ada di `MQTT_INTEGRATION_NOTES.md`.

## Format Data (Payload MQTT)

Data dikirim sebagai JSON string ke topic `letsens/toilet/sensordata`:

```json
{
  "device_id": "LETSENS-01",
  "timestamp": 1788645323,
  "time": "2026-09-04 09:15:23",
  "uptime_sec": 1234,
  "wifi_rssi": -55,
  "temperature_c": 29.5,
  "humidity_percent": 68.2,
  "gas_raw": 1450,
  "gas_index": 1.12,
  "pir_presence": true,
  "pir_duration_sec": 42,
  "light_lux": 185.3,
  "status": "NORMAL"
}
```

| Field | Keterangan |
|---|---|
| `device_id` | ID unik perangkat |
| `timestamp` | Unix epoch (UTC), hasil sinkronisasi NTP |
| `time` | Format waktu lokal WIB (`YYYY-MM-DD HH:MM:SS`) |
| `temperature_c`, `humidity_percent` | Bisa bernilai `null` kalau sensor DHT11 gagal dibaca |
| `gas_raw`, `gas_index` | Data mentah & rasio terhadap baseline kalibrasi MQ135 |
| `pir_presence`, `pir_duration_sec` | **Data simulasi/dummy** — kehadiran & durasi di toilet |
| `light_lux` | **Data simulasi/dummy** — estimasi intensitas cahaya |
| `status` | `NORMAL` / `WARNING` / `CRITICAL` / `ERROR` |

## Web Dashboard Lokal

Selama ESP32 terhubung ke WiFi, dashboard bisa diakses lewat browser di jaringan yang sama menggunakan hostname mDNS (tidak perlu tahu IP device):

```
http://letsens.local/
```

Kalau perangkat/OS Anda tidak mendukung mDNS (beberapa versi Windows lama tanpa Bonjour service), gunakan IP address sebagai cadangan — bisa dilihat di Serial Monitor saat boot, di layar OLED sesaat setelah WiFi terhubung, atau di kartu "Koneksi WiFi" pada dashboard itu sendiri.

Dashboard menampilkan:
- Status sistem (NORMAL/WARNING/CRITICAL/ERROR)
- Suhu, kelembapan, gas index (data real-time)
- Status PIR & lux (dummy)
- Info WiFi (hostname `.local`, IP, RSSI) dan waktu device
- **Status koneksi & pengiriman MQTT** (Terhubung/Terputus, Berhasil/Gagal, pesan error terakhir)
- Tombol reset konfigurasi WiFi

Auto-refresh tiap 3 detik lewat endpoint `/data` (JSON).

## Konfigurasi Threshold

Parameter ambang batas dapat disesuaikan langsung di kode sesuai kondisi ruangan:

```cpp
const float TEMP_WARNING = 33.0;   // Ambang suhu warning (°C)

const float GAS_WARNING  = 1.50;   // Ambang Gas Index untuk WARNING
const float GAS_CRITICAL = 2.00;   // Ambang Gas Index untuk CRITICAL
```

> Nilai `GAS_WARNING` dan `GAS_CRITICAL` bersifat relatif terhadap baseline hasil kalibrasi, bukan nilai mutlak — sesuaikan berdasarkan kondisi lapangan setelah beberapa kali pengujian.

## Status Indikator

| Status | LED | Kondisi |
|---|---|---|
| **NORMAL** | Hijau menyala | Suhu & gas dalam batas aman |
| **WARNING** | Kuning menyala | Suhu > ambang, dan/atau Gas Index ≥ `GAS_WARNING` |
| **CRITICAL** | Merah berkedip | Gas Index ≥ `GAS_CRITICAL` |
| **ERROR** | Merah menyala | Sensor DHT11 gagal dibaca |

## Roadmap

- [ ] Pasang sensor PIR fisik, ganti fungsi simulasi (`simulatePIR()`) dengan pembacaan pin asli
- [ ] Pasang sensor cahaya fisik (BH1750/LDR), ganti fungsi simulasi (`simulateLight()`) dengan pembacaan sensor asli
- [ ] Pertimbangkan pinning root CA certificate untuk koneksi TLS ke HiveMQ (saat ini pakai `setInsecure()` untuk kemudahan development)
- [ ] Backend/dashboard cloud untuk menyimpan & memvisualisasikan data historis (lihat diagram blok sistem)
- [ ] Hostname mDNS unik per device (`letsens01.local`, `letsens02.local`, dst) kalau nanti dipasang lebih dari 1 unit

## Troubleshooting

**DHT11 tidak terbaca / selalu error:**
- Pastikan wiring DATA, VCC, dan **GND** semua tersambung dengan benar (GND yang lepas adalah penyebab paling umum).
- Jika sensor jenis bare (bukan modul PCB), tambahkan resistor pull-up 4.7k–10kΩ di pin data.

**OLED tidak menyala:**
- Jalankan I2C scanner untuk memastikan alamat I2C (umumnya `0x3C`).
- Pastikan wiring SDA → GPIO21, SCL → GPIO22.
- Jika sudah terdeteksi I2C tapi layar tetap gelap, kemungkinan modul OLED rusak secara fisik — coba ganti dengan modul lain untuk memastikan.

**Gas Index tidak akurat / status WARNING terus muncul:**
- Pastikan proses warm-up (60 detik) dan kalibrasi baseline selesai sebelum menilai hasil pembacaan.
- Kalibrasi ulang jika alat dipindahkan ke lokasi dengan kondisi udara berbeda.

**WiFi tidak konek / portal setup tidak muncul:**
- Cek OLED — kalau menampilkan mode setup, konek HP/laptop ke WiFi `LETSENS-Setup` dan buka `192.168.4.1`.
- Kalau device stuck restart terus, tahan tombol BOOT (GPIO0) saat power-on untuk menghapus kredensial WiFi lama.

**`http://letsens.local` tidak bisa dibuka:**
- Pastikan device yang dipakai untuk mengakses (HP/laptop) berada di jaringan WiFi yang **sama persis** dengan ESP32.
- Beberapa jaringan WiFi kantor/kampus (dengan client isolation) memblokir mDNS antar perangkat — coba akses via IP sebagai alternatif.
- Windows tanpa iTunes/Bonjour terpasang biasanya tidak mendukung mDNS secara native — install [Bonjour Print Services](https://support.apple.com/kb/DL999) atau gunakan IP address.
- Android & iOS umumnya sudah mendukung mDNS bawaan, begitu juga macOS dan sebagian besar distro Linux.

**Data tidak muncul di MQTT broker (MQTTX/HiveMQ Console):**
- Pastikan subscribe ke topic yang benar: `letsens/toilet/sensordata` atau wildcard `letsens/toilet/#`.
- Cek Serial Monitor — harus muncul log `MQTT: publish BERHASIL` setiap 30 detik. Kalau muncul `GAGAL`, cek status error di web dashboard atau Serial Monitor.
- Pastikan device terhubung WiFi dengan koneksi internet yang stabil.

---

© LetSens Toilet — M. Ibrahim Yusuf

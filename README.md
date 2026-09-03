# LetSens Toilet

Sistem monitoring lingkungan toilet berbasis ESP32 — memantau suhu, kelembapan, dan kualitas udara secara real-time, dengan indikator status melalui LED dan tampilan OLED.

**Versi Produk:** 1.0
**Versi Firmware:** 1.0
**Dibuat oleh:** M. Ibrahim Yusuf

---

## Daftar Isi

- [Tentang Proyek](#tentang-proyek)
- [Fitur](#fitur)
- [Komponen Hardware](#komponen-hardware)
- [Skema Pin](#skema-pin)
- [Cara Kerja](#cara-kerja)
- [Instalasi](#instalasi)
- [Konfigurasi](#konfigurasi)
- [Status Indikator](#status-indikator)
- [Troubleshooting](#troubleshooting)

---

## Tentang Proyek

LetSens Toilet adalah alat monitoring kondisi udara di dalam toilet menggunakan ESP32 sebagai mikrokontroler utama. Alat ini membaca suhu, kelembapan, dan kualitas udara (deteksi gas) secara berkala, lalu menampilkan status kondisi (NORMAL / WARNING / CRITICAL) melalui LED dan layar OLED.

## Fitur

- Pembacaan suhu & kelembapan real-time (DHT11)
- Deteksi kualitas udara / gas (MQ135) dengan kalibrasi baseline otomatis saat startup
- Indikator status visual melalui 3 LED (Hijau / Kuning / Merah)
- Tampilan status detail di layar OLED
- Log data ke Serial Monitor untuk debugging/monitoring

## Komponen Hardware

| Komponen | Fungsi |
|---|---|
| ESP32 DevKit | Mikrokontroler utama |
| DHT11 | Sensor suhu & kelembapan |
| MQ135 | Sensor kualitas udara / gas |
| LED Hijau, Kuning, Merah | Indikator status kondisi |
| OLED SSD1306 128x64 (I2C) | Tampilan data & status |

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

## Cara Kerja

1. **Startup** — ESP32 menginisialisasi seluruh sensor, LED, dan OLED.
2. **Warm-up MQ135** — sensor gas dipanaskan selama 60 detik agar pembacaan stabil (wajib untuk sensor jenis MQ).
3. **Kalibrasi baseline** — sistem mengambil 100 sampel pembacaan MQ135 untuk menentukan nilai baseline udara normal di lokasi pemasangan.
4. **Monitoring loop** — setiap 2 detik, sistem membaca suhu, kelembapan, dan gas, lalu membandingkan dengan ambang batas (threshold) untuk menentukan status:
   - `Gas Index = Nilai MQ135 saat ini / Baseline`
   - Status ditentukan dari kombinasi suhu dan Gas Index.
5. Status ditampilkan di OLED, dikirim ke Serial Monitor, dan direfleksikan melalui LED.

## Instalasi

1. Install [Arduino IDE](https://www.arduino.cc/en/software) dan board package **ESP32**.
2. Install library berikut melalui Library Manager:
   - `DHT sensor library` (Adafruit)
   - `Adafruit Unified Sensor`
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
3. Hubungkan seluruh komponen sesuai [Skema Pin](#skema-pin).
4. Buka file `LetSens_Toilet.ino`, pilih board **ESP32 Dev Module**, lalu upload.
5. Buka Serial Monitor (baud rate `115200`) untuk melihat log data.

## Konfigurasi

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
| **ERROR** | Merah berkedip | Sensor DHT11 gagal dibaca |

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

---

© LetSens Toilet — M. Ibrahim Yusuf

# Wiring Diagram — LetSens Toilet Monitoring System

> Sumber kebenaran (source of truth): **Skematik PCB LetSens V2** (EasyEDA, update 2026‑09‑07).
> Mikrokontroler: **ESP32-DEVKIT-V1**
>
> Firmware (`LetSens_Toilet_V2_4.ino`) masih akan disesuaikan agar mengikuti wiring di dokumen ini — khususnya bagian sensor suhu/kelembaban yang di kode masih memakai DHT11, sementara di PCB sudah terpasang **GY-SHT31 (I2C)**.

---

## 1. Power Distribution

| Sumber | Ke | Keterangan |
|---|---|---|
| DC1 (Jack DC-005) pin 1 (+) | SW2 (Rocker Switch) pin 2 | Input baterai/adaptor |
| SW2 pin 1 | F2 (Fuse RXEF160) | Sakelar ON/OFF utama |
| F2 → net **7.4V** | U11 (MINI560 Buck Converter) pin **IN-** (1) | Tegangan input step-down |
| DC1 pin 2 (−) | GND | |
| U11 pin **IN+** (2) | GND | |
| U11 pin **OUT-** (3) | GND | Output step-down |
| U11 pin **OUT+** (4) → net **5V** | C1 (1000 µF) + rail 5V | Rail 5V untuk sensor & ESP32 VIN |
| ESP32 pin **VIN** (30) | Rail **5V** | Suplai utama ESP32 |
| ESP32 pin **3V3** (pin 1, output) | Rail **3V3** | Suplai untuk sensor I2C & OLED |

---

## 2. Tabel Pin ESP32-DEVKIT-V1 (U10)

| GPIO (Label Skematik) | No. Pin | Terhubung ke (Net) | Fungsi |
|---|---|---|---|
| VIN | 30 | 5V | Power in |
| EN | 16 | *(tidak dipakai/NC)* | Reset |
| VP | 17 | *(NC)* | – |
| VN | 18 | *(NC)* | – |
| 3V3 | 1 | 3V3 (rail) | Power out 3.3V |
| RX0 / TX0 | 12 / 13 | *(NC — reserved USB Serial)* | – |
| RX2 / TX2 | 6 / 7 | *(NC)* | – |
| D2 | 4 | *(NC)* | – |
| D4 | 5 | *(NC di skematik)* | Bekas jalur DHT11 (sudah tidak dipakai, lihat catatan §10) |
| D5 | 8 | *(NC)* | – |
| D12 | 27 | *(NC)* | – |
| D13 | 28 | *(NC)* | – |
| D14 | 26 | *(NC)* | – |
| D15 | 3 | *(NC)* | – |
| D18 | 9 | net **D18** → EXT 2 (CN8 pin 3) | GPIO ekspansi |
| D19 | 10 | *(NC)* | – |
| D21 | 11 | net **SCL** | I2C Clock (bus OLED/SHT31/BH1750) |
| D22 | 14 | net **SDA** | I2C Data (bus OLED/SHT31/BH1750) |
| D23 | 15 | *(NC)* | – |
| D25 | 23 | net **LED_HIJAU** | LED indikator hijau |
| D26 | 24 | net **LED_KUNING** | LED indikator kuning |
| D27 | 25 | net **LED_MERAH** | LED indikator merah |
| D32 | 21 | net **PIR** | Sensor PIR |
| D33 | 22 | net **D33** → EXT 3 (CN9 pin 3) | GPIO ekspansi |
| D35 | 20 | *(NC)* | – |
| D34 | 19 | net **MQ-135** | Analog input gas amonia (ADC) |
| GND | 2 | GND (rail) | Ground |

---

## 3. Sensor PIR (CN3) — *Passive Infrared*

Konektor: `LAIL-PHC2.54-3P-01-XH-L`

| Pin | Net | Ke |
|---|---|---|
| 1 | 3V3 | Rail 3.3V |
| 2 | GND | Rail GND |
| 3 | PIR | ESP32 GPIO32 |

---

## 4. Sensor MQ-135 (CN4) — *Gas Amonia*

Konektor: `LAIL-PHC2.54-3P-01-XH-L`

| Pin | Net | Ke |
|---|---|---|
| 1 | 5V | Rail 5V |
| 2 | GND | Rail GND |
| 3 | AOUT | Pembagi tegangan R3 (20kΩ, seri) + R4 (10kΩ, ke GND) → net **MQ-135** → ESP32 GPIO34 (ADC) |

Pembagi tegangan menurunkan AOUT (≈5V) agar aman masuk ADC ESP32 (maks 3.3V).

---

## 5. Sensor Suhu & Kelembaban — GY-SHT31 (CN5)

Konektor: `LAIL-PHC2.54-4P-01-XH-L`

| Pin | Net | Ke |
|---|---|---|
| 1 | 3V3 | Rail 3.3V |
| 2 | SCL | ESP32 GPIO21 |
| 3 | SDA | ESP32 GPIO22 |
| 4 | GND | Rail GND |

---

## 6. Sensor Intensitas Cahaya — BH1750 (CN2)

Konektor: `LAIL-PHC2.54-4P-01-XH-L`

| Pin | Net | Ke |
|---|---|---|
| 1 | 3V3 | Rail 3.3V |
| 2 | SCL | ESP32 GPIO21 |
| 3 | SDA | ESP32 GPIO22 |
| 4 | GND | Rail GND |

---

## 7. Output Display OLED (CN6)

| Pin | Net | Ke |
|---|---|---|
| 1 | 3V3 | Rail 3.3V |
| 2 | GND | Rail GND |
| 3 | SCL | ESP32 GPIO21 |
| 4 | SDA | ESP32 GPIO22 |

> OLED, SHT31, dan BH1750 berbagi bus I2C yang sama (SDA/SCL paralel).

---

## 8. Indikator LED

| LED | Resistor | Net (dari ESP32) | GPIO |
|---|---|---|---|
| LED Hijau (U15) | R5 = 220Ω | LED_HIJAU | GPIO25 |
| LED Kuning (U14) | R6 = 220Ω | LED_KUNING | GPIO26 |
| LED Merah (U13) | R7 = 220Ω | LED_MERAH | GPIO27 |

Rangkaian tiap LED: `GND → Resistor 220Ω → LED → GPIO`.

---

## 9. Konektor Ekspansi (EXT)

**EXT 1 (CN7)** — `LAIL-PHC2.54-4P-01-XH-L`

| Pin | Net |
|---|---|
| 1 | 3V3 |
| 2 | GND |
| 3 | SCL |
| 4 | SDA |

**EXT 2 (CN8)** — `LAIL-PHC2.54-3P-01-XH-L`

| Pin | Net |
|---|---|
| 1 | 3V3 |
| 2 | GND |
| 3 | D18 (GPIO18) |

**EXT 3 (CN9)** — `LAIL-PHC2.54-3P-01-XH-L`

| Pin | Net |
|---|---|
| 1 | **5V** ⚠️ (bukan 3V3) |
| 2 | GND |
| 3 | D33 (→ GPIO33) |

---

## 10. Catatan & To-Do untuk Update Firmware

Wiring di dokumen ini adalah **acuan final** (mengikuti skematik PCB). Beberapa hal yang perlu disesuaikan di sisi firmware (`LetSens_Toilet_V2_4.ino`):

| # | Item | Kondisi Firmware saat ini | Wiring Final (Skematik) | Aksi |
|---|---|---|---|---|
| 1 | Sensor suhu/kelembaban | Pakai library `DHT` di `DHT_PIN = 4` (DHT11) | Sensor terpasang: **GY-SHT31**, komunikasi **I2C** (bukan GPIO4) | 🔧 **Perlu diganti** ke driver/library SHT31 (mis. `Adafruit_SHT31`), baca lewat bus I2C yang sama dengan OLED & BH1750 |
| 2 | I2C SDA/SCL | `Wire.begin(OLED_SDA=21, OLED_SCL=22)` | Sesuai skematik: GPIO21 = **SCL**, GPIO22 = **SDA** (kebalikan dari definisi saat ini) | 🔧 Cek/sesuaikan urutan argumen `Wire.begin()` agar sesuai jalur fisik di PCB |
| 3 | PIR | `PIR_PIN = 32` | GPIO32 | ✅ Sudah sesuai, tidak perlu diubah |
| 4 | LED Hijau/Kuning/Merah | `LED_GREEN=25, LED_YELLOW=26, LED_RED=27` | GPIO25 / GPIO26 / GPIO27 | ✅ Sudah sesuai, tidak perlu diubah |
| 5 | MQ-135 | `MQ135_PIN = 34` | GPIO34 | ✅ Sudah sesuai, tidak perlu diubah |
| 6 | EXT 3 | — | Menyediakan **5V**, bukan 3.3V seperti EXT 1 & EXT 2 | ⚠️ Hindari colok modul 3.3V ke EXT 3 |
| 7 | GPIO33 (net "D33" ke EXT 3) | Belum dipakai | Diteruskan ke header **EXT 3** pin 3 | ℹ️ GPIO cadangan/ekspansi untuk pengembangan berikutnya |

Item #1 dan #2 di atas adalah bagian yang perlu diubah saat kode disesuaikan dengan sensor yang benar-benar terpasang di skematik.

---

*Dibuat berdasarkan `Skematik PCB LetSens V2` (acuan final wiring). Board: LetSens · Drawn by: Ibrahim · Update skematik: 2026-09-07.*

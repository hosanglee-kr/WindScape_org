![WindScape Title](https://github.com/TilmanGriesel/WindScape/blob/main/docs/title.png?raw=true)

# 🌿 Smart Nature Wind 단독 실행 버전 (WindScape – Standalone Ver.)

* **소스**
  https://github.com/TilmanGriesel/WindScape.git

---

*단순히 일정한 세기의 바람만 내보내던 DC 선풍기나 PC 팬을,  
자연 그대로의 바람처럼 살아있는 공기로 바꿔주는 스마트 제어기 프로젝트입니다.*

Smart Nature Wind는 잔잔한 지중해의 바람부터 알프스 산맥의 상쾌한 돌풍까지,  
자연의 장소에서 영감을 받은 현실적인 바람을 만들어냅니다.  
사무실, 스터디 카페, 침실, 책상 어디서든  
자연스럽고 몰입감 있는 분위기를 연출할 수 있으며,  
당신이 상상하는 모든 공간과 프로젝트에 생동감을 불어넣습니다.

![noctua_nv_fs1_1](https://noctua.at/pub/media/catalog/product/cache/74c1057f7991b4edb2bc7bdaa94de933/n/o/noctua_nv_fs1_5.jpg)

---

## 📋 목차 (Table of Contents)

1. [기능 (Features)](#기능-features)  
2. [작동 모드 (Operating Modes)](#작동-모드-operating-modes)  
3. [데모 (Demo)](#데모-demo)  
4. [제작 가이드 (Build Guides)](#제작-가이드-build-guides)  
5. [소프트웨어 설정 (Software Setup)](#소프트웨어-설정-software-setup)  
6. [프리셋 라이브러리 (Preset Library)](#프리셋-라이브러리-preset-library)  
7. [문제 해결 (Troubleshooting)](#문제-해결-troubleshooting)  
8. [최상의 사용 팁 (Tips for Best Experience)](#최상의-사용-팁-tips-for-best-experience)  
9. [기술 노트 (Technical Notes)](#기술-노트-technical-notes)  
10. [로드맵 (Roadmap)](#로드맵-roadmap)  
11. [WindScape 작동 원리 (How WindScape Works)](#windscape-작동-원리-how-windscape-works)  
12. [링크 (Links)](#링크-links)

---

## 🌬 기능 (Features)

* **현실적인 바람 물리 (Realistic Wind Physics)**  
  실제 자연의 흐름을 기반으로,  
  스마트 난류 모델링과 열기(thermal) 효과를 조합해  
  진짜 야외의 바람처럼 부드럽고 리듬감 있는 패턴을 만듭니다.

* **자연에서 영감을 받은 프리셋 (Nature-Inspired Presets)**  
  시골의 잔잔한 산들바람부터 강한 바닷바람까지,  
  실제 장소의 감성을 담은 8가지 환경 프리셋이 기본 제공됩니다.

* **정적 / 동적 모드 (Steady or Dynamic Modes)**  
  - **정적 모드**: 일정한 세기의 부드러운 바람 유지  
  - **동적 모드**: 선택한 환경에 따라 시시각각 변화하는 자연스러운 바람

* **외부 센서 연동 (External Sensor Support)**  
  온도 센서를 연결하면 주변 온도에 따라  
  바람 세기가 자동으로 조정되어, 더욱 현실적인 체감 바람을 제공합니다.

* **Home Assistant 연동 (Home Assistant Integration)**  
  Wi-Fi로 연결해 집 안 어디서든 제어하고,  
  자동화 설정으로 시간·환경에 따라 바람을 제어할 수 있습니다.

* **실시간 모니터링 (Real-Time Monitoring)**  
  현재 바람 세기, 난류 수준, 열기 이벤트, 목표 속도 등  
  모든 시뮬레이션 정보를 실시간으로 확인할 수 있습니다.

* **무선 펌웨어 업데이트 (OTA Firmware Updates)**  
  복잡한 프로그램 설치 없이,  
  웹 브라우저에서 바로 펌웨어를 업데이트할 수 있습니다.

---

## ⚙ 작동 모드 (Operating Modes)

| 모드 | 설명 |
|------|------|
| **Wind Simulation (자연풍 모드)** | 끊임없이 변화하는 자연스러운 공기 흐름을 시뮬레이션합니다. |
| **Constant Breeze (정속 모드)** | 일정한 세기의 바람을 유지합니다. |
| **Temperature Sensor Mode (온도 센서 모드)** | 온도 센서 입력값에 따라 자동으로 바람 세기를 조정합니다. |

> 💡 **전원 컨트롤 (글로벌 Power Control)** 은 언제든 모든 모드를 우선 제어합니다.

---

## 🗺️ 4. 환경 프리셋 (10가지 자연 모드)

자연의 기류를 테마별로 담은 **10종 환경 프리셋**은  
단순한 풍속 조절을 넘어 **감정과 분위기를 전달하는 바람**을 만듭니다.

| 번호 | 모드명 | 슬로건 | 특징 요약 |
|------|---------|----------|-------------|
| 1 | 시골 바람 | “고요한 평화, 쉼을 들이켜다.” | 낮은 풍속과 낮은 난류, 휴식·독서용 |
| 2 | 지중해성 바람 | “햇살 아래 쾌적함, 이탈리아의 오후.” | 따뜻하고 부드러운 해안의 바람 |
| 3 | 해변 바람 | “거침없는 시원함, 대양의 숨결.” | 높은 난류와 강한 돌풍, 환기·집중용 |
| 4 | 산 바람 | “정상을 향한 도전, 짜릿한 상쾌함.” | 빠른 풍속 변화, 익스트림 시뮬레이션용 |
| 5 | 대평원 바람 | “가슴이 뻥 뚫리는, 야성의 해방감.” | 강력하고 광활한 기류, 냉각 효율 극대화 |
| 6 | 항구 바람 | “도시의 열기 속, 바다의 여운.” | 중간 강도의 습윤한 바람, 감성 연출 |
| 7 | 숲 그늘 바람 | “나뭇잎 사이로, 깊은 쉼의 기류.” | 부드럽고 잔잔한 수면 보조용 |
| 8 | 도시 석양 바람 | “석양처럼 차분하게, 오늘을 마무리.” | 저녁의 잔잔한 대류, 집중·정리 시간용 |
| 9 | 열대 소나기 바람 | “예측 불가능한 생동감, 폭풍우의 전율.” | 드라마틱한 난류, 몰입형 게이밍용 |
| 10 | 사막의 밤 바람 | “별빛 아래 정적, 고독한 몰입.” | 극도로 건조하고 고요한 야간 집중용 |

---

## 🎬 데모 (Demo)

![WindScape configuration]
![WindScape dashboard]
![External sensor example]
![Breezer9000]

---

## 🔧 제작 가이드 (Build Guides)

선택할 빌드는 사용하는 팬의 전압에 따라 다릅니다.  
일반적으로 **5 V 팬**은 가장 간단하며, USB A 3.0 / USB-C / 보조 배터리로 바로 작동합니다.

### 💡 하드웨어 구성

* **ESP32 개발보드** – Wi-Fi 기능 내장 (약 ₩4,000 수준)  
* **PWM 지원 팬** – 5 V 또는 12 V PC 케이스 팬

---

### 🔌 팬 선택 팁

특정 모델이 필수는 아니지만, **PWM 속도 제어**가 가능한 팬을 선택해야 합니다.  
조용하고 부드럽게 작동하는 **Noctua NF-A12x25 5 V** 모델이 추천됩니다.  
> 🔈 참고: 데시벨(dB)은 로그 단위이므로, 작은 수치 차이도 큰 소음 차이를 만듭니다.

---

### ⚙ 5 V DIY (USB 전원형 간단 구성)

<details>
<summary>부품 목록 및 배선도</summary>

| 구성품 | 예상 가격 (€) |
|--------|---------------|
| Noctua NF-A12x25 5 V 팬 | 33.00 |
| ESP32 보드 | 3.00 |
| 나사·부속품 | 4.00 |
| 3D 프린트 마운트 | 5.00 |
| **총 예상 비용** | **45.00 €** |

#### 🧩 배선 예시


<details>
<summary>Wiring Diagram</summary>

```
+----------------------------+
|     USB Power (5V)         |
|                            |
|   +5V ─────────────┐       |
|                    ▼       |
|           +----------------------+
|           |   ESP32 WROOM Board  |
|           |  (AliExpress-style)  |
|           |                      |
|           | 5V/VIN ◄─── 5V from USB
|           | GND    ◄─── GND from USB
|           |                      |
|           | GPIO14 ───┐          |
|           |           └────► PWM (Fan Pin 4, Blue)
|           |                      |
|           | GPIO27 ◄──┬────── TACH (Fan Pin 3, Green)
|           |           │
|           |   [10kΩ pull-up to 3.3V]
|           |           │
|           |     [3.3kΩ] in series
|           |           │
|           |   [0.1nF cap to GND] ◄──(RC filter)
|           +----------------------+
|                     │
|                     ▼
|           +----------------------+
|           |     Noctua Fan       |
|           |    5V PWM (4-pin)    |
|           |                      |
|           | Pin 1 (Black): GND ◄────── GND
|           | Pin 2 (Red or Yellow):  +5V ◄─── 5V/VIN
|           | Pin 3 (Green): TACH ───► GPIO27 (filtered)
|           | Pin 4 (Blue):   PWM ◄─── GPIO14
|           +----------------------+
```

</details>

</details>


---

## 💻 소프트웨어 설정 (Software Setup)

1. 제공된 **WindScape ESPHome YAML**을 ESP32에 플래시합니다.  
2. `secrets.yaml`에 Wi-Fi 정보를 입력합니다.  
3. 재부팅 후, 자동으로 네트워크에서 기기를 인식합니다.

---

## 🌈 프리셋 라이브러리 (Preset Library)

| # | 프리셋 이름 | 기본 풍속 범위 | 돌풍 스타일 | 특징 / 분위기 |
|---|--------------|----------------|--------------|----------------|
| 1 | **Ocean (대서양 바다)** | 8–16 mph | 완만한 파도형 | 활력 넘치는 지속 바람 |
| 2 | **Mediterranean (지중해)** | 4–10 mph | 부드럽고 따뜻함 | 휴식과 독서에 적합 |
| 3 | **Countryside (시골 들판)** | 2–8 mph | 드물고 잔잔함 | 수면용 바람 |
| 4 | **Mountains (알프스 산맥)** | 6–18 mph | 날카롭고 맑음 | 상쾌한 산바람 |
| 5 | **Plains (평원)** | 10–22 mph | 강하고 지속적 | 더운 날에 적합 |
| 6 | **Fjord (피오르드)** | 8–20 mph | 좁은 통로형 | 극적이고 시원한 바람 |
| 7 | **Rainy (비 오는 날)** | 3–9 mph | 간헐적 변화 | 잔잔하고 감성적 분위기 |
| 8 | **Manual (수동)** | — | — | 사용자가 직접 조절 |

</details>

---

## 🧭 문제 해결 (Troubleshooting)

| 증상 | 확인 사항 |
|------|-----------|
| **팬이 돌지 않음** | 전원(5V/12V) 연결 및 PWM 핀 설정 확인 |
| **RPM 값이 안 잡힘** | Tach 배선 및 풀업 저항 확인 |
| **바람이 변하지 않음** | *동적 모드 활성화* 및 *변동성 조정* 확인 |
| **너무 세게 불음** | 최대 출력 값 또는 프리셋 변경 |
| **너무 조용함** | 풍속 강도 슬라이더 상향 조정 |

---

## 💡 최상의 사용 팁 (Tips for Best Experience)

* **처음에는 기본값으로 시작하세요.** – 기본 프리셋은 이미 자연스러움을 기준으로 조정되어 있습니다.  
* **조절은 천천히.** 작은 값 변경도 체감상 큰 차이를 만듭니다.  
* **활동에 맞는 프리셋을 선택하세요.** – 수면 모드, 독서 모드, 집중 모드 등.

---

## 🧠 기술 노트 (Technical Notes)

Smart Nature Wind는 실제 대기 모델을 바탕으로 한 물리 시뮬레이션을 사용합니다.  

* **난류 모델링** – 자연스러운 미세 변화를 만드는 바람의 리듬 재현  
* **열기포 효과** – 상승 기류를 모사해 리얼한 공기 움직임 생성  
* **기상 패턴 전환** – 조용함 → 보통 → 강풍으로 자연스럽게 변화  
* **스마트 돌풍 알고리즘** – 돌풍이 점차 강해졌다 사라지는 리얼한 패턴  
* **관성 기반 변화** – 갑작스러운 속도 변화 없이 부드럽게 이동  

---

## 🚀 로드맵 (Roadmap)

* **Moodist 환경 엔진과 연동 예정**  
  – 스케줄 개인화 기능
    - 지정한 요일의 지정한 시간에만 작동 (복수 시간대 설정)
    - 작동 규칙 개인화 기능
      - 10분 자연풍(산들바람), 3븐 Off
      - 10분 100%세기, 10분 70% 세기, 10분 50% 세기...
  - 움직임 감지 또는 주위 존재 감지 작동
    - 움직임 감지 시 설정된 시간동안 작동
    - 등록된 스마트폰이 근처에 있을 경우 작동
   
    

---

## 🌪 WindScape 작동 원리 (How WindScape Works)

1. **미세 난류** – 초당 여러 주파수의 변화를 혼합하여 잎사귀가 흔들리는 듯한 바람을 생성  
2. **열기포 효과** – 8~15초 주기로 따뜻한 상승 기류 재현  
3. **기상 패턴 변화** – 1.5~5분 주기로 잔잔 → 보통 → 강풍으로 순환  
4. **부드러운 가속/감속** – 물리적 관성을 이용해 자연스러운 전환  
5. **실시간 센서 연동** – 온도 또는 외부 데이터에 맞춰 즉시 반응  

---

## 🔗 링크 (Links)

* **Home Assistant** – [https://www.home-assistant.io/](https://www.home-assistant.io/)  
* **ESPHome** – [https://esphome.io/](https://esphome.io/)  
* **Noctua Fans & Accessories** – [https://noctua.at/](https://noctua.at/)  
  * https://noctua.at/en/nf-a12x25-pwm  
  * https://noctua.at/en/nv-aa1-12  
  * Noctua NV-FS1 리뷰 – [YouTube 영상](https://youtu.be/9PvWBuDTGDo)  
* **3D 프린트 마운트 STL 파일**  
  * https://www.printables.com/model/554226-120mm-computer-fan-desk-mount  
  * https://www.printables.com/model/1324299-pc-desk-fan  
  * https://www.printables.com/model/889331-noctua-inspired-desk-fan-mount  

---

*© 2025 Smart Nature Wind (WindScape). 특별히 명시되지 않은 경우 MIT 라이선스 적용.*

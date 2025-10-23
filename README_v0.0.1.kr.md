![Smart Nature Wind Title](https://github.com/TilmanGriesel/WindScape/blob/main/docs/title.png?raw=true)

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

## 🎬 데모 (Demo)

![WindScape configuration](https://github.com/TilmanGriesel/WindScape/blob/main/docs/windscape_demo_01.gif?raw=true)
![WindScape dashboard](https://github.com/TilmanGriesel/WindScape/blob/main/docs/windscape_demo_02.gif?raw=true)
![External sensor example](https://github.com/TilmanGriesel/WindScape/blob/main/docs/ha_iracing_01.png?raw=true)
![Breezer9000](https://raw.githubusercontent.com/TilmanGriesel/WindScape/843b6eca3a42019fdb35a68ddca5e0dcae5bd2b5/docs/title.png?raw=true)

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

| 구성품 | 예상 가격 (€) |
|--------|---------------|
| Noctua NF-A12x25 5 V 팬 | 33.00 |
| 일반 ESP32 보드 | 3.00 |
| 나사·부속품 | 4.00 |
| 3D 프린트 마운트 | 5.00 |
| **총 예상 비용** | **45.00 €** |

---

### 🪛 배선 예시

![WindScape Title](https://github.com/TilmanGriesel/WindScape/blob/main/docs/title.png?raw=true)

# 스마트 자연 바람 (WindScape)

* **소스**
  https://github.com/TilmanGriesel/WindScape.git

*일상적인 PWM 제어 PC 팬을 놀라울 만큼 자연스러운 실내 바람 시뮬레이터로 바꿔주는 Home Assistant / ESPHome 프로젝트입니다.*

WindScape는 잔잔한 지중해의 속삭임부터 상쾌한 알프스 산맥의 돌풍에 이르기까지, 위치에서 영감을 받은 실제와 같은 공기 흐름을 제공하여 작업 공간, 게이밍 설정, 또는 심 레이싱 콕핏을 위한 자연스럽고 몰입감 있는 분위기를 조성합니다. 이는 당신이 생각해 낼 수 있는 모든 프로젝트를 향상시킬 수 있는 다재다능한 바람 시뮬레이터입니다.

![noctua_nv_fs1_1](https://noctua.at/pub/media/catalog/product/cache/74c1057f7991b4edb2bc7bdaa94de933/n/o/noctua_nv_fs1_5.jpg)

## 목차 (Table of Contents)

1. [기능 (Features)](#features)
1. [작동 모드 (Operating Modes)](#operating-modes)
1. [데모 (Demo)](#demo)
1. [제작 가이드 (Build Guides)](#build-guides)
1. [소프트웨어 설정 (Software Setup)](#software-setup)
1. [프리셋 라이브러리 (Preset Library)](#preset-library)
1. [문제 해결 (Troubleshooting)](#troubleshooting)
1. [최상의 경험을 위한 팁 (Tips for Best Experience)](#tips-for-best-experience)
1. [기술 노트 (Technical Notes)](#technical-notes)
1. [로드맵 (Roadmap)](#roadmap)
1. [WindScape 작동 원리 (How WindScape Works)](#how-windscape-works)
1. [링크 (Links)](#links)

---

## 기능 (Features)

* **현실적인 바람 물리 (Realistic Wind Physics)**
  자연스러운 느낌의 바람 패턴을 생성하는 스마트 난류 모델링 및 열 효과를 통해 실제 야외에서 경험하는 것과 같은 바람을 만듭니다.

* **자연에서 영감을 받은 환경 프리셋 (Nature-Inspired Environment Presets)**
  잔잔한 시골 바람부터 강력한 바다 바람까지, 다양한 야외 장소의 느낌을 포착한 5가지 신중하게 제작된 프로파일.

* **정적 또는 동적 모드 (Steady or Dynamic Modes)**
  일정한 공기 흐름 또는 선택한 환경에 자연스럽게 반응하며 끊임없이 변화하는 바람 패턴 중에서 선택하세요.

* **외부 센서 지원 (External Sensor Support)**
  실시간 데이터 피드(예: 레이싱 시뮬레이션 속도)를 연결하면, WindScape가 실시간으로 공기 흐름을 자동 조정하며, 신호가 60초간 끊기면 스마트 폴백(fallback) 기능을 제공합니다.

* **Home Assistant 통합 (Home Assistant Integration)**
  Wi-Fi를 통해 전체 제어, 모니터링 및 자동화가 가능합니다.

* **포괄적인 실시간 모니터링 (Comprehensive Real-Time Monitoring)**
  시뮬레이션 엔진에 대한 깊은 통찰력 – 날씨 단계, 열 이벤트, 난류 수준, 목표 추적 및 모든 물리 계산이 센서로 노출됩니다.

* **OTA 펌웨어 업데이트 (OTA Firmware Updates)**
  ESPHome을 기반으로 구축되어 원클릭 무선 업그레이드가 가능합니다.

---

## 작동 모드 (Operating Modes)

| 모드 | 설명 |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| **Oscillating (바람 시뮬레이션)** | 자연스럽고 끊임없이 변화하는 공기 흐름 패턴입니다. |
| **Steady (일정한 산들바람)** | 속도가 고정되어 있으며 변동성이 없습니다. |
| **External Sensor (외부 센서)** | Home Assistant 센서(예: 자동차 속도)를 실시간 팬 속도에 매핑합니다. 60초 동안 신호가 없을 시 자동 폴백됩니다. |

> Home Assistant의 **전역 전원 제어 (Global Power Control)**는 모든 모드를 재정의(override)합니다.

---

## 데모 (Demo)

![WindScape configuration](https://github.com/TilmanGriesel/WindScape/blob/main/docs/windscape_demo_01.gif?raw=true)
![WindScape dashboard](https://github.com/TilmanGriesel/WindScape/blob/main/docs/windscape_demo_02.gif?raw=true)
![External sensor example](https://github.com/TilmanGriesel/WindScape/blob/main/docs/ha_iracing_01.png?raw=true)
![Breezer9000](https://raw.githubusercontent.com/TilmanGriesel/WindScape/843b6eca3a42019fdb35a68ddca5e0dcae5bd2b5/docs/title.png?raw=true)

---

## 제작 가이드 (Build Guides)

선택하는 제작 방식은 프로젝트와 연결할 팬에 따라 달라집니다. 일반적으로 5V 팬은 USB-A 3.0 또는 USB-C 포트, 보조 배터리 또는 저전력 벽면 어댑터에서 전원을 공급받을 수 있어 더 간단합니다.

### 하드웨어 (Hardware)

* **마이크로컨트롤러 (Microcontroller)** – Wi-Fi가 내장된 모든 ESP32 개발 보드(약 €3의 저렴한 가격으로 널리 사용 가능).
* **팬 (Fan)** – PWM(Pulse-Width Modulation)을 지원하는 모든 5V PC 케이스 팬.

### 팬 선택 (Fan Selection)

단일 "필수" 팬은 없으며, PWM 속도 제어를 지원하는지 확인하십시오. 훌륭한 프리미엄 옵션은 **Noctua NF‑A12x25 5V**로, 매우 조용하며 5%만큼 낮은 듀티 사이클에서도 작동할 수 있습니다.

소음 수준을 비교할 때, 데시벨(dB) 스케일은 로그 스케일임을 기억하십시오. 숫자의 작은 변화도 청각적으로는 상당한 차이를 의미할 수 있습니다.

### 5V 팬 (DIY USB 전원 & 단순)

<details>
<summary>부품 명세서(Bill of Materials) 및 배선</summary>

| 품목 | 5V DIY 제작 비용 (€) |
| ------------------------------- | ------------------ |
| Noctua NF-A12x25 5V             | 33.00              |
| 일반 ESP32                      | 3.00               |
| 일반 하드웨어(나사, 너트)         | 4.00               |
| 3D 프린팅 마운트 및 하드웨어       | 5.00               |
| **총 예상 비용** | **45.00** |

<details>
<summary>배선 다이어그램 (Wiring Diagram)</summary>




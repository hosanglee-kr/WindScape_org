{
  "nvsSpec": {
    "version": "025",
    "jsonFile": "cfg_nvsSpec_025.json",
    "namespace": "SNW",
    "entries": [
      { "key": "runMode", "type": "uint8", "default": 0, "desc": "0=Idle,1=Schedule,2=Profile,3=Override" },
      { "key": "activeProfileNo", "type": "uint8", "default": 0, "desc": "현재 프로파일 번호" },
      { "key": "activeSegmentNo", "type": "uint8", "default": 0, "desc": "현재 세그먼트 번호" },
      { "key": "presetCode", "type": "string", "default": "", "desc": "마지막 프리셋 코드" },
      { "key": "styleCode", "type": "string", "default": "", "desc": "마지막 스타일 코드" },
      { "key": "overrideActive", "type": "bool", "default": false, "desc": "오버라이드 활성 여부" },
      { "key": "overrideRemain", "type": "uint32", "default": 0, "desc": "남은 오버라이드 시간(초)" },
      { "key": "wifiConnected", "type": "bool", "default": false, "desc": "Wi-Fi 연결 상태" }
    ]
  }
}

// WF10_WiFiManager_023.cpp 

#include "WF10_WiFiManager_023.h"

// --------------------------------------------------
// Static Members Definition (단 하나의 .cpp 파일에만 정의)
// --------------------------------------------------
bool		CL_WF10_WiFiManager::s_staConnected		 = false;
wl_status_t CL_WF10_WiFiManager::s_lastStaStatus	 = WL_IDLE_STATUS;
bool		CL_WF10_WiFiManager::s_timeSynced		 = false;
uint32_t	CL_WF10_WiFiManager::s_lastSyncMs		 = 0;
uint8_t		CL_WF10_WiFiManager::s_reconnectAttempts = 0;

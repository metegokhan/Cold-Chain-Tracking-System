#pragma once
#include <Arduino.h>

struct __attribute__((packed)) TempRecord {
  uint32_t timestamp; // Unix epoch seconds (NTP time)
  int16_t temp;       // Temperature * 10 (e.g. 4.5 C -> 45)
};

// Rolling buffer sized for 4 full weeks (28 days @ 5-min intervals = 8,064 samples)
// Packed TempRecord uses only 6 bytes per sample (48.3 KB total SRAM).
const int HISTORY_SIZE = 8064; 
extern TempRecord tempHistory[HISTORY_SIZE];
extern int historyHead;
extern int historyCount;
extern void saveHistoryToFS();
extern void loadHistoryFromFS();

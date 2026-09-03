#pragma once
#include <Arduino.h>

struct TempRecord {
  uint32_t timestamp; // Unix epoch seconds (NTP time)
  int16_t temp;       // Temperature * 10 (e.g. 4.5 C -> 45)
};

const int HISTORY_SIZE = 8640; // 30 days * 24 hours * 12 samples/hour (every 5 mins)
extern TempRecord tempHistory[HISTORY_SIZE];
extern int historyHead;
extern int historyCount;
extern void saveHistoryToFS();
extern void loadHistoryFromFS();

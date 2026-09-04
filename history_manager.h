#pragma once
#include <Arduino.h>

struct __attribute__((packed)) TempRecord {
  uint32_t timestamp; // Unix epoch seconds (NTP time)
  int16_t temp;       // Temperature * 10 (e.g. 4.5 C -> 45)
};

// Rolling buffer sized for ESP32-C3 internal SRAM constraints (2880 samples = 10 full days @ 5-min intervals)
// Saves ~52 KB of critical SRAM to allow TLS/HTTPS (mbedTLS) to operate without memory exhaustion.
const int HISTORY_SIZE = 2880; 
extern TempRecord tempHistory[HISTORY_SIZE];
extern int historyHead;
extern int historyCount;
extern void saveHistoryToFS();
extern void loadHistoryFromFS();

#pragma once

#include <cstdint>

struct CanFrame {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
  uint32_t timestampMs;
  bool extended;
};

bool canTwaiBegin();
void canTwaiTask();
bool canTwaiReceive(CanFrame& out);
bool canTwaiSend(const CanFrame& frame);
uint32_t canTwaiRxCount();
uint32_t canTwaiTxCount();
uint32_t canTwaiErrorCount();
bool canTwaiIsListenOnly();

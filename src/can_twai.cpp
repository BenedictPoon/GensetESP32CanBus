#include "can_twai.h"

#include <Arduino.h>
#include <cstring>

#include <driver/twai.h>

#include "board_pins.h"
#include "config.h"

namespace {

uint32_t gRxCount = 0;
uint32_t gTxCount = 0;
uint32_t gErrCount = 0;
bool gListenOnly = false;

}  // namespace

bool canTwaiBegin() {
  gListenOnly = (CAN_LISTEN_ONLY != 0);

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(PIN_CAN_TX),
      static_cast<gpio_num_t>(PIN_CAN_RX),
      TWAI_MODE_NORMAL);

  if (gListenOnly) {
    g_config.mode = TWAI_MODE_LISTEN_ONLY;
  }

  // Official 50 kbit/s timing (matches HIMOINSA CEx7 / CCMODBUS)
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_50KBITS();

  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
  if (err != ESP_OK) {
    Serial.printf("[CAN] twai_driver_install failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = twai_start();
  if (err != ESP_OK) {
    Serial.printf("[CAN] twai_start failed: %s\n", esp_err_to_name(err));
    twai_driver_uninstall();
    return false;
  }

  Serial.printf("[CAN] TWAI started @ %lu bit/s TX=%d RX=%d listen_only=%d\n",
                static_cast<unsigned long>(kCanBitrate), PIN_CAN_TX, PIN_CAN_RX,
                gListenOnly ? 1 : 0);
  return true;
}

void canTwaiTask() {
  // Alerts / bus recovery hooks can go here later
}

bool canTwaiReceive(CanFrame& out) {
  twai_message_t msg;
  const esp_err_t err = twai_receive(&msg, 0);
  if (err != ESP_OK) {
    return false;
  }

  out.id = msg.identifier;
  out.extended = msg.extd;
  out.dlc = msg.data_length_code;
  if (out.dlc > 8) {
    out.dlc = 8;
  }
  memcpy(out.data, msg.data, out.dlc);
  out.timestampMs = millis();
  ++gRxCount;
  return true;
}

bool canTwaiSend(const CanFrame& frame) {
  if (gListenOnly) {
    return false;
  }

  twai_message_t msg = {};
  msg.identifier = frame.id;
  msg.extd = frame.extended;
  msg.rtr = false;
  msg.data_length_code = frame.dlc > 8 ? 8 : frame.dlc;
  memcpy(msg.data, frame.data, msg.data_length_code);

  const esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(20));
  if (err != ESP_OK) {
    ++gErrCount;
    return false;
  }
  ++gTxCount;
  return true;
}

uint32_t canTwaiRxCount() { return gRxCount; }
uint32_t canTwaiTxCount() { return gTxCount; }
uint32_t canTwaiErrorCount() { return gErrCount; }
bool canTwaiIsListenOnly() { return gListenOnly; }

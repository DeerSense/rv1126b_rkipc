#ifndef LORA_H
#define LORA_H

#include <string>
#include <vector>
#include <memory>
#include <termios.h>
#include "SerialPort.h"
#include "rk_gpio.h"

namespace Lora {

class LoraModule {
public:
  enum Mode { CONFIG = 0, NORMAL = 1, SLEEP = 2 };

  LoraModule(const std::string &port = "/dev/ttyS3", speed_t baud = B9600,
             int timeout_ms = 100);
  ~LoraModule();

  bool init();
  bool setMode(Mode m);
  Mode getMode() const { return mode_; }

  ssize_t send(const std::vector<uint8_t> &data);
  ssize_t sendString(const std::string &s);

private:
  bool setup_gpio();
  bool apply_gpio();

  std::unique_ptr<Serial::SerialPort> sp_;
  std::string port_;
  speed_t baud_;
  Mode mode_;
  int timeout_ms_;

  const uint32_t md0_gpio_ = 25; // MD0 -> gpio25
  const uint32_t md1_gpio_ = 24; // MD1 -> gpio24
};

} // namespace Lora

#endif // LORA_H

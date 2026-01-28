#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "lora.cc"

#include "lora.h"
#include "log.h"
#include <chrono>
#include <thread>
#include <unistd.h>

using namespace Lora;

LoraModule::LoraModule(const std::string &port, speed_t baud, int timeout_ms)
		: port_(port), baud_(baud), mode_(CONFIG), timeout_ms_(timeout_ms) {}

LoraModule::~LoraModule() {
	// put device into sleep to be safe
	setMode(SLEEP);
}

bool LoraModule::setup_gpio() {
	if (rk_gpio_export_direction(md0_gpio_, GPIO_DIRECTION_OUTPUT) < 0) {
		LOG_WARN("rk_gpio_export_direction md0(%d) failed\n", md0_gpio_);
		// continue, maybe already exported
	}
	if (rk_gpio_export_direction(md1_gpio_, GPIO_DIRECTION_OUTPUT) < 0) {
		LOG_WARN("rk_gpio_export_direction md1(%d) failed\n", md1_gpio_);
	}
	return true;
}

bool LoraModule::apply_gpio() {
	int ret = 0;
	switch (mode_) {
	case CONFIG:
		// MD0=0, MD1=0
		ret |= rk_gpio_set_value(md0_gpio_, 0);
		ret |= rk_gpio_set_value(md1_gpio_, 0);
		break;
	case NORMAL:
		// MD0=1, MD1=0
		ret |= rk_gpio_set_value(md0_gpio_, 1);
		ret |= rk_gpio_set_value(md1_gpio_, 0);
		break;
	case SLEEP:
		// MD0=1, MD1=1
		ret |= rk_gpio_set_value(md0_gpio_, 1);
		ret |= rk_gpio_set_value(md1_gpio_, 1);
		break;
	}
	// allow a short settling time
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	if (ret <= 0) {
		LOG_WARN("apply_gpio returned %d\n", ret);
		return false;
	}
	LOG_INFO("Applied Lora mode %d (MD0=%d MD1=%d) \n", mode_,
					 rk_gpio_get_value(md0_gpio_), rk_gpio_get_value(md1_gpio_));
	return true;
}

bool LoraModule::init() {
	setup_gpio();
	// create serial port
	sp_.reset(new Serial::SerialPort(port_, baud_, timeout_ms_));
	if (!sp_ || !sp_->is_Open()) {
		LOG_WARN("Serial port %s open failed", port_.c_str());
		return false;
	}
	// default to NORMAL working mode after init
	setMode(NORMAL);
	return true;
}

bool LoraModule::setMode(Mode m) {
	mode_ = m;
	return apply_gpio();
}

ssize_t LoraModule::send(const std::vector<uint8_t> &data) {
	if (!sp_ || !sp_->is_Open()) {
		LOG_WARN("serial port not open");
		return -1;
	}
	return sp_->send(data.data(), data.size());
}

ssize_t LoraModule::sendString(const std::string &s) {
	std::vector<uint8_t> buf(s.begin(), s.end());
	return send(buf);
}


#include "outputs/DrsOutput.hpp"

namespace outputs {

DrsOutput::DrsOutput(hal::IPwmOutput& pwm, DrsConfig config) : pwm_(pwm), config_(config) {}

void DrsOutput::setOpen(bool open) {
    pwm_.setPulseMicroseconds(open ? config_.openMicros : config_.closedMicros);
}

} // namespace outputs

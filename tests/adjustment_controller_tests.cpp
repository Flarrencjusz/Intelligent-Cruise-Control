// SPDX-License-Identifier: GPL-3.0-only
#include <cstdlib>
#include <iostream>

#include "../src/adjustment_controller.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void baseline_does_not_adjust() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 90.0F / 3.6F);
    require(!controller.adjusting(), "initial limit must only establish a baseline");
}

void changed_limit_adjusts_active_cruise() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 90.0F / 3.6F);
    controller.observe_speed_limit(60.0F / 3.6F, 90.0F / 3.6F);
    require(controller.adjusting(), "changed limit should schedule adjustment");
    require(controller.next_pulse(90.0F / 3.6F) == -1, "lower limit should decrease cruise");
}

void disabled_controller_never_adjusts() {
    icc::AdjustmentController controller(false);
    controller.observe_speed_limit(80.0F / 3.6F, 90.0F / 3.6F);
    controller.observe_speed_limit(60.0F / 3.6F, 90.0F / 3.6F);
    require(!controller.adjusting(), "disabled controller must ignore limit changes");
}

void inactive_cruise_is_ignored() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 0.0F);
    controller.observe_speed_limit(60.0F / 3.6F, 0.0F);
    require(!controller.adjusting(), "inactive cruise must not be enabled by the plugin");
}

void invalid_limit_is_ignored() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 90.0F / 3.6F);
    controller.observe_speed_limit(0.0F, 90.0F / 3.6F);
    require(!controller.adjusting(), "no-limit telemetry must not create a target");
}

void reaching_target_stops_pulses() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 90.0F / 3.6F);
    controller.observe_speed_limit(60.0F / 3.6F, 90.0F / 3.6F);
    require(controller.next_pulse(60.0F / 3.6F) == 0, "matching cruise needs no pulse");
    require(!controller.adjusting(), "matching cruise should clear the target");
}

void waits_for_telemetry_before_clicking_again() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 80.0F / 3.6F);
    controller.observe_speed_limit(50.0F / 3.6F, 80.0F / 3.6F);

    require(controller.next_pulse(80.0F / 3.6F) == -1, "first decrease should be emitted");
    require(controller.next_pulse(80.0F / 3.6F) == 0, "a release frame is required");
    require(controller.next_pulse(80.0F / 3.6F) == 0, "stale telemetry must not repeat a click");
}

void exact_target_does_not_overshoot() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 80.0F / 3.6F);
    controller.observe_speed_limit(50.0F / 3.6F, 80.0F / 3.6F);

    float cruise_kph = 80.0F;
    int clicks = 0;
    for (int frame = 0; frame < 200 && controller.adjusting(); ++frame) {
        const int pulse = controller.next_pulse(cruise_kph / 3.6F);
        if (pulse != 0) {
            cruise_kph += static_cast<float>(pulse * 2);
            ++clicks;
        }
    }

    require(cruise_kph == 50.0F, "80 to 50 with a 2 km/h step must stop at 50");
    require(clicks == 15, "80 to 50 with a 2 km/h step requires exactly 15 clicks");
    require(!controller.adjusting(), "exact target should finish adjustment");
}

void unreachable_odd_limit_settles_without_oscillation() {
    icc::AdjustmentController controller;
    controller.observe_speed_limit(80.0F / 3.6F, 80.0F / 3.6F);
    controller.observe_speed_limit(85.0F / 3.6F, 80.0F / 3.6F);

    float cruise_kph = 80.0F;
    int clicks = 0;
    for (int frame = 0; frame < 100 && controller.adjusting(); ++frame) {
        const int pulse = controller.next_pulse(cruise_kph / 3.6F);
        if (pulse != 0) {
            cruise_kph += static_cast<float>(pulse * 2);
            ++clicks;
        }
    }

    require(cruise_kph == 84.0F, "85 with a 2 km/h step should settle at non-speeding 84");
    require(clicks == 2, "unreachable target must not alternate between 84 and 86");
    require(!controller.adjusting(), "unreachable target should finish adjustment");
}

}  // namespace

int main() {
    baseline_does_not_adjust();
    changed_limit_adjusts_active_cruise();
    disabled_controller_never_adjusts();
    inactive_cruise_is_ignored();
    invalid_limit_is_ignored();
    reaching_target_stops_pulses();
    waits_for_telemetry_before_clicking_again();
    exact_target_does_not_overshoot();
    unreachable_odd_limit_settles_without_oscillation();
    std::cout << "All adjustment controller tests passed.\n";
    return 0;
}

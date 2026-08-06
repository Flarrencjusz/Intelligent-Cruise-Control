// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cmath>
#include <optional>

namespace icc {

// Pure, game-independent state machine used by the plugin and unit tests.
// Speeds use the SCS SDK's native metres-per-second unit.
class AdjustmentController {
public:
    static constexpr float kValidLimitMinimumMps = 0.5F;
    static constexpr float kCruiseActiveMinimumMps = 0.5F;
    static constexpr float kMatchToleranceMps = 0.12F;
    // A click is not repeated until telemetry confirms that ETS2 applied the
    // previous click. This prevents stale telemetry from producing extra clicks.
    static constexpr float kTelemetryChangeToleranceMps = 0.02F;
    static constexpr int kAcknowledgementTimeoutFrames = 60;
    static constexpr int kReleaseFrames = 1;

    explicit AdjustmentController(bool enabled = true) : enabled_(enabled) {}

    void set_enabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) {
            cancel();
        }
    }

    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] bool adjusting() const { return target_mps_.has_value(); }
    [[nodiscard]] std::optional<float> target_mps() const { return target_mps_; }

    // The first valid value establishes a baseline. Adjustment happens only when
    // the game subsequently reports a different speed limit.
    void observe_speed_limit(float limit_mps, float cruise_mps) {
        const bool valid = std::isfinite(limit_mps) && limit_mps >= kValidLimitMinimumMps;
        const float normalized = valid ? limit_mps : 0.0F;

        if (!has_limit_baseline_) {
            has_limit_baseline_ = true;
            last_limit_mps_ = normalized;
            return;
        }

        if (std::fabs(normalized - last_limit_mps_) <= kMatchToleranceMps) {
            return;
        }

        last_limit_mps_ = normalized;
        reset_pending_click();

        // A zero value means that the Route Advisor currently has no usable
        // limit. Never disable cruise control or invent a target in that case.
        if (enabled_ && valid && cruise_mps >= kCruiseActiveMinimumMps) {
            target_mps_ = limit_mps;
        } else {
            target_mps_.reset();
        }
    }

    // Returns +1 for cruise increase, -1 for decrease, or 0 for no button pulse.
    int next_pulse(float cruise_mps) {
        if (!enabled_ || !target_mps_.has_value()) {
            return 0;
        }

        if (!std::isfinite(cruise_mps) || cruise_mps < kCruiseActiveMinimumMps) {
            target_mps_.reset();
            return 0;
        }

        // Always report at least one false frame after a click so ETS2 sees a
        // fresh leading edge for the next semantic button press.
        if (release_frames_ > 0) {
            --release_frames_;
            return 0;
        }

        if (awaiting_acknowledgement_) {
            const float applied_change = std::fabs(cruise_mps - pulse_origin_mps_);
            if (applied_change > kTelemetryChangeToleranceMps) {
                observed_step_mps_ = applied_change;
                awaiting_acknowledgement_ = false;
                acknowledgement_frames_ = 0;

                if (stop_after_acknowledgement_) {
                    target_mps_.reset();
                    stop_after_acknowledgement_ = false;
                    return 0;
                }
            } else if (++acknowledgement_frames_ <= kAcknowledgementTimeoutFrames) {
                return 0;
            } else {
                // ETS2 did not accept the click. Permit a single retry after the
                // timeout instead of flooding the input mix every few frames.
                awaiting_acknowledgement_ = false;
                acknowledgement_frames_ = 0;
            }
        }

        const float delta = *target_mps_ - cruise_mps;
        if (std::fabs(delta) <= kMatchToleranceMps) {
            target_mps_.reset();
            return 0;
        }

        if (observed_step_mps_.has_value() &&
            std::fabs(delta) < *observed_step_mps_ - kMatchToleranceMps) {
            if (delta > 0.0F) {
                // The next increase would exceed an unreachable limit. Keep the
                // closest non-speeding setpoint and finish without oscillation.
                target_mps_.reset();
                return 0;
            }

            const float predicted = cruise_mps - *observed_step_mps_;
            const float predicted_distance = std::fabs(*target_mps_ - predicted);
            if (predicted_distance <= std::fabs(delta) + kMatchToleranceMps) {
                // One final decrease produces an equally close or closer value
                // below the limit. Stop as soon as telemetry acknowledges it.
                return emit_pulse(-1, cruise_mps, true);
            }

            target_mps_.reset();
            return 0;
        }

        return emit_pulse(delta > 0.0F ? 1 : -1, cruise_mps, false);
    }

    void cancel() {
        target_mps_.reset();
        reset_pending_click();
    }

private:
    int emit_pulse(int direction, float cruise_mps, bool stop_after_acknowledgement) {
        awaiting_acknowledgement_ = true;
        pulse_origin_mps_ = cruise_mps;
        acknowledgement_frames_ = 0;
        release_frames_ = kReleaseFrames;
        stop_after_acknowledgement_ = stop_after_acknowledgement;
        return direction;
    }

    void reset_pending_click() {
        awaiting_acknowledgement_ = false;
        acknowledgement_frames_ = 0;
        release_frames_ = 0;
        stop_after_acknowledgement_ = false;
    }

    bool enabled_ = true;
    bool has_limit_baseline_ = false;
    float last_limit_mps_ = 0.0F;
    std::optional<float> target_mps_;
    std::optional<float> observed_step_mps_;
    bool awaiting_acknowledgement_ = false;
    bool stop_after_acknowledgement_ = false;
    float pulse_origin_mps_ = 0.0F;
    int acknowledgement_frames_ = 0;
    int release_frames_ = 0;
};

}  // namespace icc

#include "record_runtime/hmi_controller.h"
#include <utility>

namespace DA::Ugripper {

HmiController::HmiController(HmiControllerOptions options, NowMsFn now_ms_fn)
    : options_(std::move(options)), now_ms_fn_(now_ms_fn)
{
}

bool HmiController::updateDebouncedButton(bool raw_pressed,
                                          uint64_t now_ms,
                                          uint64_t debounce_ms,
                                          DebouncedButtonState *state)
{
    if (state == nullptr)
    {
        return false;
    }

    if (raw_pressed != state->raw_pressed)
    {
        state->raw_pressed = raw_pressed;
        state->raw_changed_ms = now_ms;
    }

    if (state->stable_pressed != state->raw_pressed &&
        now_ms - state->raw_changed_ms >= debounce_ms)
    {
        state->stable_pressed = state->raw_pressed;
    }

    return state->stable_pressed;
}

std::vector<HmiEvent> HmiController::HandleButtons(const ButtonSnapshot& buttons)
{
    return HandleButtonsAt(buttons, now_ms_fn_ != nullptr ? now_ms_fn_() : 0);
}

std::vector<HmiEvent> HmiController::HandleButtonsAt(const ButtonSnapshot& buttons, uint64_t now_ms)
{
    std::vector<HmiEvent> events;
    const ButtonSnapshot stable_buttons{
        .up_pressed = updateDebouncedButton(
            buttons.up_pressed,
            now_ms,
            buttons.up_pressed ? options_.press_debounce_ms : options_.release_debounce_ms,
            &up_button_),
        .down_pressed = updateDebouncedButton(
            buttons.down_pressed,
            now_ms,
            buttons.down_pressed ? options_.press_debounce_ms : options_.release_debounce_ms,
            &down_button_),
    };

    if (stable_buttons.up_pressed && !last_buttons_.up_pressed)
    {
        tracker_.up_pressed_since_ms = now_ms;
        tracker_.up_long_handled = false;
    }
    if (stable_buttons.down_pressed && !last_buttons_.down_pressed)
    {
        tracker_.down_pressed_since_ms = now_ms;
        tracker_.down_long_handled = false;
    }

    if (!stable_buttons.up_pressed)
    {
        tracker_.up_pressed_since_ms = 0;
    }
    if (!stable_buttons.down_pressed)
    {
        tracker_.down_pressed_since_ms = 0;
    }

    if (stable_buttons.up_pressed && stable_buttons.down_pressed)
    {
        if (!tracker_.dual_chord_active)
        {
            tracker_.dual_chord_active = true;
            tracker_.both_pressed_since_ms = now_ms;
            tracker_.dual_long_handled = false;
            tracker_.shutdown_prompt_played = false;
        }

        const uint64_t dual_held_ms = now_ms - tracker_.both_pressed_since_ms;
        if (dual_held_ms >= options_.shutdown_prompt_threshold_ms && !tracker_.shutdown_prompt_played)
        {
            tracker_.shutdown_prompt_played = true;
            events.push_back({HmiEventType::ShutdownPromptRequested});
        }
        if (dual_held_ms >= options_.dual_long_press_threshold_ms && !tracker_.dual_long_handled)
        {
            tracker_.dual_long_handled = true;
            last_button_action_ms_ = now_ms;
            events.push_back({HmiEventType::ShutdownRequested});
        }

        last_buttons_ = stable_buttons;
        return events;
    }

    if (tracker_.dual_chord_active)
    {
        if (!stable_buttons.up_pressed && !stable_buttons.down_pressed)
        {
            tracker_.dual_chord_active = false;
            tracker_.both_pressed_since_ms = 0;
            tracker_.dual_long_handled = false;
            tracker_.shutdown_prompt_played = false;
        }
        last_buttons_ = stable_buttons;
        return events;
    }

    if (stable_buttons.up_pressed && !tracker_.up_long_handled && tracker_.up_pressed_since_ms > 0 &&
        (now_ms - tracker_.up_pressed_since_ms) >= (options_.up_long_press_threshold_ms ?
            options_.up_long_press_threshold_ms : options_.long_press_threshold_ms))
    {
        tracker_.up_long_handled = true;
        last_button_action_ms_ = now_ms;
        events.push_back({HmiEventType::LongUpPressed});
    }

    if (stable_buttons.down_pressed && !tracker_.down_long_handled && tracker_.down_pressed_since_ms > 0 &&
        (now_ms - tracker_.down_pressed_since_ms) >= options_.long_press_threshold_ms)
    {
        tracker_.down_long_handled = true;
        last_button_action_ms_ = now_ms;
        events.push_back({HmiEventType::LongDownPressed});
    }

    const bool up_released = last_buttons_.up_pressed && !stable_buttons.up_pressed;
    const bool down_released = last_buttons_.down_pressed && !stable_buttons.down_pressed;

    if (up_released && !tracker_.up_long_handled &&
        (now_ms - last_button_action_ms_) >= options_.action_debounce_ms)
    {
        last_button_action_ms_ = now_ms;
        events.push_back({HmiEventType::ShortUpPressed});
    }

    if (down_released && !tracker_.down_long_handled &&
        (now_ms - last_button_action_ms_) >= options_.action_debounce_ms)
    {
        last_button_action_ms_ = now_ms;
        events.push_back({HmiEventType::ShortDownPressed});
    }

    if (up_released)
    {
        tracker_.up_long_handled = false;
    }
    if (down_released)
    {
        tracker_.down_long_handled = false;
    }

    last_buttons_ = stable_buttons;
    return events;
}

void HmiController::Reset()
{
    up_button_ = {};
    down_button_ = {};
    last_buttons_ = {};
    tracker_ = {};
    last_button_action_ms_ = 0;
}

} // namespace DA::Ugripper

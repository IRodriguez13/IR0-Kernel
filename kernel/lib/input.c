/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — input facade for /dev/events0 provider.
 */

#include <ir0/input.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <config.h>
#include <string.h>

int ir0_input_is_available(void)
{
    /*
     * Keyboard IRQ path + event queue are part of baseline x86 bring-up.
     * Runtime event availability is polled separately via ir0_input_poll().
     */
    return 1;
}

int ir0_input_poll(void)
{
    return input_event_has_data() ? 1 : 0;
}

int ir0_input_read_event(struct ir0_input_event *ev)
{
    struct input_event raw;
    size_t n;

    if (!ev)
        return -1;

    n = input_event_read(&raw, 1);
    if (n == 0)
        return 0;

    ev->type = raw.type;
    ev->code = raw.code;
    ev->value = raw.value;
    ev->timestamp_ms = ((int64_t)raw.time.tv_sec * 1000LL) +
                       ((int64_t)raw.time.tv_usec / 1000LL);
    return 1;
}

void ir0_input_get_caps(struct ir0_input_caps *caps)
{
    if (!caps)
        return;

    memset(caps, 0, sizeof(*caps));
    caps->keyboard = 1;
    caps->mouse = input_mouse_is_available() ? 1 : 0;
    caps->has_events0 = 1;
#if CONFIG_INIT_PS2_CONTROLLER
    caps->keyboard_ps2 = 1;
#endif
    caps->mouse_ps2 = input_mouse_is_available() ? 1 : 0;
    caps->event_queue_depth = (int)input_event_queue_depth();
    caps->supports_key_up_down = 1;
    caps->supports_mouse_motion = input_mouse_is_available() ? 1 : 0;
}

int ir0_input_inject_event(uint16_t type, uint16_t code, int32_t value)
{
#if CONFIG_TEST_INPUT_INJECT
    input_event_push(type, code, value);
    return 0;
#else
    (void)type;
    (void)code;
    (void)value;
    return -ENOSYS;
#endif
}

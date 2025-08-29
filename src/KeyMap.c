#include "KeyMap.h"
#include "cex.h"
#include "libevdev/libevdev.h"
#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

Exception
KeyMap_create(KeyMap_c* self, char* input_dev_or_name)
{
    uassert(self->input.fd == 0 && "already initialized or non ZII");
    uassert(!self->mod_pressed && "non ZII?");

    // NOTE: Setting up virtual keyboard for output
    e$except_errno (self->output.fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) { goto err; }
    e$except_errno (ioctl(self->output.fd, UI_SET_EVBIT, EV_KEY)) { goto err; }
    e$except_errno (ioctl(self->output.fd, UI_SET_EVBIT, EV_SYN)) { goto err; }

    for (int key = 0; key < KEY_MAX; key++) {
        e$except_errno (ioctl(self->output.fd, UI_SET_KEYBIT, key)) {
            return e$raise(Error.io, "error setting virtual keycode: %d", key);
        }
    }
    struct uinput_setup usetup = { 0 };
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234; /* sample vendor */
    usetup.id.product = 0x0001;
    e$ret(str.copy(usetup.name, "UberKeyboardMappper", sizeof(usetup.name)));

    e$except_errno (ioctl(self->output.fd, UI_DEV_SETUP, &usetup)) { goto err; }
    e$except_errno (ioctl(self->output.fd, UI_DEV_CREATE)) { goto err; }

    // Attaching for input keyboard
    if (str.starts_with(input_dev_or_name, "/dev/")) {
        e$except_errno (self->input.fd = open(input_dev_or_name, O_RDONLY | O_NONBLOCK)) {
            log$error("Error opening: %s\n", input_dev_or_name);
            goto err;
        }
        e$except_errno (libevdev_new_from_fd(self->input.fd, &self->input.dev)) {
            log$error("Error opening: %s\n", input_dev_or_name);
            goto err;
        };
        bool is_qwerty = KeyMap.is_qwerty_keyboard(self->input.dev);
        if (!is_qwerty) {
            return e$raise(
                Error.argument,
                "Input device: %s is not qwerty keyboard",
                input_dev_or_name
            );
        }
        if (self->debug) {
            char* sys_kbd_name = (char*)libevdev_get_name(self->input.dev);
            printf("Keyboard FOUND\n");
            printf("Evdev version: %x\n", libevdev_get_driver_version(self->input.dev));
            printf("Input device name: \"%s\"\n", sys_kbd_name);
            printf("Phys location: %s\n", libevdev_get_phys(self->input.dev));
            printf("Uniq identifier: %s\n", libevdev_get_uniq(self->input.dev));
            printf("Is Qwetry Keyboard: %d\n", is_qwerty);
        }
    } else {
        e$ret(KeyMap.find_mapped_keyboard(self, input_dev_or_name));
    }
    if (self->debug) {}

    e$except_errno (libevdev_grab(self->input.dev, LIBEVDEV_GRAB)) { goto err; }

    // Making mouse
    if (self->mouse_key_code) {
        struct libevdev* dev = NULL;

        // Create a new evdev device
        e$except_null (dev = libevdev_new()) { goto err; }

        // Set device properties
        libevdev_set_name(dev, "UberKeyboardMappperVirtualMouse");
        libevdev_set_id_vendor(dev, 0x1234);
        libevdev_set_id_product(dev, 0x0002);
        libevdev_set_id_bustype(dev, BUS_USB);
        libevdev_set_id_version(dev, 1);

        // Enable relative axes (mouse movement)
        libevdev_enable_event_type(dev, EV_REL);
        libevdev_enable_event_code(dev, EV_REL, REL_X, NULL);
        libevdev_enable_event_code(dev, EV_REL, REL_Y, NULL);
        libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, NULL);
        libevdev_enable_event_code(dev, EV_REL, REL_HWHEEL, NULL);

        // Enable buttons
        libevdev_enable_event_type(dev, EV_KEY);
        libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, NULL);
        libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, NULL);
        libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, NULL);

        // Enable synchronization events
        libevdev_enable_event_type(dev, EV_SYN);

        // Create uinput device
        e$except_errno (
            libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &self->mouse.dev)
        ) {
            goto err;
        }

        printf(
            "Virtual mouse created successfully Device: %s\n",
            libevdev_uinput_get_devnode(self->mouse.dev)
        );

        if (self->mouse_sensitivity <= 0) {
            self->mouse_sensitivity = 1.0f;
        } else {
            uassertf(
                self->mouse_sensitivity < 10 && self->mouse_sensitivity > 0.1,
                "sensitivity expected in (0.1;10) got: %0.3f",
                self->mouse_sensitivity
            );
        }
        uassertf(
            self->mouse_speedup_ms > 0 && self->mouse_speedup_ms < 10000,
            "mouse_speedup_ms weird value: %lu",
            self->mouse_speedup_ms
        );

        libevdev_free(dev);
    }

    return EOK;

err:
    return Error.io;
}

Exception
KeyMap_find_mapped_keyboard(KeyMap_c* self, char* keyboard_name)
{
    (void)self;
    (void)keyboard_name;
    mem$scope(tmem$, _)
    {
        io.printf("Looking for keyboard: '%s'\n", keyboard_name);
        for$each (it, os.fs.find("/dev/input/event*", false, _)) {
            e$except_errno (self->input.fd = open(it, O_RDONLY | O_NONBLOCK)) {
                log$error("Error opening: %s\n", it);
                goto err;
            }
            e$except_errno (libevdev_new_from_fd(self->input.fd, &self->input.dev)) {
                log$error("Error opening: %s\n", it);
                goto err;
            };

            char* sys_kbd_name = (char*)libevdev_get_name(self->input.dev);
            bool is_qwerty = KeyMap.is_qwerty_keyboard(self->input.dev);

            printf(
                "%s: Input device name: '%s' Phys: '%s' is_qwerty: %d\n",
                it,
                sys_kbd_name,
                libevdev_get_phys(self->input.dev),
                is_qwerty
            );

            if (is_qwerty && str.eq(sys_kbd_name, keyboard_name)) {
                if (self->debug) {
                    printf("Keyboard FOUND\n");
                    printf("Evdev version: %x\n", libevdev_get_driver_version(self->input.dev));
                    printf("Input device name: \"%s\"\n", sys_kbd_name);
                    printf("Phys location: %s\n", libevdev_get_phys(self->input.dev));
                    printf("Uniq identifier: %s\n", libevdev_get_uniq(self->input.dev));
                    printf("Is Qwetry Keyboard: %d\n", is_qwerty);
                }
                return EOK;
            } else {
                if (self->input.dev) {
                    libevdev_free(self->input.dev);
                    self->input.dev = NULL;
                }
                if (self->input.fd > 0) {
                    close(self->input.fd);
                    self->input.fd = -1;
                }
            }
        }
    }

    return e$raise(Error.not_found, "No such keyboard name: '%s'", keyboard_name);
err:
    if (self->input.dev) {
        libevdev_free(self->input.dev);
        self->input.dev = NULL;
    }
    if (self->input.fd > 0) {
        close(self->input.fd);
        self->input.fd = -1;
    }
    return Error.io;
}


bool
KeyMap_is_qwerty_keyboard(struct libevdev* dev)
{
    if (!libevdev_has_event_type(dev, EV_KEY)) { return false; }
    if (!libevdev_has_event_code(dev, EV_KEY, KEY_Q)) { return false; }
    if (!libevdev_has_event_code(dev, EV_KEY, KEY_W)) { return false; }
    if (!libevdev_has_event_code(dev, EV_KEY, KEY_E)) { return false; }
    if (!libevdev_has_event_code(dev, EV_KEY, KEY_ESC)) { return false; }
    if (!libevdev_has_event_code(dev, EV_KEY, KEY_CAPSLOCK)) { return false; }

    // NOTE: some keyboards may have more than 1 inputs, with qwerty, usually input0 works
    char* phys_loc = (char*)libevdev_get_phys(dev);
    if (!str.ends_with(phys_loc, "/input0")) { return false; }

    return true;
}

static int
print_event(struct input_event* ev)
{
    if (ev->type == EV_SYN) {
        printf(
            "Event: time %ld.%06ld, ++++++++++++++++++++ %s (%d) +++++++++++++++\n",
            ev->input_event_sec,
            ev->input_event_usec,
            libevdev_event_type_get_name(ev->type),
            ev->code
        );
    } else {
        printf(
            "Event: time %ld.%06ld, type %d (%s), code %d (%s), value %d\n",
            ev->input_event_sec,
            ev->input_event_usec,
            ev->type,
            libevdev_event_type_get_name(ev->type),
            ev->code,
            libevdev_event_code_get_name(ev->type, ev->code),
            ev->value
        );
    }
    return 0;
}

static u64
get_monotonic_time_ms()
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        unreachable("clock_gettime CLOCK_MONOTONIC failed");
        return 0;
    }

    return (u64)ts.tv_sec * 1000 + (u64)ts.tv_nsec / 1000000;
}

Exception
KeyMap_mouse_movement(KeyMap_c* self, int rel_x, int rel_y)
{
    uassert(self->mouse.dev);

    if (rel_x) {
        e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_REL, REL_X, rel_x)) {
            goto err;
        }
    }
    if (rel_y) {
        e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_REL, REL_Y, rel_y)) {
            goto err;
        }
    }
    e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_SYN, SYN_REPORT, 0)) {
        goto err;
    }
    return EOK;

err:
    return Error.io;
}

Exception
KeyMap_mouse_click(KeyMap_c* self, int button, int pressed)
{
    uassert(self->mouse.dev);
    struct input_event ev = { 0 };

    // Virtually unpress mouse mod key
    ev.type = EV_MSC;
    ev.code = MSC_SCAN;
    ev.value = 0;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    ev.type = EV_KEY;
    ev.code = self->mouse_key_code;
    ev.value = 0;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    // Send button event
    e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_KEY, button, pressed)) {
        goto err;
    }

    // Send synchronization event
    e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_SYN, SYN_REPORT, 0)) {
        goto err;
    }
    usleep(20000);

    // Virtually press mouse mod key
    ev.type = EV_MSC;
    ev.code = MSC_SCAN;
    ev.value = 0;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    ev.type = EV_KEY;
    ev.code = self->mouse_key_code;
    ev.value = 1;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    ev.type = EV_KEY;
    ev.code = self->mouse_key_code;
    ev.value = 2;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    e$except_errno (write(self->output.fd, &ev, sizeof(ev))) { return Error.io; }

    if (self->debug) { printf("Button %d %s\n", button, pressed ? "pressed" : "released"); }
    return EOK;
err:
    return Error.io;
}

Exception
KeyMap_mouse_wheel(KeyMap_c* self, int vertical)
{
    uassert(self->mouse.dev);

    if (vertical != 0) {
        e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_REL, REL_WHEEL, vertical)) {
            goto err;
        }
        e$except_errno (libevdev_uinput_write_event(self->mouse.dev, EV_SYN, SYN_REPORT, 0)) {
            goto err;
        }
    }
    return EOK;
err:
    return Error.io;
}

Exception
KeyMap_handle_key(KeyMap_c* self, struct input_event* ev)
{
    uassert(ev->type == EV_KEY || ev->type == EV_MSC || ev->type == EV_SYN);

    if (self->debug) { print_event(ev); }

    if (ev->code < KEY_MAX) {
        if (self->mouse_key_code && ev->code == self->mouse_key_code) {
            self->mouse_pressed = ev->value > 0;
            self->mouse.last_press_ts = 0;

            if (!self->mouse_pressed) {
                self->mouse.left = false;
                self->mouse.right = false;
                self->mouse.up = false;
                self->mouse.down = false;
            }
        }

        if (self->mod_key_code && ev->code == self->mod_key_code) {
            // Special case (bug) when MOD key released before arrow key,
            //   it was leading to infinite key loop
            if (self->mod_pressed && ev->value == 0 && self->last_key_mod) {
                ev->type = EV_MSC;
                ev->code = MSC_SCAN;
                ev->value = self->last_key_mod;
                e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }

                ev->type = EV_KEY;
                ev->code = self->last_key_mod;
                ev->value = 0;
                e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }

                ev->type = EV_SYN;
                ev->code = SYN_REPORT;
                ev->value = 0;
                e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }
            }
            self->mod_pressed = ev->value > 0;
            self->last_key_mod = 0;
        } else {
            if (ev->type == EV_KEY && ev->value == 2) { self->last_key_mod = ev->code; }

            if (self->mod_pressed) {
                if (self->mod_map[ev->code]) {
                    ev->code = self->mod_map[ev->code];
                    e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }

                    ev->type = EV_SYN;
                    ev->code = SYN_REPORT;
                    ev->value = 0;
                    e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }
                }
            } else if (self->mouse_pressed) {
                if (self->mouse_map[ev->code]) {
                    switch (self->mouse_map[ev->code]) {
                        case BTN_LEFT:
                            e$ret(KeyMap.mouse_click(self, BTN_LEFT, ev->value));
                            break;
                        case BTN_RIGHT:
                            e$ret(KeyMap.mouse_click(self, BTN_RIGHT, ev->value));
                            break;
                        case BTN_GEAR_UP:
                            e$ret(KeyMap.mouse_wheel(self, 1));
                            break;
                        case BTN_GEAR_DOWN:
                            e$ret(KeyMap.mouse_wheel(self, -1));
                            break;
                        // NOTE: movements are handled in KeyMap_handle_events
                        case KEY_RIGHT:
                            self->mouse.right = ev->value > 0;
                            break;
                        case KEY_LEFT:
                            self->mouse.left = ev->value > 0;
                            break;
                        case KEY_UP:
                            self->mouse.up = ev->value > 0;
                            break;
                        case KEY_DOWN:
                            self->mouse.down = ev->value > 0;
                            break;
                        default:
                            unreachable("Unsupported mouse btn or event");
                    }
                } else {
                    e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }
                }
            } else {
                ev->code = self->direct_map[ev->code] ? self->direct_map[ev->code] : ev->code;
                e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }
            }
        }
    } else {
        // Weird key code, but still fallback to the event propagation
        e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }
    }

    return EOK;
}

Exception
KeyMap_handle_mouse_move(KeyMap_c* self)
{
    // Initial direction
    int x = 0;
    int y = 0;
    if (self->mouse.up) { y = -10; }
    if (self->mouse.down) { y = 10; }
    if (self->mouse.left) { x = -10; }
    if (self->mouse.right) { x = 10; }

    if (x != 0 || y != 0) {
        u64 ts = get_monotonic_time_ms();
        if (self->mouse.last_press_ts == 0) { self->mouse.last_press_ts = ts; }

        f32 speed = self->mouse_sensitivity;
        u64 speedup_interval_ms = self->mouse_speedup_ms;
        u64 delta = ts - self->mouse.last_press_ts;

        if (delta < speedup_interval_ms) {
            if (delta < speedup_interval_ms / 10) {
                speed *= 0.1;
            } else {
                speed *= ((f64)delta / (f64)speedup_interval_ms);
            }
        }
        if (speed < 0.1f) { speed = 0.1f; }
        x *= speed;
        y *= speed;

        if (self->debug) { printf("Mouse move x=%d y=%d\n", x, y); }
        e$ret(KeyMap.mouse_movement(self, x, y));
    } else {
        // No cursor button, help reset speed
        self->mouse.last_press_ts = 0;
    }

    return EOK;
}

Exception
KeyMap_handle_events(KeyMap_c* self)
{
    int rc = 0;
    int poll_rc = 1;
    struct pollfd poll_input_fd = { self->input.fd, POLLIN, 0 };

    do {
        struct input_event ev;
        // Peek event que or file descriptor
        e$except_errno (poll_rc = libevdev_has_event_pending(self->input.dev)) { return Error.io; };

        if (poll_rc == 0) {
            // No events in current que, blocking wait with timeout for mouse
            e$except_errno (
                poll_rc = poll(&poll_input_fd, 1, (self->mouse_pressed) ? 10 : -1 /*timeout*/)
            ) {
                return Error.io;
            }
        }

        if (poll_rc > 0) {
            rc = libevdev_next_event(
                self->input.dev,
                LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
                &ev
            );
            if (rc == LIBEVDEV_READ_STATUS_SYNC) {
                printf("::::::::::::::::::::: dropped ::::::::::::::::::::::\n");
                while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                    rc = libevdev_next_event(self->input.dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
                }
                printf("::::::::::::::::::::: re-synced ::::::::::::::::::::::\n");
            }

            // Do magic remapping here
            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) { e$ret(KeyMap_handle_key(self, &ev)); }
            // printf("poll_rc = %d, rc = %d\n", poll_rc, rc);
        }


        if (self->mouse_pressed && (poll_rc == 0 /*timeout*/ ||
                                    (rc == LIBEVDEV_READ_STATUS_SUCCESS && ev.type == EV_KEY))) {
            e$ret(KeyMap_handle_mouse_move(self));
        }


    } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS ||
             rc == -EAGAIN);

    if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN) {
        return e$raise(Error.io, "Failed to handle events: %s\n", strerror(-rc));
    }
    return EOK;
}

void
KeyMap_destroy(KeyMap_c* self)
{
    if (self->input.dev) {
        libevdev_grab(self->input.dev, LIBEVDEV_UNGRAB);
        libevdev_free(self->input.dev);
    }

    if (self->input.fd > 0) {
        close(self->input.fd);
        self->input.fd = -1;
    }
    if (self->output.fd > 0) {
        ioctl(self->output.fd, UI_DEV_DESTROY);
        close(self->output.fd);
        self->output.fd = -1;
    }
    if (self->mouse.dev) { libevdev_uinput_destroy(self->mouse.dev); }
    memset(self, 0, sizeof(*self));
}

const struct __cex_namespace__KeyMap KeyMap = {
    // Autogenerated by CEX
    // clang-format off

    .create = KeyMap_create,
    .destroy = KeyMap_destroy,
    .find_mapped_keyboard = KeyMap_find_mapped_keyboard,
    .handle_events = KeyMap_handle_events,
    .handle_key = KeyMap_handle_key,
    .handle_mouse_move = KeyMap_handle_mouse_move,
    .is_qwerty_keyboard = KeyMap_is_qwerty_keyboard,
    .mouse_click = KeyMap_mouse_click,
    .mouse_movement = KeyMap_mouse_movement,
    .mouse_wheel = KeyMap_mouse_wheel,

    // clang-format on
};

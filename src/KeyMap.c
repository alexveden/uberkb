#include "KeyMap.h"
#include "cex.h"
#include "libevdev/libevdev.h"
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stdbool.h>

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
    usetup.id.vendor = 0x1234;  /* sample vendor */
    usetup.id.product = 0x5678; /* sample product */
    e$ret(str.copy(usetup.name, "UberKeyboardMappper", sizeof(usetup.name)));

    e$except_errno (ioctl(self->output.fd, UI_DEV_SETUP, &usetup)) { goto err; }
    e$except_errno (ioctl(self->output.fd, UI_DEV_CREATE)) { goto err; }

    // Attaching for input keyboard
    if (str.starts_with(input_dev_or_name, "/dev/")) {
        e$except_errno (self->input.fd = open(input_dev_or_name, O_RDONLY)) {
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
            e$except_errno (self->input.fd = open(it, O_RDONLY)) {
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

Exception
KeyMap_handle_key(KeyMap_c* self, struct input_event* ev)
{
    uassert(ev->type == EV_KEY || ev->type == EV_MSC || ev->type == EV_SYN);

    if (self->debug) { print_event(ev); }

    if (ev->code < KEY_MAX) {
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
            if (self->mod_pressed) {
                if (self->mod_map[ev->code]) {
                    ev->code = self->mod_map[ev->code];
                    if (ev->value == 2) {
                        self->last_key_mod = ev->code;
                    }
                    e$except_errno (write(self->output.fd, ev, sizeof(*ev))) { return Error.io; }

                    ev->type = EV_SYN;
                    ev->code = SYN_REPORT;
                    ev->value = 0;
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
KeyMap_handle_events(KeyMap_c* self)
{
    int rc = 0;
    do {
        struct input_event ev;
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
    .is_qwerty_keyboard = KeyMap_is_qwerty_keyboard,

    // clang-format on
};

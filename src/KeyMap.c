#include "KeyMap.h"
#include "cex.h"
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>

Exception
KeyMap_create(KeyMap_c* self, char* input_dev)
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
    e$except_errno (self->input.fd = open(input_dev, O_RDONLY)) { goto err; }
    e$except_errno (libevdev_new_from_fd(self->input.fd, &self->input.dev)) { goto err; };

    char* kbd_name = (char*)libevdev_get_name(self->input.dev);
    printf(
        "Input device ID: bus %#x vendor %#x product %#x\n",
        libevdev_get_id_bustype(self->input.dev),
        libevdev_get_id_vendor(self->input.dev),
        libevdev_get_id_product(self->input.dev)
    );
    printf("Evdev version: %x\n", libevdev_get_driver_version(self->input.dev));
    printf("Input device name: \"%s\"\n", kbd_name);
    printf("Phys location: %s\n", libevdev_get_phys(self->input.dev));
    printf("Uniq identifier: %s\n", libevdev_get_uniq(self->input.dev));

    uassert(
        str.find(kbd_name, "Microsoft") && "Microsoft Natural® Ergonomic Keyboard 4000 expected"
    );
    e$except_errno (libevdev_grab(self->input.dev, LIBEVDEV_GRAB)) { goto err; }

    return EOK;

err:
    return Error.io;
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
            self->mod_pressed = ev->value > 0;
        } else {
            if (self->mod_pressed) {
                if (self->mod_map[ev->code]) {
                    ev->code = self->mod_map[ev->code];
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
    .handle_events = KeyMap_handle_events,
    .handle_key = KeyMap_handle_key,

    // clang-format on
};

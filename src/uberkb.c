#define CEX_IMPLEMENTATION
#include "cex.h"
#include "KeyMap.c"
#include "KeyMap.h"
#include <linux/input-event-codes.h>

int
main(int argc, char** argv)
{
    int result = 1;
    KeyMap_c keymap = {
        .debug = true,
        .mod_key_code = KEY_LEFTALT,
        .mod_map = {
            [KEY_I] = KEY_UP, 
            [KEY_K] = KEY_DOWN, 
            [KEY_J] = KEY_LEFT, 
            [KEY_L] = KEY_RIGHT, 
            [KEY_SPACE] = KEY_BACKSPACE,
            [KEY_N] = KEY_DELETE,
            [KEY_U] = KEY_HOME,
            [KEY_O] = KEY_END,
            [KEY_Y] = KEY_PAGEUP,
            [KEY_H] = KEY_PAGEDOWN,
            [KEY_F] = KEY_SCROLLLOCK,
            [KEY_X] = KEY_CUT,
            [KEY_C] = KEY_COPY,
            [KEY_V] = KEY_PASTE,
        },
        .direct_map = {
            [KEY_CAPSLOCK] = KEY_ESC, 
        }
    };

    char* file = argv[1];
    if (argc < 2) {
        fprintf(stderr, "usage: myapp /dev/input/eventN");
        goto end;
    }

    e$goto(KeyMap.create(&keymap, file), end);
    e$goto(KeyMap.handle_events(&keymap), end);

    result = 0;
end:
    KeyMap.destroy(&keymap);
    return result;
}

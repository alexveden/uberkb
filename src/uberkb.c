#define CEX_IMPLEMENTATION
#include "KeyMap.c"
#include "KeyMap.h"
#include "cex.h"
#include <linux/input-event-codes.h>

int
main(int argc, char** argv)
{
    int result = 1;

    KeyMap_c keymap = { 0 };
    char* file = argv[1];
    if (argc < 2) {
        fprintf(stderr, "usage: uberkb /dev/input/eventN or 'My Keyboard Name'\n");
        goto end;
    }

    if (str.eq(file, "Ultimate Gadget Laboratories UHK 60 v1")) {
        log$info("Using special settings for UHK\n");
        // CUT/COPY/PASTE for UHK
        keymap = (KeyMap_c)  {
            // .debug = true,
            .direct_map = {
                [KEY_F13] = KEY_CUT,
                [KEY_F14] = KEY_COPY,
                [KEY_F15] = KEY_PASTE,
            },
        };
    } else {
        // Default mapping all other generic keyboards
        keymap = (KeyMap_c)  {
            // .debug = true,
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
    }

    e$goto(KeyMap.create(&keymap, file), end);
    e$goto(KeyMap.handle_events(&keymap), end);

    result = 0;
end:
    KeyMap.destroy(&keymap);
    return result;
}

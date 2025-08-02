#include <unistd.h>
#if __has_include("cex_config.h")
// Custom config file
#    include "cex_config.h"
#else
// Overriding config values
#    define cexy$cc_include "-I.", "-I./lib"
#    define CEX_LOG_LVL 4 /* 0 (mute all) - 5 (log$trace) */
#endif

#define cexy$pkgconf_libs "libevdev"

#define CEX_IMPLEMENTATION
#define CEX_BUILD
#include "cex.h"

Exception cmd_install(int argc, char** argv, void* user_ctx);

int
main(int argc, char** argv)
{

    cexy$initialize(); // cex self rebuild and init
    argparse_c args = {
        .description = cexy$description,
        .epilog = cexy$epilog,
        .usage = cexy$usage,
        argparse$cmd_list(
            cexy$cmd_all,
            cexy$cmd_fuzz, /* feel free to make your own if needed */
            cexy$cmd_test, /* feel free to make your own if needed */
            cexy$cmd_app,  /* feel free to make your own if needed */
            { .name = "install", .func = cmd_install, .help = "Install as a service" },
        ),
    };
    if (argparse.parse(&args, argc, argv)) { return 1; }
    void* my_user_ctx = NULL; // passed as `user_ctx` to command
    if (argparse.run_command(&args, my_user_ctx)) { return 1; }
    return 0;
}

/// Custom build command for building static lib
Exception
cmd_install(int argc, char** argv, void* user_ctx)
{
    log$info("Launching install command\n");
    e$assert(getuid() == 0 && "Expected to run with sudo");

    (void)user_ctx;

    argparse_c cmd_args = {
        .program_name = "./cex",
        .usage = "install 'keyboard_name'",
        .description = "Installs uberkb.service for a keyboard",
        argparse$opt_list(argparse$opt_help(), ),
    };
    e$ret(argparse.parse(&cmd_args, argc, argv));
    char* keyboard_name = argparse.next(&cmd_args);
    if (keyboard_name == NULL){
        argparse.usage(&cmd_args);
        return Error.argument;
    }
    char* sys_service = "/etc/systemd/system/uberkb.service";
    char* sys_exec = "/usr/local/uberkb";

    mem$scope(tmem$, _)
    {
        // Copy new version
        e$assert(os.path.exists(cexy$build_dir "/uberkb"));
        if (os.path.exists(sys_exec)){
            e$ret(os.fs.remove(sys_exec));
        }
        log$info("Copy executable to %s\n", sys_exec);
        e$ret(os.fs.copy(cexy$build_dir"/uberkb", sys_exec));
        e$ret(os$cmd("chown", "root:root", sys_exec));
        e$ret(os$cmd("chmod", "700", sys_exec));

        // Making a service
        log$info("Making a uberkb.service\n");
        e$assert(os.path.exists("uberkb.service") && "service template not found in current dir");

        char* template_service = io.file.load("uberkb.service", _);
        e$assert(template_service);
        io.printf("%s\n", template_service);

        char* service_txt = str.replace(template_service, "{KBD_NAME}", keyboard_name, _);
        e$assert(service_txt);

        e$ret(io.file.save(sys_service, service_txt));

        // Mode probe for kernel uinput module
        e$ret(io.file.save("/etc/modules-load.d/uberkb-service.conf", "uinput"));
        e$ret(os$cmd("modprobe", "uinput"));

        // Starting
        e$ret(os$cmd("systemctl", "daemon-reload"));
        e$ret(os$cmd("systemctl", "enable", "uberkb.service"));
        e$ret(os$cmd("systemctl", "restart", "uberkb.service"));
        e$ret(os$cmd("systemctl", "status", "uberkb.service"));
    }

    return EOK;
}

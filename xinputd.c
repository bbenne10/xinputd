#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>
#include <unistd.h>

struct {
  xcb_input_event_mask_t head;
  xcb_input_xi_event_mask_t mask;
} mask;

int global_args;
char **global_argv;
int last_num_devices = 0;

static void
xerror (const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(EXIT_FAILURE);
}

static void
help (int exit_status) {
  fprintf(stderr, "Usage: %s [options] command\n\n"
          "OPTIONS:\n"
          "-n: Do not daemonize\n"
          "-h: Print this help message\n"
          "\n"
          "Command will be run when an input device is attached or detached.\n",
          NAME);
  exit(exit_status);
}

static void
catch_child(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

int
get_num_devices (xcb_connection_t* conn) {
  xcb_generic_error_t * err;
  xcb_input_list_input_devices_cookie_t cookie;
  xcb_input_list_input_devices_reply_t * reply;
  int num_devices;

  cookie = xcb_input_list_input_devices(conn);
  if ((reply = xcb_input_list_input_devices_reply(conn, cookie, &err))) {
    /*
    TODO: only handle unique hardware devices, below is a start:
    xcb_input_device_info_iterator_t devices_it = xcb_input_list_input_devices_devices_iterator(reply);

    do {
      xcb_input_device_info_t * data = devices_it.data;
      printf("FOUND DEVICE: %d %d\n", data->device_id, data->device_type);

      xcb_input_device_info_next(&devices_it);
    } while (devices_it.rem > 0);

    printf("============================================\n");
    */
    num_devices = xcb_input_list_input_devices_devices_length(reply);
    free(reply);
  } else {
    fprintf(stderr, "Failed to list input devices. Error code was %d\n", err->error_code);
    num_devices = last_num_devices;
    free(err);
  }
  return num_devices;
}

void
exec_script () {
  if (fork() == 0) {
    setsid();
    execvp(global_argv[global_args], &(global_argv[global_args]));
  }
}


void
setup(xcb_connection_t* conn, xcb_screen_t* screen) {
  // Set ourselves up to receive the proper events
  mask.head.deviceid = XCB_INPUT_DEVICE_ALL;

  mask.mask = XCB_INPUT_XI_EVENT_MASK_DEVICE_CHANGED;
  mask.head.mask_len = sizeof(mask.mask) / sizeof(uint32_t);
  xcb_input_xi_select_events(conn, screen->root, 1, &mask.head);
  xcb_flush(conn);

  last_num_devices = get_num_devices(conn);
  exec_script();
}

int
main (int argc, char ** argv) {
  xcb_connection_t * conn;
  xcb_screen_t * screen;
  xcb_generic_event_t *ev;
  int num_devices;
  uid_t uid;
  int args = 1;
  int daemonize = 1;

  if (argc < 2) {
    help(EXIT_FAILURE);
  }

  for (args = 1; args < argc && *(argv[args]) == '-'; args++) {
    switch (argv[args][1]) {
      case 'n':
        daemonize = 0;
        break;
      case 'h':
        help(EXIT_SUCCESS);
        break;
      default:
        help(EXIT_FAILURE);
    }
  }

  if (((uid = getuid()) == 0) || uid != geteuid()) {
    xerror("%s may not run as root\n", NAME);
  }

  if (daemonize) {
    switch(fork()) {
      case -1:
        xerror("Could not fork\n");
      case 0:
        break;
      default:
        exit(EXIT_SUCCESS);
    }

    setsid();

    close(STDIN_FILENO);
    close(STDERR_FILENO);
    close(STDOUT_FILENO);
  }

  global_args = args;
  global_argv = argv;
  signal(SIGCHLD, catch_child);

  conn = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(conn)) {
    xerror("%s failed to get a connection to the X server", NAME);
  }

  screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

  setup(conn, screen);

  while ((ev = xcb_wait_for_event(conn))) {
    num_devices = get_num_devices(conn);

    if (num_devices > last_num_devices) {
      last_num_devices = num_devices;
      exec_script();
    } else if (num_devices < last_num_devices) {
      last_num_devices = num_devices;
      exec_script();
    }

    free(ev);
  }

  xcb_disconnect(conn);
}

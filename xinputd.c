#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>
#include <xcb/randr.h>
#include <unistd.h>

struct {
  xcb_input_event_mask_t head;
  xcb_input_xi_event_mask_t mask;
} mask;

int global_args;
char **global_argv;
int last_num_devices = 0;
uint8_t randr_base = -1;
uint8_t xinput_base = -1;
uint8_t xinput_opcode = -1;

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

void
exec_script () {
  if (fork() == 0) {
    setsid();
    execvp(global_argv[global_args], &(global_argv[global_args]));
  }
}

void
setup(xcb_connection_t* conn, xcb_screen_t* screen) {
  const xcb_query_extension_reply_t *randr_qer;
  const xcb_query_extension_reply_t *xinput_qer;
  xcb_input_xi_query_version_unchecked(conn, 1, 1);
  xcb_randr_query_version_unchecked(conn, 1, 5);

  randr_qer = xcb_get_extension_data(conn, &xcb_randr_id);
  if (!randr_qer || !randr_qer->present) {
    xerror("RandR extension missing\n");
    exit(EXIT_FAILURE);
  }
  randr_base = randr_qer->first_event;

  xinput_qer = xcb_get_extension_data(conn, &xcb_input_id);
  if (!xinput_qer || !xinput_qer->present) {
    xerror("XInput extension missing\n");
    exit(EXIT_FAILURE);
  }

  xinput_base = xinput_qer->first_event;
  xinput_opcode = xinput_qer->major_opcode;

  // Set ourselves up to receive the proper RANDR events
  mask.head.deviceid = XCB_INPUT_DEVICE_ALL;

  mask.mask = XCB_INPUT_XI_EVENT_MASK_DEVICE_CHANGED;
  mask.head.mask_len = sizeof(mask.mask) / sizeof(uint32_t);
  xcb_input_xi_select_events(conn, screen->root, 1, &mask.head);

  xcb_randr_select_input(conn, screen->root,
                         XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
  xcb_flush(conn);
  /*
  last_num_devices = get_num_devices(conn);
  exec_script();
  */
}

void
randr_callback (xcb_connection_t* conn, xcb_randr_notify_event_t* ev) {
  // xcb_randr_notify_t not_ev = (xcb_randr_notify_event_t*) ev;
  if (ev->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE) {
    xcb_randr_output_t output = ev->u.oc.output;
    xcb_randr_get_output_info_cookie_t cookie = xcb_randr_get_output_info_unchecked(
        conn, output, XCB_CURRENT_TIME);
    xcb_randr_get_output_info_reply_t* reply = xcb_randr_get_output_info_reply(conn, cookie, NULL);

  }
}

void
device_change_callback (xcb_ge_generic_event_t* ev) {
  printf("Got device change\n");
}

int
main (int argc, char ** argv) {
  xcb_connection_t * conn;
  xcb_screen_t * screen;
  xcb_generic_event_t *ev;
  xcb_ge_generic_event_t *ge_ev;
  xcb_randr_screen_change_notify_event_t* randr_ev;
  xcb_timestamp_t last_time;

  uid_t uid;
  int args = 1;
  int daemonize = 1;

  uint8_t response_type;

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
    response_type = ev->response_type & ~0x80;

    if (response_type == 0) {
      printf("Got error?\n");
    } else if (response_type == randr_base + XCB_RANDR_NOTIFY) {
        randr_ev = (xcb_randr_screen_change_notify_event_t*) ev;
        if (randr_ev->timestamp != last_time) {
          last_time = randr_ev->timestamp;
          randr_callback((xcb_randr_notify_event_t *) ev);
        }
    } else if (response_type == 35) { // ge generic event; delineated by 'extension'
      ge_ev = (xcb_ge_generic_event_t*) ev;
      if (ge_ev->extension == xinput_opcode && ge_ev->event_type == XCB_INPUT_DEVICE_CHANGED) {
        device_change_callback(ge_ev);
      }
    } else {
      printf("Got event: %d (%d)\n", response_type, randr_base);
    }
    free(ev);
  }



  /*
uint8_t randr_ev_type = ev->response_type - randr_base;
    if (randr_ev_type == XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
      randr_ev = (xcb_randr_screen_change_notify_event_t*) ev;

      if (randr_ev->timestamp != last_time) {
        last_time = randr_ev->timestamp;
        printf("GOT RANDR EVENT: %d\n", ev->response_type);
      }
    } else {
      // xinput event
      num_devices = get_num_devices(conn);

      if (num_devices > last_num_devices) {
        last_num_devices = num_devices;
        exec_script();
      } else if (num_devices < last_num_devices) {
        last_num_devices = num_devices;
        exec_script();
      }
    }
    free(ev);
  }
  */

  xcb_disconnect(conn);
}

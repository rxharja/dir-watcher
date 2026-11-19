#include <assert.h>
#include <err.h>
#include <linux/limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <libgen.h>
#include "file.h"
#include <sys/signalfd.h>
#include <errno.h>
#include "app.h"

int main(void) {
  AppCtx app = {0};

  int poll_num;

  struct signalfd_siginfo fdsi;
  ssize_t signal_size;

  struct pollfd *fds = NULL;
  AppStatus res = app_setup(&app);
  if (res.kind != APP_OK) goto fail;

  app_watched_dirs_show(&app);

  nfds_t nfds = 2 + app.rules.watch_path_count; // app.rules.watch_path_count;
  fds = calloc(nfds, sizeof(fds[0]));
  if (!fds) { 
    perror("calloc");
    goto fail; 
  }

  fds[0].fd = app.handles.signal_fd;
  fds[0].events = POLLIN;

  fds[1].fd = app.handles.inotify_fd; // inotify input
  fds[1].events = POLLIN;

  for (size_t i = 0; i < app.rules.watch_path_count; i++) {
    fds[2 + i].fd = app.handles.watched[i].td; // timer
    fds[2 + i].events = POLLIN; // can call read
  }

  printf("Press Ctrl+C to terminate.\n");
  printf("Listening for events.\n");

  while (1) {
    poll_num = poll(fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) continue;
      perror("poll");
      exit(EXIT_FAILURE);
    }

    if (poll_num > 0) {
      // Management of timer fd's triggering
      for (size_t i = 2; i < nfds; i++) {
        if (fds[i].revents & POLLIN) {
          // subtracting 2 to shift it back to watch path count
          app_watch_timer_handle(&app, i - 2); 
        }
      }

      // signal events
      if (fds[0].revents & POLLIN) {
        signal_size = read(fds[0].fd, &fdsi, sizeof(fdsi));

        if (signal_size != sizeof(fdsi)) {
          perror("signalfd");
          exit(EXIT_FAILURE);
        }

        if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) break;
        else continue;
      }

      // inotify events 
      if (fds[1].revents & POLLIN) app_events_handle(&app);

    }
  }

  printf("Listening for events stopped.\n");
  app_destroy(&app);
  if (fds) free(fds);
  exit(EXIT_SUCCESS);

fail:
  app_status_show(res);
  if (fds) free(fds);
  exit(EXIT_FAILURE);
}

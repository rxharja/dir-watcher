#include <assert.h>
#include <linux/limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <libgen.h>
#include "file.h"
#include <sys/signalfd.h>
#include <errno.h>
#include "app.h"

ssize_t signal_setup() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
    perror("sigprocmask");
    exit(EXIT_FAILURE); 
  }

  int sig_fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);

  if (sig_fd == -1) {
    perror("signalfd");
    exit(EXIT_FAILURE);
  }

  return sig_fd;
}

int main(void) {
  AppCtx app = {0};
  if (app_setup(&app) != 0) {
    perror("app_setup");
    app_destroy(&app);
    exit(EXIT_FAILURE);
  }

  app_watched_dirs_show(&app);

  struct signalfd_siginfo fdsi;
  ssize_t signal_size;

  nfds_t nfds = 2;
  struct pollfd fds[2];

  fds[0].fd = signal_setup(); // signal input
  fds[0].events = POLLIN;

  fds[1].fd = app.handles.inotify_fd; // inotify input
  fds[1].events = POLLIN;

  printf("Press Ctrl+C to terminate.\n");
  printf("Listening for events.\n");

  int poll_num;
  while (1) {
    poll_num = poll(fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) continue;
      perror("poll");
      exit(EXIT_FAILURE);
    }

    if (poll_num > 0) {
      if (fds[0].revents & POLLIN) {
        signal_size = read(fds[0].fd, &fdsi, sizeof(fdsi));

        if (signal_size != sizeof(fdsi)) {
          perror("signalfd");
          exit(EXIT_FAILURE);
        }

        if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) break;
        else continue;
      }

      if (fds[1].revents & POLLIN) app_events_handle(&app);
    }
  }

  printf("Listening for events stopped.\n");
  app_destroy(&app);
  exit(EXIT_SUCCESS);
}

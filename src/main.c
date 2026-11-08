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

static void events_handle(AppCtx *app, int argc, char *argv[]) {
  char buf[sizeof(struct inotify_event) + NAME_MAX + 1] __attribute__((aligned(__alignof__(struct inotify_event))));

  const struct inotify_event *event; 

  ssize_t size;

  for (;;) {
    size = read(app->handles.inotify_fd, buf, sizeof buf);

    if (size == -1 && errno != EAGAIN) {
      perror("read");
      exit(EXIT_FAILURE);
    }

    if (size <= 0) break;

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & IN_CLOSE_WRITE) printf("IN_CLOSE_WRITE: ");
      if (event->mask & IN_MOVED_TO) printf("IN_MOVED_TO: ");
    }

    for (int i = 0; i < argc; ++i) {
      if (app->handles.watched[i].wd == event->wd) {
        printf("%s/", argv[i + 1]);
        break;
      }
    }

    if (event->len) printf("%s", event->name);
    if (event->mask & IN_ISDIR) printf(" [directory]\n");
    else printf(" [file]\n");

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
        for (int i = 0; i < argc; ++i) {
          if (app->handles.watched[i].wd == event->wd && event->len) {
            file_move_handle(app, &app->handles.watched[i], event->name);
            break;
          }
        }
      }
    }
  }
}

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

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage : %s PATH [PATH ...]\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  AppCtx app = {0};
  if (app_setup(&app, argc, argv) != 0) {
    perror("app_setup");
    app_destoy(&app);
    exit(EXIT_FAILURE);
  }

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

      if (fds[1].revents & POLLIN) events_handle(&app, argc, argv);
    }
  }

  printf("Listening for events stopped.\n");
  app_destoy(&app);
  exit(EXIT_SUCCESS);
}

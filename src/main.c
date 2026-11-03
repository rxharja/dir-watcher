#include <errno.h>
#include <linux/limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include "file.c"
#include <sys/signalfd.h>

static void events_handle(int fd, int *wd, int argc, char *argv[]) {
  char buf[sizeof(struct inotify_event) + NAME_MAX + 1] __attribute__((aligned(__alignof__(struct inotify_event))));

  const struct inotify_event *event; 

  ssize_t size;

  for (;;) {
    size = read(fd, buf, sizeof buf);

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

    for (size_t i = 1; i < argc; ++i) {
      if (wd[i] == event->wd) {
        printf("%s/", argv[i]);
        break;
      }
    }

    if (event->len) printf("%s", event->name);
    if (event->mask & IN_ISDIR) printf(" [directory]\n");
    else printf(" [file]\n");

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
        for (size_t i = 1; i < argc; ++i) {
          if (wd[i] == event->wd && event->len) {
            file_move(argv[i], strlen(argv[1]), event->name, strlen(event->name));
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
  char buf;
  int fd, i, poll_num;
  int *wd;
  nfds_t nfds;
  struct pollfd fds[2];

  if (argc < 2) {
    printf("Usage : %s PATH [PATH ...]\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  printf("Press Ctrl+C to terminate.\n");

  fd = inotify_init1(IN_NONBLOCK);
  if (fd == -1) {
    perror("inotify_init1");
    exit(EXIT_FAILURE);
  }

  wd = calloc(argc, sizeof(int));

  if (wd == NULL) {
    perror("calloc");
    exit(EXIT_FAILURE);
  }

  for (i = 1; i < argc; i++) {
    wd[i] = inotify_add_watch(fd, argv[1], IN_CLOSE_WRITE | IN_MOVED_TO);

    if (wd[i] == -1) {
      fprintf(stderr, "Cannot watch '%s': %s\n", argv[i], strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  struct signalfd_siginfo fdsi;
  ssize_t signal_size;

  nfds = 2;
  fds[0].fd = signal_setup(); // signal input
  fds[0].events = POLLIN;

  fds[1].fd = fd; // inotify input
  fds[1].events = POLLIN;

  printf("Listening for events.\n");

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

        if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) exit(EXIT_SUCCESS);
        else continue;

        break;
      }

      if (fds[1].revents & POLLIN) events_handle(fd, wd, argc, argv);
    }
  }

  printf("Listening for events stopped.\n");

  close(fd);

  free(wd);

  exit(EXIT_SUCCESS);
}

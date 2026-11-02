#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

#define DIR "/home/redonxharja/downloads"

static void file_move(const char *dir, size_t dir_len, const char *file_name, size_t len) {
  static char out_path[] = "/home/redonxharja";
  char original_path[256];
  char final_path[256];

  snprintf(original_path, dir_len + len + 1, "%s%s", dir, file_name);
  snprintf(final_path, sizeof(out_path) + len + 1, "%s/%s", out_path, file_name);

  printf("Moving to %s\n", final_path);
  rename(original_path, final_path);
}

static void events_handle(int fd, int *wd, int argc, char *argv[]) {
  // char buf[sizeof(struct inotify_event) + NAME_MAX + 1] 
  //   __attribute__((aligned(__alignof__(struct inotify_event))));
  char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

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
      if (event->mask & IN_OPEN) printf("IN_OPEN: ");
      if (event->mask & IN_CREATE) printf("IN_CREATE: ");
      if (event->mask & IN_ATTRIB) printf("IN_ATTRIB: ");
      if (event->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE: ");
      if (event->mask & IN_CLOSE_WRITE) printf("IN_CLOSE_WRITE: ");
      if (event->mask & IN_MOVED_FROM) printf("IN_MOVED_FROM: ");
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

  printf("Press ENTER key to terminate.\n");

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
    wd[i] = inotify_add_watch(fd, argv[1], IN_OPEN | IN_CLOSE | IN_ATTRIB | IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO);

    if (wd[i] == -1) {
      fprintf(stderr, "Cannot watch '%s': %s\n", argv[i], strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  nfds = 2;
  fds[0].fd = STDIN_FILENO; // console input
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
        while (read(STDIN_FILENO, &buf, 1) > 0 && buf != '\n') continue;
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

#include <complex.h>
#include <cstdio>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>

#define DIR "/home/redonxharja/downloads"

int main(void) {
  //set up
  char buf[sizeof(struct inotify_event) + NAME_MAX + 1] 
    __attribute__((aligned(__alignof__(struct inotify_event))));

  const struct inotify_event *event; 
  ssize_t len;

  int fd = inotify_init();

  int wd = inotify_add_watch(fd, DIR, IN_CREATE | IN_MOVED_FROM | IN_MASK_CREATE);

  // do some work
  len = read(fd, buf, sizeof(buf));

  if(len == -1 && errno != EAGAIN) {
    perror("read");
    return -1;
  }

  for (char *ptr = buf; ptr < buf + len; 
        ptr += sizeof(struct inotify_event) + event->len) {

    event = (const struct inotify_event *)ptr;

    if (event->mask & IN_CREATE) printf("IN CREATE: ");
    if (event->mask & IN_MOVED_FROM) printf("IN MOVED FROM: ");
    if (event->len) printf("%s", event->name);
  }


  // clean up
  inotify_rm_watch(fd, wd);
  close(wd);
  close(fd);

  return 0;
}

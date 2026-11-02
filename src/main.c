#include <sys/inotify.h>
#include <unistd.h>

#define DIR "/home/redonxharja/downloads"

int main(void) {
  //set up
  struct inotify_event *event; // assume that it binds to the read output?
  void *event_buf[2048];

  int fd = inotify_init();

  int wd = inotify_add_watch(fd, DIR, 0);

  // do some work
  int bytes_read = read(fd, event_buf, 2048);


  // clean up
  inotify_rm_watch(fd, wd);
  close(wd);
  close(fd);

  return 0;
}

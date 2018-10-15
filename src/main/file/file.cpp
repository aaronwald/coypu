#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "file.h"

using namespace coypu::file;

int FileUtil::MakeTemp(const char *pfix, char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "/tmp/%s-XXXXXX", pfix);
  return ::mkstemp(buf);
}

int FileUtil::Remove (const char *pathname) {
  return ::unlink(pathname);
}

off64_t FileUtil::LSeek (int fd, off64_t offset, int whence) {
  return ::lseek64(fd, offset, whence);
}

off64_t FileUtil::LSeekEnd (int fd) {
  return ::lseek64(fd, 0, SEEK_END);
}

off64_t FileUtil::LSeekSet (int fd, off64_t offset) {
  return ::lseek64(fd, offset, SEEK_SET);
}

int FileUtil::Truncate (int fd, off64_t size) {
  return ::ftruncate64(fd, size);
}

ssize_t FileUtil::Write (int fd, const char *buf, size_t count) {
  return ::write(fd, buf, count);
}

ssize_t FileUtil::Read (int fd, void *buf, size_t count) {
  return ::read(fd, buf, count);
}

int FileUtil::Close (int fd) {
  return ::close(fd);
}

int FileUtil::Open (const char *pathname, int flags, mode_t mode) {
  return ::open(pathname, flags, mode);
}

int FileUtil::Exists(const char *file, bool &x) {
  struct stat s = {};
  int i = ::stat(file, &s);
  x = i == 0;
  return i;
}

int FileUtil::GetSize (int fd, off64_t &offset) {
  struct stat s = {};
  int i = ::fstat(fd, &s);
  if (i == 0) {
    offset = s.st_size;
  }
  return i;
}

void * FileUtil::MMapSharedWrite (int fd, off64_t offset, size_t len) {
  return ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

void * FileUtil::MMapSharedRead (int fd, off64_t offset, size_t len) {
  return ::mmap(nullptr, len, PROT_WRITE, MAP_SHARED, fd, offset);
}

int FileUtil::MUnmap (void *addr, size_t len) {
  return ::munmap(addr, len);
}


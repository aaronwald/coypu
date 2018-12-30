#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include "file.h"
#include "util/string-util.h"

using namespace coypu::file;

int FileUtil::Mkdir (const char *dir, mode_t mode, bool ignore_last) {
  std::vector<std::string> paths;
  char path[PATH_MAX];
  ::memset(path, 0, sizeof(path));

  coypu::util::StringUtil::Split(dir, '/', paths);
  if (ignore_last) paths.pop_back();

  int offset = 0;
  for (std::string &s : paths) {
    offset += ::snprintf(&path[offset], sizeof(path)-offset, offset == 0 ? "%s" : "/%s", s.c_str());
    ::mkdir(path, mode); // ignore error
  }
  return 0;
}

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

void * MMapShared::MMapWrite (int fd, off64_t offset, size_t len) {
  return ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

void * MMapShared::MMapRead (int fd, off64_t offset, size_t len) {
  return ::mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, offset);
}

int MMapShared::MUnmap (void *addr, size_t len) {
  return ::munmap(addr, len);
}

off64_t MMapShared::LSeekSet (int fd, off64_t offset) {
  return FileUtil::LSeekSet(fd, offset);
}

int MMapShared::Truncate (int fd, off64_t offset) {
  return FileUtil::Truncate(fd, offset);
}

void * MMapAnon::MMapWrite (int fd, off64_t offset, size_t len) {
  return ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void * MMapAnon::MMapRead (int fd, off64_t offset, size_t len) {
  return ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

int MMapAnon::MUnmap (void *addr, size_t len) {
  return ::munmap(addr, len);
}

off64_t MMapAnon::LSeekSet (int fd, off64_t offset) {
  return FileUtil::LSeekSet(fd, offset);
}

int MMapAnon::Truncate (int fd, off64_t offset) {
  return FileUtil::Truncate(fd, offset);
}

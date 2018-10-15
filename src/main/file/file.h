#ifndef __COYPU_FILE_
#define __COYPU_FILE_
#include <string>
#include <sys/types.h>

namespace coypu {
  namespace file {
    class FileUtil {
    public:
      FileUtil() = delete;
      ~FileUtil () = delete;
      FileUtil (const FileUtil &other) = delete;
      FileUtil &operator= (const FileUtil &other) = delete;
      FileUtil (FileUtil &&) = delete;
      FileUtil &operator= (FileUtil &&) = delete;
      
      /* Must be closed */
      static int MakeTemp(const char *pfix, char *buf, size_t bufsize);
      static int Open (const char *pathname, int flags, mode_t mode);
      static int Close (int fd);
      static int Truncate (int fd, off64_t offset);
      static int GetSize (int fd, off64_t &offset);
      static int Remove(const char *pathname);
      static int Exists(const char *file, bool &b);
      static off64_t LSeek (int fd, off64_t offset, int whence);
      static off64_t LSeekEnd (int fd);
      static off64_t LSeekSet (int fd, off64_t offset);
      static ssize_t Write (int fd, const char *buf, size_t count);
      static ssize_t Read (int fd, void *buf, size_t count);

      // MMap
      static void *MMapSharedRead (int fd, off64_t offset, size_t len);
      static void *MMapSharedWrite (int fd, off64_t offset, size_t len);
      static int MUnmap (void *addr, size_t len);
    };
  }
}

#endif

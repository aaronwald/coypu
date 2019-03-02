#ifndef __COYPU_FILE_
#define __COYPU_FILE_
#include <string>
#include <sys/stat.h>
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
      static int Mkdir(const char *dir, mode_t mode, bool ignore_last=false);
      static off64_t LSeek (int fd, off64_t offset, int whence);
      static off64_t LSeekEnd (int fd);
      static off64_t LSeekSet (int fd, off64_t offset);
      static ssize_t Write (int fd, const char *buf, size_t count);
      static ssize_t Read (int fd, void *buf, size_t count);
    };

	 class MMapShared {
    public:
      MMapShared() = delete;
      ~MMapShared () = delete;
      MMapShared (const MMapShared &other) = delete;
      MMapShared &operator= (const MMapShared &other) = delete;
      MMapShared (MMapShared &&) = delete;
      MMapShared &operator= (MMapShared &&) = delete;

		static void * MMapRead (int fd, off64_t offset, size_t len);
      static void * MMapWrite (int fd, off64_t offset, size_t len);
      static int MUnmap (void *addr, size_t len);
		static off64_t LSeekSet (int fd, off64_t offset);
		static int Truncate (int fd, off64_t offset);
      static int GetSize (int fd, off64_t &offset);
	 };
	 
	 class MMapAnon {
    public:
      MMapAnon() = delete;
      ~MMapAnon () = delete;
      MMapAnon (const MMapAnon &other) = delete;
      MMapAnon &operator= (const MMapAnon &other) = delete;
      MMapAnon (MMapAnon &&) = delete;
      MMapAnon &operator= (MMapAnon &&) = delete;

		static void * MMapRead (int fd, off64_t offset, size_t len);
      static void * MMapWrite (int fd, off64_t offset, size_t len);
      static int MUnmap (void *addr, size_t len);
		static off64_t LSeekSet (int fd, off64_t offset);
		static int Truncate (int fd, off64_t offset);
		static int GetSize (int fd, off64_t &offset);
	 };

  }
}

#endif

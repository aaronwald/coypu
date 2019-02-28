
#pragma once

#include <memory>
#include <fcntl.h>
#include <unistd.h>

#include "store/store.h"
#include "mem/mem.h"
#include "file/file.h"

namespace coypu {
  namespace store {
	 class StoreUtil {
	 public:
		template <typename StreamType, typename BufType>
		  static std::shared_ptr <StreamType> CreateAnonStore () {
		  int pageMult = 64;
		  size_t pageSize = pageMult * coypu::mem::MemManager::GetPageSize();
		  off64_t curSize = 0;
		  std::shared_ptr<BufType> bufSP = std::make_shared<BufType>(pageSize, curSize, -1, true);
		  return std::make_shared<StreamType>(bufSP);
		}

		template <typename StreamType, typename BufType>
		  static std::shared_ptr <StreamType> CreateRollingStore (const std::string &path, int pageMultiplier = 64) {
		  bool fileExists = false;
		  char storeFile[PATH_MAX];
		  std::shared_ptr<StreamType> streamSP = nullptr; 
		
		  coypu::file::FileUtil::Mkdir(path.c_str(), 0777, true);
	
		  // TODO if we go backward it will be quicker (less copies?)
		  for (uint32_t index = 0; index < UINT32_MAX; ++index) {
			 ::snprintf(storeFile, PATH_MAX, "%s.%09d.store", path.c_str(), index);
			 fileExists = false;
			 coypu::file::FileUtil::Exists(storeFile, fileExists);
			 if (!fileExists) {
				// open in direct mode
				int fd = coypu::file::FileUtil::Open(storeFile, O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
				if (fd >= 0) {
				  size_t pageSize = pageMultiplier * coypu::mem::MemManager::GetPageSize();
				  off64_t curSize = 0;
				  coypu::file::FileUtil::GetSize(fd, curSize);

				  std::shared_ptr<BufType> bufSP = std::make_shared<BufType>(pageSize, curSize, fd, false);
				  streamSP = std::make_shared<StreamType>(bufSP);
				} else {
				  return nullptr;
				}
				return streamSP;
			 }
		  }
		  return nullptr;
		}

		template <typename BufType>
		  static std::shared_ptr <BufType> CreateSimpleBuf (const std::string &path, int pageMultiplier = 64) {
		  coypu::file::FileUtil::Mkdir(path.c_str(), 0777, true);
	
		  // open in direct mode
		  int fd = coypu::file::FileUtil::Open(path.c_str(), O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
		  if (fd >= 0) {
			 size_t pageSize = pageMultiplier * coypu::mem::MemManager::GetPageSize();
			 off64_t curSize = 0;
			 coypu::file::FileUtil::GetSize(fd, curSize);
			 
			 return std::make_shared<BufType>(pageSize, curSize, fd, false);
		  } else {
			 return nullptr;
		  }
		}

		
		template <typename StreamType, typename BufType>
		  static std::shared_ptr <StreamType> CreateSimpleStore (const std::string &path, int pageMultiplier = 64) {
		  return std::make_shared<StreamType>(CreateSimpleBuf<BufType>(path, pageMultiplier));
		}

		
	 private:
		StoreUtil() = delete;
	 };
  }
}

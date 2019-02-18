
#ifndef __COYPU_STRING_H
#define __COYPU_STRING_H

#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <vector>

// https://stackoverflow.com/questions/236129/the-most-elegant-way-to-iterate-the-words-of-a-string
namespace coypu {
  namespace util {
	 class StringUtil {
	 public:
		static void ToLower (std::string &s) {
		  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		}
		
		static void Split(const std::string &s, char delim, std::vector <std::string> &out) {
		  std::stringstream ss(s);
		  std::string item;
		  while (std::getline(ss, item, delim)) {
			 out.push_back(item);
		  }
		}
		
	 private:
		StringUtil() = delete;
	 };
  }
}

#endif

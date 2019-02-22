
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
		static inline void ToLower (std::string &s) {
		  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		}
		
		static inline void Split(const std::string &s, char delim, std::vector <std::string> &out) {
		  std::stringstream ss(s);
		  std::string item;
		  while (std::getline(ss, item, delim)) {
			 out.push_back(item);
		  }
		}

		// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
		static inline void LTrim(std::string &s) {
		  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
				  return !std::isspace(ch);
				}));
		}

		static inline void RTrim(std::string &s) {
		  s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
				  return !std::isspace(ch);
				}).base(), s.end());
		}
		
		static inline void Trim(std::string &s) {
		  LTrim(s);
		  RTrim(s);
		}
		
	 private:
		StringUtil() = delete;
	 };
  }
}

#endif

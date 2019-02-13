
#ifndef __COYPU_CONFIG_H
#define __COYPU_CONFIG_H

#include <assert.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdlib>

namespace coypu {
    namespace config {

        class CoypuConfig
        {
            public:
                enum CoypuConfigType { 
                    CCT_MAP 		= 0,
                    CCT_SEQUENCE 	= 1,
                    CCT_SCALAR 	    = 2
                };

                static std::shared_ptr<CoypuConfig> Parse (const std::string &file);
                
                virtual ~CoypuConfig () {
                    _seqs.clear();
                    _maps.clear();
                }
                
                void Add (std::shared_ptr<CoypuConfig> newItem) {
                    // if this is a map, is this a key or value
                    // if this is a sequence, just add 

                    if (_type == CCT_MAP) {
                        if (newItem->GetType() == CCT_MAP || newItem->GetType() == CCT_SEQUENCE) {
                            if (_isValue) {
                                _maps.insert(std::make_pair(_lastKey, newItem));
                                _isValue = false;
                            } else {
                                assert(false);
                            }
                        } else if (newItem->GetType() == CCT_SCALAR) {
                            if (_isValue) {
                                _maps.insert(std::make_pair(_lastKey, newItem));
                                _isValue = false;
                            } else {
                                _lastKey = newItem->GetValue();
                                _isValue = true;
                            }
                        }
                    } else if (_type == CCT_SEQUENCE) {
                        _seqs.push_back(newItem);
                    } else if (_type == CCT_SCALAR) {
                        assert(false);
                    }
                }

                CoypuConfigType GetType () {
                    return _type;
                }

                const std::string & GetValue () {
                    return _value;
                }

                const bool GetKeyValue (const std::string & key, std::string &out) {
                    auto i = _maps.find(key);
                    if (i != _maps.end()) {
                        out =  (*i).second->GetValue();
                        return true;
                    }

                    return false;
                }

                template <typename T> 
                const bool GetValue (const std::string & key, T &out) {
                    auto i = _maps.find(key);
                    if (i != _maps.end()) {
                        out =  (*i).second->GetValue();
                        return true;
                    }

                    return false;
                }

					 template <typename T> 
						const bool GetValue (const std::string & key, T &out, const T &defaultValue) {
                    auto i = _maps.find(key);
                    if (i != _maps.end()) {
                        out =  (*i).second->GetValue();
                        return true;
                    }
						  out = defaultValue;
                    return true;
                }

                void GetKeys (std::vector<std::string> & out) const {
                    for (auto i = _maps.begin(); i != _maps.end(); ++i) {
                        out.push_back((*i).first);
                    }
                }

                void GetSeqValues (std::vector<std::shared_ptr<CoypuConfig>> & out) const {
                    for (auto i = _seqs.begin(); i != _seqs.end(); ++i) {
                        out.push_back((*i));
                    }
                }

                void GetSeqValues (const std::string &name, std::vector<std::string> &out) const {
                    std::shared_ptr <CoypuConfig> c = GetConfig(name);
                    if (c) {
                        std::vector<std::shared_ptr<CoypuConfig>> syms;

                        c->GetSeqValues(syms);
                        for (auto s : syms) {
                            out.push_back(s->GetValue());
                        }
                    }
                }

                std::shared_ptr <CoypuConfig> GetConfig (const std::string &name) const {
                    auto i = _maps.find(name);
                    if (i != _maps.end()) {
                        return (*i).second;
                    }
                    return nullptr;
                }

                bool HasConfig (const std::string &name) const {
                    return _maps.find(name) != _maps.end();
                }
            private:
                CoypuConfig (const CoypuConfig &other) = delete;
                CoypuConfig &operator= (const CoypuConfig &other) = delete;
                explicit CoypuConfig (CoypuConfigType type) : _type(type), _value(""), _isValue(false) {}
                explicit CoypuConfig (const std::string &value) : _type(CCT_SCALAR), _value(value), _isValue(false) {}


                CoypuConfigType _type;

                std::vector <std::shared_ptr<CoypuConfig>> _seqs;
                std::unordered_map <std::string, std::shared_ptr<CoypuConfig>> _maps;
                std::string _value;
                std::string _lastKey;
                bool _isValue;
        };


	template <> 
	  const bool CoypuConfig::GetValue<bool> (const std::string & key, bool &out);
	
	template <> 
	  const bool CoypuConfig::GetValue<int> (const std::string & key, int &out);
	template <> 
	  const bool CoypuConfig::GetValue<std::string> (const std::string & key, std::string &out);
    }
}

#endif


#include <stdio.h>
#include <yaml.h>

#include <memory>

#include "config.h"

using namespace coypu::config;

std::shared_ptr<CoypuConfig> CoypuConfig::Parse (const std::string &file) 
{
    yaml_parser_t parser;
	yaml_event_t event;

	FILE *input = ::fopen(file.c_str(), "rb");
	if (!input)
	{
		fprintf(stderr, "Unable to open '%s'\n", file.c_str());
        return nullptr;
	}

	if (!yaml_parser_initialize(&parser))
	{
		fprintf(stderr, "Yaml parser failed to initialize.\n");
	}

	yaml_parser_set_input_file(&parser, input);

	// check events
	bool done = false, fail = false;
	std::shared_ptr<CoypuConfig> rootConfig(new CoypuConfig(CoypuConfig::CCT_MAP));
	rootConfig->Add(std::shared_ptr<CoypuConfig>(new CoypuConfig("<<ROOT>>")));
	std::shared_ptr<CoypuConfig> curConfig = rootConfig;
	std::vector<std::shared_ptr<CoypuConfig>> configStack;

	while (!done)
	{
		/* Get the next event. */
		if (!yaml_parser_parse(&parser, &event)) {
			printf ("yaml error [%d] %s\n", parser.error, parser.problem);
			done = true;
            fail = true;
		} else {
			switch (event.type) {
				case YAML_SEQUENCE_START_EVENT:
				{
					assert(curConfig != nullptr);
					configStack.push_back(curConfig);
					std::shared_ptr<CoypuConfig> lastConfig = curConfig;
					curConfig =  std::shared_ptr<CoypuConfig>(new CoypuConfig(CoypuConfig::CCT_SEQUENCE));
					lastConfig->Add(curConfig);
				}
				break;

				case YAML_SEQUENCE_END_EVENT:
				{
					assert(configStack.size() > 0);
					curConfig = configStack.back();
					assert(curConfig != nullptr);
					configStack.pop_back();
				}
				break;

				case YAML_MAPPING_START_EVENT:
				{
					assert(curConfig != nullptr);
					configStack.push_back(curConfig);

					std::shared_ptr<CoypuConfig> lastConfig = curConfig;
                    curConfig =  std::shared_ptr<CoypuConfig>(new CoypuConfig(CoypuConfig::CCT_MAP));

					lastConfig->Add(curConfig);

				}
				break;

				case YAML_MAPPING_END_EVENT:
				{
					assert(configStack.size() > 0);
					curConfig = configStack.back();
					assert(curConfig != nullptr);
					configStack.pop_back();
				}
				break;

				case YAML_ALIAS_EVENT: {
					assert(false);
				}
				break;

				case YAML_SCALAR_EVENT:
				{ 
                    std::shared_ptr<CoypuConfig> sp(new CoypuConfig(std::string(reinterpret_cast<char *>(event.data.scalar.value))));
					curConfig->Add(sp);
				}
				break;

				case YAML_STREAM_END_EVENT:
				{ 
					done = true;
				}
				break;

				case YAML_NO_EVENT:
				case YAML_STREAM_START_EVENT:
				case YAML_DOCUMENT_START_EVENT:
				case YAML_DOCUMENT_END_EVENT:
				break;
			}
		}

		/* The application is responsible for destroying the event object. */
		yaml_event_delete(&event);
	}
	yaml_parser_delete(&parser);

	::fclose(input);

    if (fail) return nullptr;
    
    return rootConfig;
}

	template <> 
	  const bool CoypuConfig::GetValue<bool> (const std::string & key, bool &out) {
	  auto i = _maps.find(key);
	  if (i != _maps.end()) {
	    out =  (*i).second->GetValue() == "true" || (*i).second->GetValue() == "TRUE";
	    return true;
	  }
	  
	  return false;
	}
	
	template <> 
	  const bool CoypuConfig::GetValue<int> (const std::string & key, int &out) {
	  auto i = _maps.find(key);
	  if (i != _maps.end()) {
	    out = std::atoi((*i).second->GetValue().c_str());
	    return true;
	  }
	  
	  return false;
	}
	
	template <> 
	  const bool CoypuConfig::GetValue<std::string> (const std::string & key, std::string &out) {
	  auto i = _maps.find(key);
	  if (i != _maps.end()) {
	    out =  (*i).second->GetValue();
	    return true;
	  }
	  
	  return false;
	}

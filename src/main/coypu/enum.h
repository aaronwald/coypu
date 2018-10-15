#ifndef _COYPU_ENUM_H
#define _COYPU_ENUM_H

// https://gist.github.com/saltares/8904478

#define SMARTENUM_VALUE(typeName, value) e##typeName##_##value, 
#define SMARTENUM_STRING(typeName, value) #value, 
#define SMARTENUM_DEFINE_ENUM(typeName, values) enum typeName { values(SMARTENUM_VALUE) e##typeName##_Count, };
#define SMARTENUM_DEFINE_NAMES(typeName, values) const char* typeName##Array [] = { values(SMARTENUM_STRING) };
 
#define SMARTENUM_DEFINE_GET_VALUE_FROM_STRING(typeName, name) \
  typeName get##typeName##FromString(const char* str)          \
  {                                                            \
    for (int i = 0; i < e##typeName##_Count; ++i)              \
      if (!strcmp(typeName##Array[i], str))                    \
        return (typeName)i;                                    \
    return e##typeName##_Count;                                \
  }
 
#define getStringFromEnumValue(typeName, value) typeName##Array[##value]
#define getEnumValueFromString(typeName, name)get##typeName##FromString(##name)

#endif

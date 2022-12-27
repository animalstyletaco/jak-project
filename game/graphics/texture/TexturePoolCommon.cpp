#include "TexturePoolDataTypes.h"

const char* texture_pool::goal_string(u32 ptr, const u8* memory_base) {
  static const char empty_string[] = "";
  if (ptr == 0) {
    return empty_string;
  }
  return (const char*)(memory_base + ptr + 4);
}

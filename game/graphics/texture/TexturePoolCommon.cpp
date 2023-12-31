#include "TexturePoolDataTypes.h"
#include "common/util/Assert.h"

const char* texture_pool::goal_string(u32 ptr, const u8* memory_base) {
  static const char empty_string[] = "";
  if (ptr == 0) {
    return empty_string;
  }
  return (const char*)(memory_base + ptr + 4);
}

void VramTextureSlotSet::remove_slot(u32 slot) {
  auto it = std::find(slots.begin(), slots.end(), slot);
  ASSERT(it != slots.end());
  slots.erase(it);
}

void VramTextureSlotSet::add_slot(u32 slot) {
  ASSERT(std::find(slots.begin(), slots.end(), slot) == slots.end());
  slots.push_back(slot);
}

void VramTextureSlotSet::add_slot_if_not_found(u32 slot_addr) {
  if (std::find(slots.begin(), slots.end(), slot_addr) == slots.end()) {
    slots.push_back(slot_addr);
  }
}

void VramTextureSlotSet::force_add_slot(u32 slot) {
  slots.push_back(slot);
}

void VramTextureSlotSet::force_add_mt4hh_slot(u32 slot) {
  mt4hh_slots.push_back(slot);
}


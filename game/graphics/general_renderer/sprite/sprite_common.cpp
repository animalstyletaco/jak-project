#include "sprite_common.h"

/*!
 * Does the next DMA transfer look like it could be the start of a 2D group?
 */
bool sprite_common::looks_like_2d_chunk_start(const DmaFollower& dma) {
  return dma.current_tag().qwc == 1 && dma.current_tag().kind == DmaTag::Kind::CNT;
}

/*!
 * Read the header. Asserts if it's bad.
 * Returns the number of sprites.
 * Advances 1 dma transfer
 */
u32 sprite_common::process_sprite_chunk_header(DmaFollower& dma) {
  auto transfer = dma.read_and_advance();
  // note that flg = true, this should use double buffering
  bool ok = verify_unpack_with_stcycl(transfer, VifCode::Kind::UNPACK_V4_32, 4, 4, 1,
                                      SpriteDataMem::Header, false, true);
  ASSERT(ok);
  u32 header[4];
  memcpy(header, transfer.data, 16);
  ASSERT(header[0] <= SPRITES_PER_CHUNK);
  return header[0];
}

/*!
 * Does the next DMA transfer look like the frame data for sprite distort?
 */
bool sprite_common::looks_like_distort_frame_data(const DmaFollower& dma) {
  return dma.current_tag().kind == DmaTag::Kind::CNT &&
         dma.current_tag_vifcode0().kind == VifCode::Kind::NOP &&
         dma.current_tag_vifcode1().kind == VifCode::Kind::UNPACK_V4_32;
}

#include "GeneralRenderer.h"

#include "common/log/log.h"

#ifdef _WIN32
void win_print_last_error(const std::string& msg, const std::string& renderer) {
  LPSTR lpMsgBuf{};

  FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&lpMsgBuf, 0, NULL);

  lg::error("[{}] {} Win Err: {}", renderer, msg, lpMsgBuf);
}

void copy_texture_to_clipboard(int width,
                               int height,
                               const std::vector<u32>& texture_data,
                               const std::string& renderer) {
  std::vector<u32> data(texture_data);

  // BGR -> RGB
  for (auto& px : data) {
    u8 r = px >> 0;
    u8 g = px >> 8;
    u8 b = px >> 16;
    u8 a = px >> 24;
    px = (a << 24) | (r << 16) | (g << 8) | (b << 0);
  }

  // Calculate the total size of the image data
  size_t image_size = data.size() * sizeof(u32);

  // BMP/DIB file header
  BITMAPINFOHEADER header;
  header.biSize = sizeof(header);
  header.biWidth = width;
  header.biHeight = height;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  header.biSizeImage = 0;
  header.biXPelsPerMeter = 0;
  header.biYPelsPerMeter = 0;
  header.biClrUsed = 0;
  header.biClrImportant = 0;

  // Open the clipboard
  if (!OpenClipboard(NULL)) {
    win_print_last_error("Failed to open the clipboard.", renderer);
    return;
  }

  // Empty the clipboard
  if (!EmptyClipboard()) {
    win_print_last_error("Failed to empty the clipboard.", renderer);
    CloseClipboard();
    return;
  }

  // Create a global memory object to hold the image data
  HGLOBAL hClipboardData = GlobalAlloc(GMEM_MOVEABLE, sizeof(header) + image_size);
  if (hClipboardData == NULL) {
    win_print_last_error("Failed to allocate memory for clipboard data.", renderer);
    CloseClipboard();
    return;
  }

  // Get a pointer to the global memory object
  void* pData = GlobalLock(hClipboardData);
  if (pData == NULL) {
    win_print_last_error("Failed to lock clipboard memory.", renderer);
    CloseClipboard();
    GlobalFree(hClipboardData);
    return;
  }

  // Copy the image data into the global memory object
  memcpy(pData, &header, sizeof(header));
  memcpy((char*)pData + sizeof(header), data.data(), image_size);

  // Unlock the global memory object
  if (!GlobalUnlock(hClipboardData) && GetLastError() != NO_ERROR) {
    win_print_last_error("Failed to unlock memory.", renderer);
    CloseClipboard();
    GlobalFree(hClipboardData);
    return;
  }

  // Set the image data to clipboard
  if (!SetClipboardData(CF_DIB, hClipboardData)) {
    win_print_last_error("Failed to set clipboard data.", renderer);
    CloseClipboard();
    GlobalFree(hClipboardData);
    return;
  }

  // Close the clipboard
  CloseClipboard();
  GlobalFree(hClipboardData);

  lg::info("Image data copied to clipboard successfully!");
}
#endif

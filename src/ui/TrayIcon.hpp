#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

// Forward-declare libdbus opaque types so we don't expose the header
struct DBusConnection;

namespace NeonSight {

class TrayIcon {
public:
  explicit TrayIcon(std::function<void()> onActivate);
  ~TrayIcon();

  bool Initialize();

private:
  std::function<void()> onActivate_;

  // SNI (StatusNotifierItem via raw libdbus-1)
  bool InitializeSNI();
  void SNILoop();
  void HandleSNIMessage(DBusConnection* conn, void* rawMsg);
  static int  SNIFilterFunc(DBusConnection* conn, void* rawMsg, void* data);

  DBusConnection*    sniConn_{nullptr};
  std::thread        sniThread_;
  std::atomic<bool>  sniRunning_{false};
  std::string        sniServiceName_;

  // Cached ARGB icon pixel data (32×32, ARGB big-endian per SNI spec)
  std::vector<uint8_t> iconPixels_;
  int iconW_{0}, iconH_{0};

  // XEmbed (_NET_SYSTEM_TRAY_S0) fallback
  bool InitializeXEmbed();
  void XEmbedLoop();

  std::thread        x11Thread_;
  std::atomic<bool>  x11Running_{false};
  void*              x11Display_{nullptr};
  unsigned long      x11Window_{0};
};

} // namespace NeonSight

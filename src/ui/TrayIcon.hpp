#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
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

  // Tray clicks are delivered on the DBus/X11 worker thread, but the actions
  // they trigger (show/hide the window) call GLFW, which is only safe on the
  // thread that owns the context (the main loop). So the worker thread only
  // records the requested action here...
  void EnqueueAction(std::function<void()> action);

  // ...and the main loop drains + runs it here. Call once per app-loop iteration.
  void PumpActions();

  // Called on the main thread when the user picks a tray context-menu item.
  // `id` is the stable item id we advertise in GetLayout (1..N). If unset, menu
  // selections are accepted as no-ops.
  void SetMenuActionCallback(std::function<void(int)> onMenuAction) { onMenuAction_ = std::move(onMenuAction); }

  // Invoke the registered menu-item callback for the given id (no-op if unset).
  // Called on the main thread; exposed publicly so the free-function D-Bus
  // handlers can dispatch through it.
  void InvokeMenuAction(int id) {
    if (onMenuAction_)
      onMenuAction_(id);
  }

private:
  std::function<void()> onActivate_;
  std::function<void(int)> onMenuAction_;

  // Thread-safe handoff from worker thread -> main thread.
  std::mutex actionMutex_;
  std::deque<std::function<void()>> pendingActions_;

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

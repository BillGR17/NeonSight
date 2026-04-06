#pragma once

#include <memory>
#include <string>
#include <vector>
#include <atomic>

namespace NeonSight {

class IInputManager;
class ICursorInjector;
class ConfigWindow;
class TrayIcon;

class App {
public:
  App();
  ~App();

  bool Initialize(bool forceX11 = false);

  // Pass in the global atomic flag controlling the application lifecycle
  void Run(const std::atomic<bool> &globalRunningFlag);

private:
  std::unique_ptr<IInputManager> inputManager_;
  std::vector<std::unique_ptr<ICursorInjector>> injectors_;
  std::unique_ptr<ConfigWindow> uiWindow_;
  std::unique_ptr<TrayIcon> trayIcon_;
  bool isRunning_{false};
};

} // namespace NeonSight

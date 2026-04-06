#include "App.hpp"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> g_Running{true};

void SignalHandler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    std::cout << "\nCaught signal " << signum << ". Shutting down gracefully..." << std::endl;
    g_Running = false;
  }
}

int main(int argc, char **argv) {
  // Register signal handlers for graceful exit (Ctrl+C)
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  bool forceX11 = false;
  if (argc > 1) {
    std::string arg = argv[1];
    if (arg == "--force-x11")
      forceX11 = true;
  }

  NeonSight::App app;
  if (app.Initialize(forceX11)) {
    // We pass the global running state to App so it knows when to terminate
    app.Run(g_Running);
  } else {
    std::cerr << "Initialization failed." << std::endl;
    return 1;
  }

  return 0;
}

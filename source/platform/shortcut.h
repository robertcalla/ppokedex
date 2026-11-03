#pragma once
#include <functional>
#include <string>

namespace plat {

// HOME menu shortcut (forwarder) that launches the .nro at `nroPath`.
bool shortcutSupported();
bool shortcutInstalled(const std::string& nroPath);
// `step` is called with a short description before each stage, for the progress screen.
bool installShortcut(const std::string& nroPath, std::string& error, const std::function<void(const char*)>& step);
bool removeShortcut(const std::string& nroPath, std::string& error);

} // namespace plat

#pragma once
#include <format>
#include <string>
#include <string_view>

namespace mcw::log {

enum class Level { Debug, Info, Warn, Error };

void write(Level level, std::string_view msg);
void setMinLevel(Level level);

template <class... A> void debug(std::format_string<A...> f, A&&... a) { write(Level::Debug, std::format(f, std::forward<A>(a)...)); }
template <class... A> void info(std::format_string<A...> f, A&&... a) { write(Level::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void warn(std::format_string<A...> f, A&&... a) { write(Level::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void error(std::format_string<A...> f, A&&... a) { write(Level::Error, std::format(f, std::forward<A>(a)...)); }

}  // namespace mcw::log

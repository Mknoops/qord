#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace Qord {
namespace Plugins {
namespace FakeProfileThemes {

// Profile theme colours (0xRRGGBB) as invisible text for a bio, and back
QString encode(quint32 primary, quint32 accent);
std::optional<std::pair<quint32, quint32>> decode(const QString &bio);

} // namespace FakeProfileThemes
} // namespace Plugins
} // namespace Qord

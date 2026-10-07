#pragma once

#include "Types.hpp"

namespace Qord {
namespace Plugins {

// Character-level diff of two message versions, ported from Equicord's
// MessageLogger (diffUtils.ts): fast paths for pure prefix/suffix changes,
// otherwise an LCS over tokens where custom emoji (<:name:id>), mentions
// (<@id>, <#id>) and code points are single tokens. Adjacent parts of the
// same kind are merged.
QList<DiffPart> createWordDiff(const QString &oldText, const QString &newText);

} // namespace Plugins
} // namespace Qord

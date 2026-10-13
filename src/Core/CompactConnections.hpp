#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace Acheron {
namespace Core {

// Qord: ShowConnections. Profiles show connected accounts as a row of icons
// with tooltips instead of a list; a GitHub account can be followed by its
// organizations.
struct CompactConnectionsOptions
{
    bool enabled = false;
    int iconSize = 32;
    int spacing = 6;
    std::function<void(const QString &githubName, std::function<void(const QStringList &logins)> done)> githubOrgs;
};

inline CompactConnectionsOptions &compactConnectionsOptions()
{
    static CompactConnectionsOptions options;
    return options;
}

} // namespace Core
} // namespace Acheron

#pragma once

namespace Acheron {
namespace Core {

// Qord: BetterFolders. Open server folders shown on a second rail beside the
// server list, and when folders open and close.
struct FolderSidebarOptions
{
    bool enabled = false; // the plugin is on
    bool sidebar = true; // open folders' servers on the second rail
    bool closeAllFolders = false; // when selecting a server not in a folder
    bool closeAllHomeButton = false; // when clicking the home button
    bool closeOthers = false; // when opening a folder
    bool closeServerFolder = false; // when selecting a server in that folder
    bool forceOpen = false; // when switching to a server of that folder
    bool keepIcons = false; // the server list keeps an open folder's servers
    enum class FolderIcon { Never, Always, MoreThanOneFolderExpanded };
    FolderIcon folderIcon = FolderIcon::Always; // above its servers on the second rail
};

inline FolderSidebarOptions &folderSidebarOptions()
{
    static FolderSidebarOptions options;
    return options;
}

} // namespace Core
} // namespace Acheron

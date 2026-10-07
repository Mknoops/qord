#pragma once

#include "Plugins/Types.hpp"

#include <QWidget>

#include <functional>

class QHBoxLayout;

namespace Acheron {
namespace UI {

// The buttons plugins add next to the message input (Qord). Rebuilt when a
// plugin asks for it, when plugins are toggled, and on channel switches.
class PluginChatButtons : public QWidget
{
    Q_OBJECT
public:
    using ContextProvider = std::function<Qord::Plugins::ChatContext()>;

    explicit PluginChatButtons(ContextProvider provider, QWidget *parent = nullptr);

    // Re-query the plugins (call after switching channels).
    void rebuild();

private:
    void scheduleRebuild();

    ContextProvider contextProvider;
    QHBoxLayout *layout;
    bool rebuildPending = false;
};

} // namespace UI
} // namespace Acheron

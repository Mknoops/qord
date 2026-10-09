#pragma once

#include <QMetaType>

#include "Discord/Enums.hpp"

namespace Acheron {
namespace Core {

// Qord: PlatformIndicators' colorMobileIndicator. Mobile users get the
// phone-shaped status whatever their status, not only when online.
inline bool &mobileIndicatorForAnyStatus()
{
    static bool any = false;
    return any;
}

struct PresenceBadge
{
    Discord::StatusType status = Discord::StatusType::OFFLINE;
    bool mobile = false;
    bool streaming = false;
};

} // namespace Core
} // namespace Acheron

Q_DECLARE_METATYPE(Acheron::Core::PresenceBadge)

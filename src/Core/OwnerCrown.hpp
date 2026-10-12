#pragma once

namespace Acheron {
namespace Core {

// Qord: ForceOwnerCrown. The member list shows the server owner's crown even
// in large servers, where it is otherwise left out.
inline bool &forceOwnerCrown()
{
    static bool force = false;
    return force;
}

} // namespace Core
} // namespace Acheron

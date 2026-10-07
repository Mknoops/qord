#pragma once

#include <QString>
#include <QUrl>

// Developer-only mock mode. Setting QORD_MOCK_DISCORD=http://127.0.0.1:<port>
// runs the client against tools/mock_discord.py instead of Discord: a separate
// profile ("Qord-Mock"), one fake account, and every Discord host (REST,
// gateway, CDN) pointed at the local server. Only loopback addresses are
// accepted, so it can never send anything to a real server.
namespace Qord::DevMock {

inline constexpr quint64 AccountId = 100000000000000001ull;

[[nodiscard]] bool enabled();
[[nodiscard]] QUrl baseUrl();
// Placeholder the mock server accepts; it is not a Discord token.
[[nodiscard]] QString token();
// Adds (or refreshes) the mock account in the current profile's database.
void installAccount();
// Rewrites Discord CDN/media URLs to the mock server; other URLs unchanged.
[[nodiscard]] QUrl rewrite(const QUrl &url);

} // namespace Qord::DevMock

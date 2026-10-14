# Defensive source review — 2026-10-14

## Scope and evidence

- Base `qord`: `1bca433b59904455d16571b323a28c01413de769` (tree `87cdcbb332cc8121c0c845797edd85f7ba64417d`). Resolved through GitHub and `git ls-remote`, then checked out locally.
- Open PR #16, `equicord-plugin-ports`: `f8636230ac4ea657d4fc9c9217b24ebe480db50f`. Its base was the same `qord` commit. Reviewed separately; no changes or merge were made to that PR.
- Fix branch: `codex/security-audit-fixes`, based directly on the above `qord` commit.
- Read `CONTRIBUTING.md`, `QORD.md`, `SECURITY.md`, relevant decisions and test/build definitions. The base tree contains no `AGENTS.md` or `CLAUDE.md` files.
- Three independent source-review passes covered plugins/link handling, gateway/voice/video, and release/storage/mock isolation. The primary reviewer integrated changes and reviewed their diffs. An additional static pass reviewed plugin network and logging changes.

The requested `security-audit` skill was unavailable in the installed cloud and executor skill catalogs and accessible skill directories. The supplied Windows skill path and earlier reference document were not accessible here. **This is a defensive source review, not a passed execution of that skill or a complete security certification.**

`confirmed_source` below means the code establishes the stated defect or missing control. It does not imply that an exploit, crash, credential transmission, or resource exhaustion was reproduced. Such runtime consequences remain `needs_validation`. No severity scores have been assigned.

## Changes in this branch

| ID | Evidence at reviewed base | Disposition and regression coverage |
|---|---|---|
| S01 | `VoiceEncryption.cpp` passes arbitrary-length `m_key` to fixed-key-size libsodium APIs; `VoiceClient::onSessionDescription` accepts it before sending silence. | `confirmed_source`, fixed with exact key/mode checks at both boundaries. Invalid key sizes, valid dummy round trips and authentication failures added. Actual out-of-bounds behavior: `needs_validation`. |
| S02 | `H264Rtp.cpp` accumulates same-timestamp NAL/FU input without size/count bounds; `packetize(..., 2)` has zero fragment stride. | `confirmed_source`, fixed with byte/packet/NAL limits and invalid-budget rejection. Rejection and keyframe recovery cases added. Resource exhaustion/infinite-loop reproduction: `needs_validation`. |
| S03 | `IngestThread.cpp` has unlimited queue and inflate accumulation; suffix recognition examines only one transport chunk. VoiceGateway text/binary buffers are unlimited. | `confirmed_source`, fixed with bounded queues/messages/frames, generation failure/reconnect and suffix accumulation. Split suffix, persistent stream, corrupt input and expansion-limit cases added. Reconnect timing and large-server compatibility: `needs_validation`. |
| S04 | Masked-link rendering in `Core/Markdown/Parser.cpp` accepts targets excluded by automatic links; `ChatView::openLink` sends them to desktop handlers. Confirmation URLs/filenames are interpolated as HTML. | `confirmed_source`, fixed with shared policy, sink checks and escaping. Malicious schemes and normal/generated links covered. Platform handler/UI consequences: `needs_validation`. |
| S05 | `SettingSpec::validate(Int)` converts an arbitrary JSON double to `qint64` before range checking. | `confirmed_source`, fixed by checking finiteness, integrality and range first. Extreme persisted JSON and boundary tests added. Sanitizer validation pending. |
| S06 | Gateway diagnostics serialize entire redacted payloads; message content and unknown credential fields survive the key denylist. | `confirmed_source`, fixed by logging numeric metadata only, including omission of free-form event names/close reasons. Test verifies arbitrary payload strings cannot enter metadata. Other log sites were sampled, not proven globally secret-free. |
| S07 | `QtNetworkService::send` allows cross-origin HTTPS redirects and checks response size only through progress notifications before `readAll()`. | `confirmed_source` policy gaps, fixed with same-origin redirects, credential-free URL admission and bounded decoded reads. Loopback 307 redirect and exact/over-limit buffering cases added. Actual Qt header/body forwarding and backend buffering behavior: `needs_validation`. |
| S08 | Mock account add/set/delete paths can invoke TokenStore's production service despite the separate application profile. | `confirmed_source`, fixed with guards in all six entry points. Test backend aborts if a keychain job is constructed; no actual keychain is linked into that test. Backend/UI behavior pending. |

Behavior decisions are recorded as 110–116. Decision 109 is reserved for PR #16's already-proposed release decision. No browser engine, JavaScript runtime, telemetry, or real credentials were introduced.

## Remaining findings and limits

These are unresolved by this PR and must not be represented as passed checks:

| ID | Status | Finding and next work |
|---|---|---|
| U01 | `confirmed_source` | `Core/ProxyConfig::toString()` includes the password, and `Storage/AccountRepository::{saveAccount,updateProxy}` persist it in SQLite. Percent encoding is not encryption. Implement keychain-backed proxy credentials with migration, account deletion and failure propagation; never silently strip a password or fall back to direct networking. Existing database/WAL/backups also need an explicit migration policy. |
| U02 | `confirmed_source`; wire effects `needs_validation` | `ProxyConfig::toQtProxy()` maps an HTTPS proxy to Qt's plain HTTP proxy and supplies credentials. Fail closed for unsupported HTTPS-proxy transport or use a TLS-capable proxy implementation; test authenticated proxy behavior without real credentials. |
| U03 | `confirmed_source`; full reachability `needs_validation` | `DevMock::rewrite()` returns unrecognized hosts unchanged. Mock rewriting is not an egress deny policy; direct curl/FFmpeg/desktop-open paths also require review. The keychain guard does not establish network containment. Use an OS network sandbox for all mock execution. |
| U04 | `confirmed_source` omissions | The base CI uses mutable action tags and unchecked/moving binary downloads. PR #16 adds action pins, archive hashes, tagged tooling and narrower workflow permissions. Those changes were inspected but not duplicated or merged. Hash provenance, action authenticity, runner/toolchain integrity and transitive supply chain remain unverified. |
| U05 | `needs_validation` | `MediaDecoder::open` restricts nested FFmpeg opens only on its proxied path. Unproxied protocol/nested-resource access and `CurlAvio` redirect policy need fixture-based testing and an explicit local/remote media policy. No deployed media probes were used. |
| U06 | `needs_validation` | Per-frame limits do not bound `VoiceClient`'s aggregate SSRC depacketizer map or the queued GUI payload backlog. Establish reachable peer control and workload-safe aggregate limits. |
| U07 | `needs_validation` | Most scoped plugin callbacks suppress work after disable, while some raw activity/session/relationship services rely on caller lifetime guards. No concrete current use-after-free was established. Stress disable/destruction with delayed fake-service replies. |

Native statically linked plugins are trusted native code; service permission checks do not provide process isolation against a malicious compiled plugin. Reviewed host paths intersect declared permissions with grants and constrain manifest IDs. Known secret settings are excluded from JSON and routed to the keychain (memory in mock mode). Future underscore-separated secret-key namespace collisions were considered but no current collision was demonstrated.

The SQL-injection hypothesis was rejected for the inspected queries: values use bindings, and the examined dynamically joined identifiers were numeric conversions/fixed schema names. This is not exhaustive SQL coverage. No immediate additional RTP extension out-of-bounds defect was confirmed in the inspected size checks. No TLS-verification-disabling option was found in the inspected common Discord curl setup; runtime TLS behavior and dependency configuration are unverified. Absence of telemetry was not established for every network path or dependency.

PR #16's identity/keychain rename, release workflow, packaging and compatibility diff were reviewed. Release builds select a commit SHA and publishing alone receives write permission. No confirmed PR16-only exploitable defect was established. Windows/macOS package execution, signing/provenance and migration behavior were not tested. DAVE, FFmpeg, Qt, libsodium and other vendored dependency internals were not exhaustively audited.

## Validation record

Only source inspection and non-target Git operations were performed. `git diff --check` passed during review; no build or regression test is claimed to pass.

Cloud isolation was explicitly probed before any target-controlled execution:

- Bubblewrap failed: `Can't read /proc/sys/kernel/overflowuid: No such file or directory`.
- User/network namespace setup failed: `unshare: cannot open /proc/self/uid_map: No such file or directory`.
- These probes used a cleared environment and a short timeout. They did **not** establish a compliant sandbox.
- Git and GCC were present; CMake, Ninja, qmake and pkg-config were not found on PATH. Qt development availability was not established.

Accordingly, no repository script, build system, executable, test, local mock server, real Discord account/token, or deployed probe was run. No CI job was manually dispatched as a substitute for the required sandbox. If GitHub automatically runs repository checks after PR creation, those are separate from this review's validation record.

Before merging, run the new and existing Qt tests on Qt 5.15 and a supported Qt 6 build, tooling tests and generated-doc checks, under OS-enforced external-network denial, an empty allowlisted environment, read-only source/toolchain, scratch-only writes and explicit time/memory/process limits. Use dummy data and loopback fixtures only. Also perform visible-link/mock UI checks and targeted sanitizer/thread tests for ingestion and media. The new limits and reconnect behavior require realistic large-session compatibility validation.

# Device and usage statistics: wire protocol (P-000)

The contract between the client in this repository and the statistics service. It holds no server address or credential: the origin is injected at packaging time (`DMDiagnosticUploadOrigin` on iOS, `DM_DIAGNOSTIC_ORIGIN` on Android) and is never in the source. Field names live in one file, `dm/host/dm_telemetry_fields.h`.

Changing this protocol takes a new `P-<three digits>` number: this document first, then the service, then the client.

M0 sends only `launch`. `session_end` and `perf_window` wait for capability negotiation and their own milestones.

## Transport

- `GET <origin>/telemetry/v1/challenge`, `POST <origin>/telemetry/v1/events`.
- The POST body is JSON with exactly five fields: `public_key`, `challenge`, `upload_id` (32 lower-case hex digits), `payload`, `signature`. Binary values are base64url without padding. M0 does not compress.
- The key is a separate Ed25519 identity used only for statistics. Turning the switch off deletes it.
- Signed text: `D2R-TELEMETRY-V1\n<challenge>\n<upload_id>\n<lower-case hex sha256(payload)>\n`, where `payload` is the raw batch JSON bytes.
- Batch: `{"schema":1,"events":[…]}`, 1–20 events. The client keeps a batch ≤ 8 KiB; the protocol limits are 32 KiB for the payload and 128 KiB for the envelope.
- Idempotency: `(key_digest, upload_id)` for a batch, `(key_digest, event_id)` for an event. `seq` only shows order, gaps and resets. After a lost response the client may send the same events under a new `upload_id`; the same `event_id` with the same body answers `duplicate`, not 409.

## Common head and `launch`

`schema`, `event_id`, `type`, `client_ts`, `session_id`, `seq`, `mono_s`, `platform` (`ios` / `android` / `mac`), `app_version` (`[0-9A-Za-z.+-]{1,20}`), `build`, `source_revision`, `channel`, `device_code`, `consent_version`.

`launch` adds `model`, `os_version`, `mem_tier`, `locale`, `prev_exit`, `auto_crash`, `queue_dropped` (`launch`, `session_end`, `perf_window`: events dropped by queue limits or expiry), `settings` (31 keys), and when known `pack_source`, `soc`, `gpu_driver`, `mem_limit_mib`, `prev_session_id`, `prev_exit_arg`.

- `device_code`: `identifierForVendor` on iOS, `ANDROID_ID` on Android, a local random UUID on the Mac host. Never rotated; never the UDID, IMEI, serial, MAC or advertising ID. A short `ANDROID_ID` is left-padded with `0` to 16 digits and lower-cased.
- `model` on Android is `[A-Za-z0-9 ._+-]{1,40}` or `unknown`: other characters become spaces, the result is cut to 40, and an empty result is `unknown`. iOS and Mac keep their comma (`iPhone18,3`).
- On Android `upscale_selected` and `upscale_effective` are always `off` and `icloud` is always `0`: the platform has neither, and these values do not mean the player turned them off.

## Answers

A challenge answer carries `challenge` (43 base64url characters) and `capabilities`. M0's capability is `telemetry-launch-v1`. No `capabilities` field means launch only; an empty array means send nothing; unknown names are ignored. Events the service did not announce stay in the local queue.

A submit answer carries `results`, one item per event: `event_index`, `event_id`, `status` (`accepted`, `quarantined`, `duplicate`, `rejected`). Items are matched by `event_index`, not by array position.

| Answer | Client |
|---|---|
| 2xx with complete `results` | Each event leaves the queue; `rejected` are dropped and counted; the rest are counted by status |
| 2xx with missing or unverifiable `results` (index out of range, wrong ID, unknown status) | The batch stays; network backoff |
| 400 with complete `results` | As the first row |
| 400 without verifiable `results`, 411, 413, 415, other 4xx | The batch stays, nothing more this launch, one more try next launch; the same status twice on one build stops sending until events expire or the build changes |
| 403 | The batch stays; network backoff; a new challenge on the next try, never in place |
| 408, other 5xx, network errors | The batch stays; network backoff |
| 409 | The batch is dropped and counted; no retry and no splitting |
| 429, 503 | The batch stays; follow `Retry-After` (60 s when absent); over 600 s waits for the next launch; capped at 7 days |

Network backoff is 1 min, 5 min, 30 min, 2 h, 12 h with ±20% jitter, at most 6 per launch. Counts of refused and conflicting events stay in the client's local state and log; they are not part of `queue_dropped`.

## Timing and limits

Zero requests, signing or compression in a playable world. Sending happens after 5 seconds on the main menu or character select. The local queue holds 64 events, 128 KiB, 7 days; over the limit the oldest go in the order `perf_window`, `session_end`, `launch`. Per install and UTC+8 day: 8 requests (a challenge and a submit are one each), 64 KiB, and 16 KiB on metered networks, where `perf_window` waits for Wi-Fi.

## Server switch

The update manifest may carry `telemetry: {"enabled": bool, "pause_until": ISO 8601, "reason": short code}`; absent means enabled. While paused events still queue, nothing is uploaded and no notice is shown. An unreachable manifest keeps the cached state; a first install with no cache counts as enabled, and without the player's consent nothing is sent either way. The client also reads Unix seconds in `pause_until`, but the service does not send them. A 503 `uploads_paused` stops only the channel that received it; the manifest switch covers both statistics and crash reports. The switch can only reduce sending.

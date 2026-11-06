# Diagnostic upload and automatic crash reports

[简体中文](DIAGNOSTICS.zh-CN.md)

This page tells players what these two features upload, when, and how to turn them off, and records the client-server contract. Public builds get the address at build time; the source contains no server address or key. A build without an address, including one you compile yourself, never goes online for this.

## Two entry points

| | Share Diagnostics → Upload | Auto-report crashes |
|---|---|---|
| Where | Pause menu | Options → Custom (off by default) |
| When it sends | Each time you press Upload in the box | After you turn it on and agree in the consent box, on a launch that finds the previous run exited abnormally (crash, hang, out of memory, unknown exit) |
| Feedback | The box shows the result and report number | One line on the character-select page for about 20 s |

Being reclaimed by the system in the background, or swiping the app away yourself, is not an abnormal exit. Development and test runs carrying test environment variables do not report automatically.

## What is uploaded

A report is one text plus six metadata fields. The automatic report's text contains, in order:

- A first line naming the kind: `D2R Mobile Plus crash report (automatic, full)` or `(automatic, summary)`.
- One crash fence line such as `fence cause=crash arg=11 streak=2 hist=11` (cause, signal or seconds, streak).
- GPU name (iOS; on Android it is in the log's `NVN caps` line).
- Version, build, model, OS version, battery and Low Power Mode, thermal state, free storage, uptime, physical memory.
- Graphics settings and the current value of each Options → Custom switch.
- The tails of the previous and current run's logs, at most 128 KiB each (iOS); on Android at most 256 KiB overall, keeping the beginning and the end.
- The system crash diagnostic: on iOS the newest MetricKit payload, which may arrive a launch later, so a report sometimes lacks it; on Android the reason and status of the 5 latest `ApplicationExitInfo` entries, without a crash stack.

The six metadata fields are `app_version`, `build`, `source_revision`, `model`, `os_version`, `locale`. Android currently sends `model` as `unknown`; the real model is in the log head.

## What is not uploaded

- Save contents, screenshots, accounts, contacts, location.
- Device name, UDID, container UUID, team ID, user name, local paths, e-mail addresses, IP addresses, URLs, character names. These are removed on the device before upload. The server removes them again, but the client does not rely on that.
- Whole lines mentioning tokens, tickets, passwords or private keys.

Scrubbing is rule-based and cannot guarantee it removes every piece of personal information that might appear in a log. The report is kept as `dm_crash_report.payload` (on iOS in this app's folder in the Files app, on Android in the app's private directory) until it is uploaded or given up. iOS players can open that file to check exactly what will be sent.

## Identity, frequency and retention

- Each installation creates an Ed25519 key used only for diagnostics, separate from any co-op identity. iOS keeps it in the Keychain (this device only, not synchronized); Android keeps it in the no-backup private directory. The server can tell that two reports came from one installation, but the key is not tied to an account.
- At most 3 attempts per launch (starting at least 20 s after launch, after 5 s on the menu and up to 60 s waiting for this launch's update manifest, then 60 s and 300 s later, or longer if the server asks), 3 launches, 6 attempts overall and 7 days of storage; then the report is deleted with one notice. An explicit refusal is not retried.
- On iOS, in Low Data Mode automatic reports send only the scrubbed summary (at most 16 KiB), not the full report. Manual uploads are not limited this way.
- Turning the switch off and pressing Apply cancels a running upload and deletes the pending report.
- The server states its retention before each upload, currently 7 days; only authorized maintainers can read reports.

## When, and on which network

Reports go out only after 5 s on the main menu or character select; nothing is requested during a game. If a report is due while you are in a game, it is packed on the device and sent once you are back on the menu.

On cellular, Low Data Mode and metered networks only a summary goes out: the fence state, version, settings and key readings from the start of the report, at most 16 KiB, with `D2R Mobile Plus crash report (automatic, summary)` as its first line. The full report (with log tails and the system crash diagnostic) goes only on Wi-Fi, with `(automatic, full) summary_of=<the summary's upload_id>` as its first line and its own `upload_id`. There is no further switch in Settings. If Wi-Fi never comes after the summary, the full report is deleted after 7 days of storage.

## Server switch

The update manifest may carry `telemetry`: `{"enabled": false, "pause_until": <Unix seconds, optional>, "reason": "<optional>"}`, on by default. When it reads `false`, that launch uploads no report and shows no notice; a report for an abnormal exit is still packed and kept on the device, within the 7-day limit, and goes out after resuming. If the manifest cannot be fetched, the last value is kept. The switch can only make clients send less, never more, so an unsigned manifest is acceptable, and the player's own switch remains the precondition. A 503 + Retry-After from the upload endpoint counts as a pause too: no retry budget spent, probed again after at least 300 s, at most 3 probes per launch. A manual upload from the pause menu is not held back by this switch; the server can simply answer 503.

## Device and usage statistics

Options → Custom → "Device and usage statistics", off by default. A notice appears once on first reaching the main menu: Agree turns this switch and "Auto-report crashes" on, Decline leaves both off, with normal play and no requests at all. Afterwards each one can be toggled in Options with no further box; turning it off deletes the data not yet sent, the sequence number and the reporting identity on the spot, and no further requests follow.

- **Recorded now**: one entry per launch: time, version / build / source commit, platform, model, OS version, memory tier, device ID (iOS identifierForVendor, Android ANDROID_ID; never the UDID, IMEI or advertising ID), system locale, channel, how the previous run ended, drop counters and the game settings at that moment (the switches, frame rate, upscaling, ray tracing, input method, language, font). The per-session summary, weekly performance window and entry-point counters named in the notice come in later versions and are not collected in this one.
- **Never recorded**: device name, user name, local paths, accounts, contacts, location, chat, character names and saves. Every field passes a whitelist before it is written; anything else becomes the default or is left out.
- **When it is sent**: stored locally first (`dm_tele_queue.tsv`, at most 64 entries / 128 KiB / 7 days, the oldest dropped and counted beyond that), sent only after 5 s on the main menu / character select, at most 8 KiB per batch; no requests in a game, and a request in flight is cancelled on entering one. Cellular, Low Data Mode and metered networks only carry launch and session events, at most 16 KiB a day; each device sends at most 8 requests / 64 KiB a day.
- **Failures**: network errors and 403 back off 1 min / 5 min / 30 min / 2 h / 12 h, at most 6 per launch; 429 / 503 follow `Retry-After`, beyond 10 minutes waiting for the next launch; an event the server refuses or a conflicting batch is dropped and counted; a malformed request stays queued and is tried once more next launch. Details and receipts: `docs/TELEMETRY_PROTOCOL.md`.
- **Server switch**: the same one as above. While paused, events stay on the device, nothing is uploaded and nothing is shown; after it resumes, what has not expired is sent. The switch cannot override the player's own switch.

## Protocol

Prefix `/diagnostics/v1`. It shares one server contract with the co-op line (section 7 of `docs/COOP_PROTOCOL.md` on that line) and adds no fields.

1. `GET /diagnostics/v1/challenge` returns `{"challenge", "expires_in", "max_payload_bytes", "retention_days"}`. A challenge is single use and fetched again for every attempt.
2. The payload is UTF-8 JSON: `{"schema": 1, "metadata": {six fields}, "log": <scrubbed text>}`, at most 600 KiB.
3. Signed bytes: `"D2R-DIAGNOSTICS-V1\n" + challenge + "\n" + upload_id + "\n" + hex(sha256(payload)) + "\n"`.
4. `POST /diagnostics/v1/reports` with exactly five body fields: `public_key`, `challenge`, `upload_id` (32 hex digits), `payload` (base64url), `signature`. Resending the same `upload_id` is idempotent.

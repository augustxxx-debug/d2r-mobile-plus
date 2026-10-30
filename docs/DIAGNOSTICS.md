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

- A fixed first line, `D2R Mobile Plus crash report (automatic)`.
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
- At most 3 attempts per launch (20 s after launch, then 60 s and 300 s later, or longer if the server asks), 3 launches, 6 attempts overall and 7 days of storage; then the report is deleted with one notice. An explicit refusal is not retried.
- On iOS, automatic uploads do not use Low Data Mode networks. Manual uploads are not limited this way.
- Turning the switch off and pressing Apply cancels a running upload and deletes the pending report.
- The server states its retention before each upload, currently 7 days; only authorized maintainers can read reports.

## Protocol

Prefix `/diagnostics/v1`. It shares one server contract with the co-op line (section 7 of `docs/COOP_PROTOCOL.md` on that line) and adds no fields.

1. `GET /diagnostics/v1/challenge` returns `{"challenge", "expires_in", "max_payload_bytes", "retention_days"}`. A challenge is single use and fetched again for every attempt.
2. The payload is UTF-8 JSON: `{"schema": 1, "metadata": {six fields}, "log": <scrubbed text>}`, at most 600 KiB.
3. Signed bytes: `"D2R-DIAGNOSTICS-V1\n" + challenge + "\n" + upload_id + "\n" + hex(sha256(payload)) + "\n"`.
4. `POST /diagnostics/v1/reports` with exactly five body fields: `public_key`, `challenge`, `upload_id` (32 hex digits), `payload` (base64url), `signature`. Resending the same `upload_id` is idempotent.

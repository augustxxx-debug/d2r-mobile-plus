// 暂停菜单「分享诊断日志 → 上传诊断报告」的网络、签名与打包部分（线上约定见 docs/DIAGNOSTICS.zh-CN.md）。
// 界面与状态机在公共宿主 dm/host/dm_diagup.c（游戏主线程）；这里只在后台线程做一次「尝试」：取挑战 → 必要时打包 → 签名 → 上传 → 按回执 / 错误码归类，结束时回调一次。
//   报告（upload_id + 载荷字节）按任务号留在内存里：同一任务的重试沿用同一 upload_id、同一载荷，只换新挑战重签；新任务（玩家重新点「上传」）才重新打包。
//   载荷整份（不只是 log）不超过挑战给的 max_payload_bytes：超了就让外壳按更小的每份日志上限重新收集（外壳收集后整份过 dm_diagnostic_scrub_names），绝不发超限请求。
//   诊断专用的 Ed25519 安装身份（与联机身份分开）：iOS 存钥匙串（本机专用、不同步、不进备份迁移）；Mac 开发宿主存 0600 文件（未签名的命令行宿主读钥匙串会弹授权框）。私钥不进日志。
// Network, signing and packaging for the pause menu's "Share Diagnostics → Upload report" (wire contract: docs/DIAGNOSTICS.md).
// The UI and state machine live in the shared host dm/host/dm_diagup.c (game main thread); this file runs one "attempt" on a background thread: challenge → pack if needed → sign → upload → classify the receipt / error, then calls back once.
//   The report (upload_id + payload bytes) stays in memory per job: retries of one job keep the same upload_id and payload and only re-sign with a fresh challenge; a new job (the player pressing Upload again) packs anew.
//   The whole payload (not just the log) stays within the challenge's max_payload_bytes: if it does not fit, the shell re-collects with a smaller per-log limit (the shell scrubs the whole text with dm_diagnostic_scrub_names), never sending an oversized request.
//   A diagnostics-only Ed25519 installation identity (separate from the co-op one): Keychain on iOS (this device only, not synchronized, not migrated by backups); a 0600 file on the Mac dev host (an unsigned command-line host would get a Keychain prompt). The private key is never logged.
import CryptoKit
import Darwin
import Foundation
import Network
import Security

/// 结果分类，与 dm/host/dm_diagup.h 的 DM_DIAGUP_* 一致 | outcome classes, kept in step with DM_DIAGUP_* in dm/host/dm_diagup.h
public enum DmDiagOutcome: Int32, Sendable {
    case cancelled = 0     // 外壳取消（旧任务），宿主丢弃 | cancelled by the shell (stale), dropped by the host
    case ok = 1            // 收到并校验了服务端回执 | a verified server receipt
    case unconfirmed = 2   // 请求可能已到服务端但没拿到确定答复（超时 / 断线 / 响应丢失 / 网关错） | the request may have reached the server without a definite answer
    case unavailable = 3   // 报告没有发出或服务端明确没收（离线 / DNS / TLS / 服务忙） | the report was not sent, or the server explicitly did not take it
    case maintenance = 4   // diagnostic_maintenance
    case rateLimited = 5   // rate_limited（频率或每日额度） | rate or daily quota
    case tooLarge = 6      // 缩小后仍超限 | still over the limit after shrinking
    case rejected = 7      // 其他 4xx：服务端无法接收（带错误码） | other 4xx (with the error code)
    case conflict = 8      // upload_conflict：同一 upload_id 不同载荷 = 客户端缺陷 | same upload_id, different payload = a client defect
    case prepare = 9       // 本机准备失败（收集 / 身份） | local preparation failed (collection / identity)
    case configuration = 10 // 没有合法的上传地址 | no valid upload origin
    case summarySent = 11  // 只有自动上报：蜂窝下摘要版已送达 | automatic only: the summary arrived on a metered network
    case waitWifi = 12     // 只有自动上报：这次没发，等 Wi-Fi | automatic only: nothing sent, waiting for Wi-Fi
}

public struct DmDiagResult: Sendable, Equatable {
    public var outcome: DmDiagOutcome
    public var detail: String          // 成功 = report_id；失败 = 服务端错误码或本机原因 | success = report_id; failure = server error code or local reason
    public var expiresAt: Int64 = 0
    public var duplicate = false
    public var maybeDelivered = false  // 这份报告之前有一次请求结果不明：现在的失败不等于没送到 | an earlier request of this report had no definite answer
    public var retryAfter: Int32 = 0
    public var retentionDays: Int32 = 0
}

/// 挑战（每次签名上传尝试都重取，单次使用、会过期；数值一律以响应为准） | challenge: fetched for every signed attempt, single use, expires; all values come from the response
public struct DmDiagChallenge: Sendable {
    public let challenge: String
    public let expiresIn: Int
    public let maxPayloadBytes: Int
    public let retentionDays: Int
}

public struct DmDiagReport: Sendable {
    public let uploadID: String
    public let payload: Data
    public var maybeDelivered = false
}

public protocol DmDiagKeyStore: Sendable {
    func loadOrCreate() throws -> Curve25519.Signing.PrivateKey
}

public enum DmDiagError: Error, Equatable {
    case identity(String)
    case collect
    case tooLarge
}

/// 钥匙串：本机专用、不同步，与联机身份不同的服务名 | Keychain: this device only, not synchronized, a service name distinct from the co-op identity
public final class DmDiagKeychainStore: DmDiagKeyStore, @unchecked Sendable {
    public static let shared = DmDiagKeychainStore()
    public static let telemetry = DmDiagKeychainStore(service: "org.d2rmobile.telemetry.installation.v1")   // 统计用另一把：与崩溃报告不关联；玩家关统计时删掉 | statistics use another key: not linkable to crash reports; deleted when the player turns statistics off
    private let service: String
    private let account = "ed25519"
    private let lock = NSLock()
    init(service: String = "org.d2rmobile.diagnostics.installation.v1") { self.service = service }
    public func delete() {
        lock.lock(); defer { lock.unlock() }
        SecItemDelete([kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: account, kSecAttrSynchronizable as String: false] as CFDictionary)
    }
    public func loadOrCreate() throws -> Curve25519.Signing.PrivateKey {
        lock.lock(); defer { lock.unlock() }
        let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                    kSecAttrAccount as String: account, kSecAttrSynchronizable as String: false,
                                    kSecReturnData as String: true, kSecMatchLimit as String: kSecMatchLimitOne]
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecSuccess, let raw = result as? Data {
            guard raw.count == 32, let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: raw) else { throw DmDiagError.identity("keychain_malformed") }
            return key
        }
        guard status == errSecItemNotFound else { throw DmDiagError.identity("keychain_\(status)") }
        let key = Curve25519.Signing.PrivateKey()
        let add: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                  kSecAttrAccount as String: account, kSecAttrSynchronizable as String: false,
                                  kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
                                  kSecValueData as String: key.rawRepresentation]
        let added = SecItemAdd(add as CFDictionary, nil)
        guard added == errSecSuccess else { throw DmDiagError.identity("keychain_\(added)") }
        return key
    }
}

/// 文件：32 字节原始私钥，0600、属主是自己、不跟符号链接；格式不对就拒用，不覆盖 | file: 32 raw private-key bytes, 0600, owned by us, no symlinks; anything else is refused, never overwritten
public final class DmDiagFileKeyStore: DmDiagKeyStore, @unchecked Sendable {
    private let path: String
    private let lock = NSLock()
    public init(path: String) { self.path = path }
    public func loadOrCreate() throws -> Curve25519.Signing.PrivateKey {
        lock.lock(); defer { lock.unlock() }
        var st = stat()
        if lstat(path, &st) == 0 {
            guard (st.st_mode & S_IFMT) == S_IFREG, st.st_uid == getuid(), (st.st_mode & 0o077) == 0, st.st_size == 32 else { throw DmDiagError.identity("key_file_unsafe") }
            let fd = open(path, O_RDONLY | O_NOFOLLOW); guard fd >= 0 else { throw DmDiagError.identity("key_file_open") }
            defer { close(fd) }
            var raw = [UInt8](repeating: 0, count: 32)
            guard read(fd, &raw, 32) == 32, let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: raw) else { throw DmDiagError.identity("key_file_read") }
            return key
        }
        guard errno == ENOENT else { throw DmDiagError.identity("key_file_stat") }
        let dir = (path as NSString).deletingLastPathComponent
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        let key = Curve25519.Signing.PrivateKey()
        let fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0o600); guard fd >= 0 else { throw DmDiagError.identity("key_file_create") }
        defer { close(fd) }
        let raw = [UInt8](key.rawRepresentation)
        guard write(fd, raw, raw.count) == raw.count, fsync(fd) == 0 else { unlink(path); throw DmDiagError.identity("key_file_write") }
        return key
    }
}

private final class DmDiagNoRedirect: NSObject, URLSessionTaskDelegate, @unchecked Sendable {
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
}

public final class DmDiagUploader: @unchecked Sendable {
    public static let metadataKeys = ["app_version", "build", "source_revision", "model", "os_version", "locale"]
    private static let metadataRules: [String: (String, String)] = [   // 与服务端同一套格式；不合格的换成允许的缺省值 | the same formats as the server; invalid values become an allowed default
        "app_version": ("^[A-Za-z0-9.+_-]{1,64}$", "unknown"), "build": ("^[A-Za-z0-9.+_-]{1,64}$", "unknown"),
        "source_revision": ("^(?:[0-9a-f]{7,40}|unknown)$", "unknown"),
        "model": ("^(?:(?:iPhone|iPad|iPod|Mac|VirtualMac)[0-9]+,[0-9]+|arm64|x86_64|unknown|simulator)$", "unknown"),
        "os_version": ("^[0-9]{1,3}(?:\\.[0-9]{1,3}){1,3}$", "0.0"), "locale": ("^[A-Za-z0-9_-]{1,24}$", "und")]
    private let origin: URL
    private let session: URLSession
    private let apiBase: String
    private let delegate = DmDiagNoRedirect()

    /// 地址只认 https；http 只许本机回环（本地联调）。不带路径 / 账号 / 查询 | https only; plain http only for loopback (local integration). No path / credentials / query
    public static func validOrigin(_ raw: String) -> URL? {
        guard let parts = URLComponents(string: raw), let host = parts.host, !host.isEmpty, parts.user == nil, parts.password == nil,
              parts.query == nil, parts.fragment == nil, parts.path.isEmpty || parts.path == "/", let url = parts.url else { return nil }
        if parts.scheme == "https" { return url }
        if parts.scheme == "http", ["127.0.0.1", "::1", "[::1]"].contains(host) { return url }
        return nil
    }

    public init?(origin raw: String, configuration: URLSessionConfiguration? = nil, apiBase: String = "diagnostics/v1/") {
        guard let url = Self.validOrigin(raw) else { return nil }
        origin = url
        self.apiBase = apiBase
        let config = configuration ?? URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 15
        config.timeoutIntervalForResource = 30
        config.waitsForConnectivity = false   // 离线立刻失败，不在后台等网 | fail at once when offline instead of waiting in the background
        config.httpCookieStorage = nil; config.httpShouldSetCookies = false
        config.urlCache = nil; config.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        config.httpMaximumConnectionsPerHost = 1
        session = URLSession(configuration: config, delegate: delegate, delegateQueue: nil)
    }
    deinit { session.invalidateAndCancel() }

    // ---- 编码 | encoding ----
    public static func base64URL(_ data: Data) -> String {
        data.base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }
    public static func decodeBase64URL(_ s: String) -> Data? {   // 只认规范编码：无填充、再编一次要逐字相同 | canonical only: unpadded, and re-encoding must match exactly
        guard s.range(of: "^[A-Za-z0-9_-]+$", options: .regularExpression) != nil else { return nil }
        var b = s.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        b += String(repeating: "=", count: (4 - b.count % 4) % 4)
        guard let d = Data(base64Encoded: b), base64URL(d) == s else { return nil }
        return d
    }
    public static func hex(_ data: some Sequence<UInt8>) -> String { data.map { String(format: "%02x", $0) }.joined() }
    public static func newUploadID() -> String {
        var raw = [UInt8](repeating: 0, count: 16)
        if SecRandomCopyBytes(kSecRandomDefault, raw.count, &raw) != errSecSuccess { raw = (0..<16).map { _ in UInt8.random(in: 0...255) } }
        return hex(raw)
    }
    /// 签名内容逐字节：前缀 + 挑战 + upload_id + 载荷 SHA-256 小写十六进制，各接一个 \n | the exact signed bytes
    public static func transcript(challenge: String, uploadID: String, payload: Data) -> Data {
        Data("D2R-DIAGNOSTICS-V1\n\(challenge)\n\(uploadID)\n\(hex(SHA256.hash(data: payload)))\n".utf8)
    }
    public static func cleanMetadata(_ input: [String: String]) -> [String: String] {
        var out: [String: String] = [:]
        for key in metadataKeys { let (rule, fallback) = metadataRules[key]!; let v = input[key] ?? ""
            out[key] = v.range(of: rule, options: .regularExpression) != nil ? v : fallback }
        return out
    }
    /// 服务端拒收 \t \r \n 以外的控制字符（整份 invalid_log）：换成「?」 | the server refuses control characters other than tab / CR / LF: replace them with "?"
    public static func cleanLog(_ text: String) -> String {
        String(String.UnicodeScalarView(text.unicodeScalars.map { ($0.value < 32 && $0 != "\t" && $0 != "\r" && $0 != "\n") || $0.value == 0x7f ? "?" : $0 }))
    }
    public static func payload(metadata: [String: String], log: String) throws -> Data {
        try JSONSerialization.data(withJSONObject: ["schema": 1, "metadata": cleanMetadata(metadata), "log": cleanLog(log)],
                                   options: [.sortedKeys, .withoutEscapingSlashes])
    }
    /// 打一份不超过 limit 字节的报告：collect(每份日志上限) 交回整份已脱敏文本；超了按超出比例缩小上限重收，到 2 KiB 还超就放弃 | pack a report within limit bytes
    public static func buildReport(limit: Int, metadata: [String: String], collect: (Int) -> String?) throws -> DmDiagReport {
        var perLog = min(limit, 1 << 20)
        for _ in 0..<10 {
            guard let text = collect(perLog) else { throw DmDiagError.collect }
            let body = try payload(metadata: metadata, log: text)
            if body.count <= limit { return DmDiagReport(uploadID: newUploadID(), payload: body) }
            let next = min(perLog - 1024, Int(Double(perLog) * Double(limit) / Double(body.count) * 0.85))
            if next < 2048 { break }
            perLog = next
        }
        throw DmDiagError.tooLarge
    }

    // ---- HTTP ----
    struct Reply { let status: Int; let body: Data; let retryAfter: Int32 }
    private func send(_ method: String, _ path: String, body: Data?) async throws -> Reply {
        var req = URLRequest(url: origin.appendingPathComponent(apiBase + path))
        req.httpMethod = method
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        req.setValue("D2R-Mobile-Plus-Diagnostics/1", forHTTPHeaderField: "User-Agent")
        if let body { req.setValue("application/json", forHTTPHeaderField: "Content-Type"); req.httpBody = body }
        let (bytes, response) = try await session.bytes(for: req)
        defer { bytes.task.cancel() }
        guard let http = response as? HTTPURLResponse else { throw URLError(.badServerResponse) }
        var data = Data()
        for try await byte in bytes { guard data.count < 16 * 1024 else { throw URLError(.badServerResponse) }; data.append(byte) }
        var after: Int32 = 0
        if let h = http.value(forHTTPHeaderField: "Retry-After"), let v = Int32(h.trimmingCharacters(in: .whitespaces)) { after = v }
        return Reply(status: http.statusCode, body: data, retryAfter: after)
    }
    private static func object(_ data: Data) -> [String: Any]? { (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] }
    private static func errorCode(_ data: Data) -> String? {
        guard let code = object(data)?["error"] as? String, code.range(of: "^[a-z0-9_]{1,64}$", options: .regularExpression) != nil else { return nil }
        return code
    }
    private static func integer(_ v: Any?) -> Int? {   // 只认 JSON 整数（不认布尔、小数） | JSON integers only (no booleans, no fractions)
        guard let n = v as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID(), !CFNumberIsFloatType(n) else { return nil }
        return n.intValue
    }
    static func retryAfter(_ reply: Reply) -> Int32 {
        var v = reply.retryAfter
        if v <= 0, let r = integer(object(reply.body)?["retry_after"]) { v = Int32(clamping: r) }
        return v <= 0 ? 60 : min(v, 3600)
    }
    /// 连接没建成（请求肯定没到服务端）的网络错误 → 原因码；其余网络错误按「结果不明」 | network errors where the connection never formed → reason code; the rest count as indeterminate
    static func notSentReason(_ error: Error) -> String? {
        guard let e = error as? URLError else { return nil }
        if e.networkUnavailableReason == .constrained || e.networkUnavailableReason == .expensive { return "data_restricted" }
        switch e.code {
        case .notConnectedToInternet: return "offline"
        case .dataNotAllowed, .internationalRoamingOff, .callIsActive: return "data_restricted"
        case .cannotFindHost, .dnsLookupFailed: return "dns"
        case .cannotConnectToHost: return "connect"
        case .secureConnectionFailed, .serverCertificateUntrusted, .serverCertificateHasBadDate, .serverCertificateNotYetValid,
             .serverCertificateHasUnknownRoot, .clientCertificateRejected, .clientCertificateRequired, .appTransportSecurityRequiresSecureConnection: return "tls"
        default: return nil
        }
    }
    static func isCancel(_ error: Error) -> Bool { (error as? URLError)?.code == .cancelled || error is CancellationError }

    public func challenge() async -> Result<DmDiagChallenge, DmDiagFailure> {
        let reply: Reply
        do { reply = try await send("GET", "challenge", body: nil) }
        catch { return .failure(DmDiagFailure(Self.isCancel(error) ? .cancelled : .unavailable, Self.notSentReason(error) ?? "network")) }
        guard reply.status == 200 else { return .failure(Self.classifyRefusal(reply, posted: false)) }
        guard let o = Self.object(reply.body), let c = o["challenge"] as? String, let raw = Self.decodeBase64URL(c), raw.count == 32,
              let exp = Self.integer(o["expires_in"]), (1...3600).contains(exp),
              let maxBytes = Self.integer(o["max_payload_bytes"]), (1024...(16 << 20)).contains(maxBytes),
              let keep = Self.integer(o["retention_days"]), (1...366).contains(keep) else { return .failure(DmDiagFailure(.unavailable, "bad_challenge")) }
        return .success(DmDiagChallenge(challenge: c, expiresIn: exp, maxPayloadBytes: maxBytes, retentionDays: keep))
    }
    /// 服务端给了明确拒绝（非 2xx）的归类；posted = 这是上传请求（不是取挑战） | classify an explicit non-2xx answer
    static func classifyRefusal(_ reply: Reply, posted: Bool) -> DmDiagFailure {
        let code = errorCode(reply.body)
        switch (reply.status, code) {
        case (409, "upload_conflict"): return DmDiagFailure(.conflict, "upload_conflict")
        case (429, _): return DmDiagFailure(.rateLimited, code ?? "rate_limited", retryAfter: retryAfter(reply))
        case (503, "diagnostic_maintenance"): return DmDiagFailure(.maintenance, "diagnostic_maintenance", retryAfter: retryAfter(reply))
        case (413, _): return DmDiagFailure(.tooLarge, code ?? "payload_too_large")
        case (500...599, _):
            // 取挑战失败 = 报告没发；上传收到 503 + 错误码 = 服务端明说没收；其余 5xx（网关、500）请求可能已处理 = 结果不明
            // challenge failure = nothing sent; 503 with an error code on upload = the server says it did not take it; other 5xx (gateway, 500) may have been processed = indeterminate
            if !posted { return DmDiagFailure(.unavailable, code ?? "http_\(reply.status)", retryAfter: retryAfter(reply)) }
            if reply.status == 503, let c = code { return DmDiagFailure(.unavailable, c, retryAfter: retryAfter(reply)) }
            return DmDiagFailure(.unconfirmed, code ?? "http_\(reply.status)")
        default: return DmDiagFailure(.rejected, code ?? "http_\(reply.status)")
        }
    }

    /// 一次尝试（玩家每点一次「上传」/「重试」调一次）：report 进来是这任务已有的报告（重试）或 nil（新任务），出来是这任务现在的报告。
    /// 自动的只有两件、各有上限：挑战在签名后失效（challenge_rejected）重取一次；服务端说载荷太大（413）就缩小重打包，最多 3 次。其余一律交回界面，由玩家决定是否重试。
    /// One attempt (one call per press of Upload / Retry). Only two automatic, bounded steps: one fresh challenge after challenge_rejected; up to 3 smaller re-packs after a 413. Everything else goes back to the UI.
    public func attempt(report: inout DmDiagReport?, key: Curve25519.Signing.PrivateKey, metadata: [String: String], collect: (Int) -> String?) async -> DmDiagResult {
        var refetched = false, shrinks = 0, budget = Int.max
        let maybe = { (r: DmDiagReport?) in r?.maybeDelivered ?? false }
        while true {
            let ch: DmDiagChallenge
            switch await challenge() {
            case .success(let c): ch = c
            case .failure(let f): return f.result(maybeDelivered: maybe(report))
            }
            let limit = min(ch.maxPayloadBytes, budget)
            if report == nil || report!.payload.count > limit {
                if report?.maybeDelivered == true { return DmDiagResult(outcome: .tooLarge, detail: "limit_shrank", maybeDelivered: true) }   // 上次可能已送达：不换 ID 另发一份 | maybe delivered: do not send a second copy under a new ID
                do { report = try Self.buildReport(limit: limit, metadata: metadata, collect: collect) }
                catch DmDiagError.tooLarge { return DmDiagResult(outcome: .tooLarge, detail: "local_limit") }
                catch { return DmDiagResult(outcome: .prepare, detail: "collect") }
            }
            let r = report!
            let signature: Data
            do { signature = try key.signature(for: Self.transcript(challenge: ch.challenge, uploadID: r.uploadID, payload: r.payload)) }
            catch { return DmDiagResult(outcome: .prepare, detail: "sign") }
            let body: Data
            do { body = try JSONSerialization.data(withJSONObject: ["public_key": Self.base64URL(key.publicKey.rawRepresentation), "challenge": ch.challenge,
                                                                    "upload_id": r.uploadID, "payload": Self.base64URL(r.payload), "signature": Self.base64URL(signature)]) }
            catch { return DmDiagResult(outcome: .prepare, detail: "encode") }
            let reply: Reply
            do { reply = try await send("POST", "reports", body: body) }
            catch {
                if Self.isCancel(error) { report!.maybeDelivered = true; return DmDiagResult(outcome: .cancelled, detail: "cancelled", maybeDelivered: true) }
                if let why = Self.notSentReason(error) { return DmDiagResult(outcome: maybe(report) ? .unconfirmed : .unavailable, detail: why, maybeDelivered: maybe(report)) }
                report!.maybeDelivered = true
                return DmDiagResult(outcome: .unconfirmed, detail: (error as? URLError)?.code == .timedOut ? "timeout" : "response_lost", maybeDelivered: true)
            }
            if reply.status == 200 || reply.status == 201 {
                guard let o = Self.object(reply.body), let id = o["report_id"] as? String, id.range(of: "^[0-9a-f]{32}$", options: .regularExpression) != nil,
                      let exp = Self.integer(o["expires_at"]), exp > 0, let dupFlag = o["duplicate"] as? NSNumber, CFGetTypeID(dupFlag) == CFBooleanGetTypeID() else {
                    report!.maybeDelivered = true   // 2xx 但回执不合格：服务端可能已存 | 2xx without a valid receipt: the server may have stored it
                    return DmDiagResult(outcome: .unconfirmed, detail: "bad_receipt", maybeDelivered: true)
                }
                return DmDiagResult(outcome: .ok, detail: id, expiresAt: Int64(exp), duplicate: dupFlag.boolValue, retentionDays: Int32(ch.retentionDays))
            }
            let f = Self.classifyRefusal(reply, posted: true)
            if f.outcome == .rejected && f.detail == "challenge_rejected" && !refetched { refetched = true; continue }
            if f.outcome == .tooLarge && shrinks < 3 && !maybe(report) {
                shrinks += 1; budget = r.payload.count / 2
                if budget < 4096 { return DmDiagResult(outcome: .tooLarge, detail: f.detail) }
                report = nil; continue   // 服务端说太大：一定没存，按更小的上限换一份新报告 | the server said too large: certainly not stored, so a smaller new report
            }
            if f.outcome == .unconfirmed { report!.maybeDelivered = true }
            return f.result(maybeDelivered: maybe(report))
        }
    }
}

// ---- 设备与使用统计的一批（策略 dm/host/dm_telemetry.c，协议定稿前按看板 PRD §2.0 / §7；同一套挑战签名，接口与 transcript 前缀不同） | one batch of the device and usage statistics (policy: dm_telemetry.c; until the protocol is final, dashboard PRD 2.0 / 7; the same challenge signing with other endpoints and transcript prefix) ----
extension DmDiagUploader {
    public static func telemetryTranscript(challenge: String, uploadID: String, payload: Data) -> Data {
        Data("D2R-TELEMETRY-V1\n\(challenge)\n\(uploadID)\n\(hex(SHA256.hash(data: payload)))\n".utf8)
    }
    /// 回执的归类（PRD §2.6 ⑤）：2xx 与 409 = 已送达；429 / 503 照 Retry-After；408、其他 5xx 可重试；其他 4xx 整批丢弃 | classify the answer: 2xx and 409 delivered; 429 / 503 follow Retry-After; 408 and other 5xx retryable; other 4xx drop the batch
    static func classifyTelemetry(status: Int, code: String?, retryAfter: Int32) -> DmDiagResult {
        let after = min(max(retryAfter, 0), 7 * 86400)
        switch status {
        case 200...299: return DmDiagResult(outcome: .ok, detail: "ok")
        case 409: return DmDiagResult(outcome: .conflict, detail: code ?? "duplicate")
        case 429: return DmDiagResult(outcome: .rateLimited, detail: code ?? "rate_limited", retryAfter: after > 0 ? after : 60)
        case 503: return DmDiagResult(outcome: .maintenance, detail: code ?? "unavailable", retryAfter: after > 0 ? after : 60)
        case 408, 500...599: return DmDiagResult(outcome: .unavailable, detail: code ?? "http_\(status)")
        default: return DmDiagResult(outcome: .rejected, detail: code ?? "http_\(status)")
        }
    }
    /// 取挑战 → 签名 → 提交一批。挑战在签名后失效（challenge_rejected）重取一次；其余交回 dm_telemetry.c | challenge → sign → submit one batch; one fresh challenge after challenge_rejected; everything else goes back to dm_telemetry.c
    public func telemetryBatch(payload: Data, key: Curve25519.Signing.PrivateKey) async -> DmDiagResult {
        let uploadID = Self.newUploadID()
        for _ in 0..<2 {
            let got: Reply
            do { got = try await send("GET", "challenge", body: nil) }
            catch { return Self.isCancel(error) ? DmDiagResult(outcome: .cancelled, detail: "cancelled") : DmDiagResult(outcome: .unavailable, detail: Self.notSentReason(error) ?? "network") }
            guard got.status == 200 else { return Self.classifyTelemetry(status: got.status, code: Self.errorCode(got.body), retryAfter: got.retryAfter) }
            guard let c = Self.object(got.body)?["challenge"] as? String, let raw = Self.decodeBase64URL(c), raw.count == 32 else { return DmDiagResult(outcome: .unavailable, detail: "bad_challenge") }
            let body: Data
            do {
                let sig = try key.signature(for: Self.telemetryTranscript(challenge: c, uploadID: uploadID, payload: payload))
                body = try JSONSerialization.data(withJSONObject: ["public_key": Self.base64URL(key.publicKey.rawRepresentation), "challenge": c, "upload_id": uploadID, "payload": Self.base64URL(payload), "signature": Self.base64URL(sig)])
            } catch { return DmDiagResult(outcome: .prepare, detail: "sign") }
            let reply: Reply
            do { reply = try await send("POST", "events", body: body) }
            catch { return Self.isCancel(error) ? DmDiagResult(outcome: .cancelled, detail: "cancelled") : DmDiagResult(outcome: .unconfirmed, detail: Self.notSentReason(error) ?? "response_lost") }
            let code = Self.errorCode(reply.body)
            if reply.status == 403 && code == "challenge_rejected" { continue }
            return Self.classifyTelemetry(status: reply.status, code: code, retryAfter: reply.retryAfter)
        }
        return DmDiagResult(outcome: .rejected, detail: "challenge_rejected")
    }
}
private func dmTelemetryStore(_ keyFile: String?) -> DmDiagKeyStore { keyFile.map { DmDiagFileKeyStore(path: $0) as DmDiagKeyStore } ?? DmDiagKeychainStore.telemetry }
private final class DmTelemetryJob: @unchecked Sendable { let lock = NSLock(); var task: Task<Void, Never>? }
private let dmTelemetryJob = DmTelemetryJob()
public typealias DmTelemetryNetFn = @convention(c) (UInt32, Int32) -> Void
public typealias DmTelemetryDoneFn = @convention(c) (UInt32, Int32, UnsafePointer<CChar>?, Int32) -> Void
@_cdecl("dm_telemetry_net_probe")
public func dmTelemetryNetProbe(_ token: UInt32, _ done: DmTelemetryNetFn) {
    Task.detached(priority: .utility) { done(token, await DmCrashReportStore.isMetered() ? 1 : 0) }
}
@_cdecl("dm_telemetry_net_send")
public func dmTelemetryNetSend(_ token: UInt32, _ origin: UnsafePointer<CChar>?, _ keyPath: UnsafePointer<CChar>?, _ body: UnsafePointer<CChar>?, _ n: Int, _ done: DmTelemetryDoneFn) {
    let originText = dmDiagCString(origin) ?? "", keyFile = dmDiagCString(keyPath), payload = body.map { Data(bytes: $0, count: n) } ?? Data()
    dmTelemetryJob.lock.lock(); defer { dmTelemetryJob.lock.unlock() }
    dmTelemetryJob.task = Task.detached(priority: .utility) {
        func finish(_ r: DmDiagResult) { r.detail.withCString { done(token, r.outcome.rawValue, $0, r.retryAfter) } }
        guard let up = DmDiagUploader(origin: originText, apiBase: "telemetry/v1/") else { finish(DmDiagResult(outcome: .configuration, detail: "origin")); return }
        let key: Curve25519.Signing.PrivateKey
        do { key = try dmTelemetryStore(keyFile).loadOrCreate() } catch { finish(DmDiagResult(outcome: .prepare, detail: "identity")); return }
        finish(await up.telemetryBatch(payload: payload, key: key))
    }
}
@_cdecl("dm_telemetry_net_cancel")
public func dmTelemetryNetCancel() { dmTelemetryJob.lock.lock(); dmTelemetryJob.task?.cancel(); dmTelemetryJob.task = nil; dmTelemetryJob.lock.unlock() }
@_cdecl("dm_telemetry_net_forget")
public func dmTelemetryNetForget(_ keyPath: UnsafePointer<CChar>?) {
    if let f = dmDiagCString(keyPath) { unlink(f) } else { DmDiagKeychainStore.telemetry.delete() }
}

public struct DmDiagFailure: Error {
    let outcome: DmDiagOutcome; let detail: String; var retryAfter: Int32 = 0
    init(_ o: DmDiagOutcome, _ d: String, retryAfter: Int32 = 0) { outcome = o; detail = d; self.retryAfter = retryAfter }
    /// 这份报告之前可能已送达时，「没发出」类失败要说成「结果未确认」 | when the report may already be delivered, "not sent" failures read as unconfirmed
    func result(maybeDelivered: Bool) -> DmDiagResult {
        DmDiagResult(outcome: maybeDelivered && outcome == .unavailable ? .unconfirmed : outcome, detail: detail, maybeDelivered: maybeDelivered, retryAfter: retryAfter)
    }
}

// ---- 自动上报崩溃的盘上报告（dm/host/dm_crashreport.c 管次数，这里管内容） | the automatic crash report on disk (dm_crashreport.c keeps the counts, this file the contents) ----
// <dir>/dm_crash_report.payload = 载荷原文（已脱敏的 JSON，重试要逐字节相同）；.id = upload_id + 「可能已送达」标记。先写临时文件再换名
// <dir>/dm_crash_report.payload = the exact payload (scrubbed JSON; retries must be byte-identical); .id = upload_id + the maybe-delivered flag. Temp file, then rename
public enum DmCrashReportStore {
    static func paths(_ dir: String, summary: Bool = false) -> (payload: String, id: String) { let k = dir + "/dm_crash_report." + (summary ? "summary." : ""); return (k + "payload", k + "id") }
    public static func load(dir: String, summary: Bool = false) -> DmDiagReport? {
        let p = paths(dir, summary: summary)
        guard let payload = FileManager.default.contents(atPath: p.payload), let raw = FileManager.default.contents(atPath: p.id), let text = String(data: raw, encoding: .utf8) else { return nil }
        let lines = text.split(separator: "\n").map(String.init)
        guard let id = lines.first, id.range(of: "^[0-9a-f]{32}$", options: .regularExpression) != nil else { return nil }
        return DmDiagReport(uploadID: id, payload: payload, maybeDelivered: lines.contains("maybe=1"))
    }
    public static func save(_ report: DmDiagReport, dir: String, summary: Bool = false) {
        let p = paths(dir, summary: summary)
        try? report.payload.write(to: URL(fileURLWithPath: p.payload), options: .atomic)
        try? Data("\(report.uploadID)\nmaybe=\(report.maybeDelivered ? 1 : 0)\n".utf8).write(to: URL(fileURLWithPath: p.id), options: .atomic)
    }
    /// 从盘上的载荷取回日志，留头 16 KiB（围栏、版本、设置摘要）和尾部（崩溃前最后的日志）凑够 limit：上一局的日志文件这时可能已被新启动覆盖，缩小重打包只能靠它 | recover the log from the payload on disk, keep the first 16 KiB (fence, version, settings) and the tail up to limit: the log files may have rotated by now, so a smaller re-pack can only start from this
    public static func trimmedLog(payload: Data, limit: Int) -> String? {
        guard let o = (try? JSONSerialization.jsonObject(with: payload)) as? [String: Any], let log = o["log"] as? String else { return nil }
        let u = Array(log.utf8), mark = "\n\n===== （中间已省略 | middle omitted）=====\n\n"
        if u.count <= limit { return log }
        let head = min(16 * 1024, limit / 2), tail = max(0, limit - head - mark.utf8.count)
        return String(decoding: u[0..<head], as: UTF8.self) + mark + String(decoding: u[(u.count - tail)...], as: UTF8.self)
    }
    static func summaryDone(dir: String) -> Bool { (FileManager.default.contents(atPath: paths(dir, summary: true).id).flatMap { String(data: $0, encoding: .utf8) } ?? "").contains("done=1") }
    static func markSummaryDone(dir: String) { let p = paths(dir, summary: true); if let d = FileManager.default.contents(atPath: p.id), let t = String(data: d, encoding: .utf8) { try? Data((t + "done=1\n").utf8).write(to: URL(fileURLWithPath: p.id), options: .atomic) } }
    static func logAndMeta(_ payload: Data) -> (String, [String: String])? {
        guard let o = (try? JSONSerialization.jsonObject(with: payload)) as? [String: Any], let log = o["log"] as? String, let m = o["metadata"] as? [String: String] else { return nil }
        return (log, m)
    }
    static func withFirstLine(_ log: String, _ first: String) -> String { first + (log.firstIndex(of: "\n").map { String(log[$0...]) } ?? "") }
    /// 摘要版：同一份报告的头（围栏、版本、设置与关键读数）≤ 16 KiB，整行截断，自己的 upload_id | the summary: the head of the same report (fence, version, settings and key readings), at most 16 KiB, cut at a line, its own upload_id
    static func makeSummary(of full: DmDiagReport) -> DmDiagReport? {
        guard let (log, meta) = logAndMeta(full.payload) else { return nil }
        var head = Array(log.utf8.prefix(16 * 1024 - 1024)); if let nl = head.lastIndex(of: 10) { head = Array(head[..<nl]) }
        let text = withFirstLine(String(decoding: head, as: UTF8.self), "D2R Mobile Plus crash report (automatic, summary)")
        guard let body = try? DmDiagUploader.payload(metadata: meta, log: text) else { return nil }
        return DmDiagReport(uploadID: DmDiagUploader.newUploadID(), payload: body)
    }
    /// 现在是不是计量网络（蜂窝、低数据模式）；测试开关 DM_TEST_METERED=0|1 | whether the current network is metered; test switch DM_TEST_METERED
    public static func isMetered() async -> Bool {
        if let t = getenv("DM_TEST_METERED") { return t[0] == 49 }
        return await withCheckedContinuation { (c: CheckedContinuation<Bool, Never>) in
            final class Once: @unchecked Sendable { let lock = NSLock(); var done = false; func first() -> Bool { lock.withLock { defer { done = true }; return !done } } }
            let m = NWPathMonitor(), once = Once(), q = DispatchQueue(label: "dm.crash.path")
            m.pathUpdateHandler = { p in if once.first() { m.cancel(); c.resume(returning: p.isExpensive || p.isConstrained) } }
            q.asyncAfter(deadline: .now() + 2) { if once.first() { m.cancel(); c.resume(returning: true) } }   // 读不出来按计量算：只会少发 | unreadable counts as metered: only ever sends less
            m.start(queue: q)
        }
    }
    /// 总开关暂停时只打包存盘，不联网（上一局日志下次启动就被覆盖） | while the server switch pauses uploads: pack to disk only, no network (the previous log is overwritten at the next launch)
    public static func prepare(dir: String, limit: Int, metadata: [String: String], collect: (Int) -> String?) {
        guard load(dir: dir) == nil, let report = try? DmDiagUploader.buildReport(limit: limit, metadata: metadata, collect: collect) else { return }
        save(report, dir: dir)
    }
    /// 一次尝试：有盘上报告就接着传，没有就新打一份并立刻存盘。collect 只在这次启动第一次打包时走（日志文件还是崩溃那一局的） | one attempt: continue the report on disk, or build one and save it at once. collect runs only for the first pack (the log files still belong to the crashed run)
    public static func attempt(dir: String, limit: Int, uploader: DmDiagUploader, key: Curve25519.Signing.PrivateKey, metadata: [String: String], metered: Bool = false, collect: @Sendable (Int) -> String?) async -> DmDiagResult {
        var report = load(dir: dir)
        if report == nil {
            do { report = try DmDiagUploader.buildReport(limit: limit, metadata: metadata, collect: collect) }
            catch DmDiagError.tooLarge { return DmDiagResult(outcome: .tooLarge, detail: "local_limit") }
            catch { return DmDiagResult(outcome: .prepare, detail: "collect") }
            save(report!, dir: dir)
        }
        if metered {   // 蜂窝只发摘要版；摘要已到或完整版可能已到就等 Wi-Fi | metered: the summary only; wait for Wi-Fi once it arrived or the full report may have
            if summaryDone(dir: dir) || report!.maybeDelivered { return DmDiagResult(outcome: .waitWifi, detail: "wifi") }
            var sum = load(dir: dir, summary: true) ?? makeSummary(of: report!)
            guard sum != nil else { return DmDiagResult(outcome: .prepare, detail: "summary") }
            save(sum!, dir: dir, summary: true)
            let id = sum!.uploadID, first = sum!.payload
            let r = await uploader.attempt(report: &sum, key: key, metadata: metadata) { lim in trimmedLog(payload: first, limit: lim) }
            if r.outcome == .ok { markSummaryDone(dir: dir); return DmDiagResult(outcome: .summarySent, detail: r.detail, duplicate: r.duplicate) }
            if let s = sum, s.uploadID == id || r.outcome != .ok { save(s, dir: dir, summary: true) }
            return r
        }
        // 完整版的第一行：没发过才改（同一 upload_id 载荷必须不变）；发过的报告 maybeDelivered 已置，原样重发 | the full report's first line: changed only before any send (one upload_id keeps one payload); a report already sent has maybeDelivered set and goes out unchanged
        if !report!.maybeDelivered, let (log, meta) = logAndMeta(report!.payload) {
            let sumID = load(dir: dir, summary: true).flatMap { summaryDone(dir: dir) ? $0.uploadID : nil }
            let line = "D2R Mobile Plus crash report (automatic, full)" + (sumID.map { " summary_of=" + $0 } ?? "")
            if let body = try? DmDiagUploader.payload(metadata: meta, log: withFirstLine(log, line)), body != report!.payload { report = DmDiagReport(uploadID: report!.uploadID, payload: body); save(report!, dir: dir) }
        }
        let original = report!.payload
        let result = await uploader.attempt(report: &report, key: key, metadata: metadata) { lim in trimmedLog(payload: original, limit: lim) }
        if let r = report, result.outcome != .ok { save(r, dir: dir) }
        return result
    }
}

// ---- C 接口（dm/host/dm_diagup.c 的外壳回调调这里） | C interface (called from the shell callbacks of dm/host/dm_diagup.c) ----
public typealias DmDiagCollectFn = @convention(c) (Int, UnsafeMutableRawPointer?) -> UnsafeMutablePointer<CChar>?   // 返回 malloc 的已脱敏 UTF-8 全文，调用方 free | returns malloc'd scrubbed UTF-8 text, freed by the caller
public typealias DmDiagDoneFn = @convention(c) (UInt32, Int32, UnsafePointer<CChar>?, Int64, Int32, Int32, Int32) -> Void   // token, outcome, detail, expires_at, flags (bit0 duplicate, bit1 maybe delivered), retry_after, retention_days

private final class DmDiagJobs: @unchecked Sendable {
    static let shared = DmDiagJobs()
    let lock = NSLock()
    var job: UInt32 = 0
    var report: DmDiagReport?
    var task: Task<Void, Never>?
    var crashTask: Task<Void, Never>?   // 自动上报的那一路，与手动上传互不影响 | the automatic path, independent of the manual upload
}
private struct DmDiagCtx: @unchecked Sendable { let ptr: UnsafeMutableRawPointer?; let collect: DmDiagCollectFn; let done: DmDiagDoneFn }   // C 指针与回调随任务进后台线程（外壳保证它们一直有效） | C pointer and callbacks travel with the task (the shell keeps them valid)

private func dmDiagCString(_ p: UnsafePointer<CChar>?) -> String? { p.flatMap { String(validatingCString: $0) } }

/// 后台开一次尝试；同一 job 再调 = 重试（沿用报告），换 job = 新报告。meta = 「键=值」每行一个。key_path 空 = 钥匙串 | start one attempt in the background
@_cdecl("dm_diagup_net_start")
public func dmDiagupNetStart(_ token: UInt32, _ job: UInt32, _ origin: UnsafePointer<CChar>?, _ meta: UnsafePointer<CChar>?, _ keyPath: UnsafePointer<CChar>?,
                             _ collect: DmDiagCollectFn, _ ctx: UnsafeMutableRawPointer?, _ done: DmDiagDoneFn) {
    let originText = dmDiagCString(origin) ?? "", metaText = dmDiagCString(meta) ?? "", keyFile = dmDiagCString(keyPath), box = DmDiagCtx(ptr: ctx, collect: collect, done: done)
    let metadata = Dictionary(metaText.split(separator: "\n").compactMap { line in line.firstIndex(of: "=").map { (String(line[..<$0]), String(line[line.index(after: $0)...])) } },
                              uniquingKeysWith: { first, _ in first })
    let jobs = DmDiagJobs.shared
    jobs.lock.lock()
    if jobs.job != job { jobs.job = job; jobs.report = nil }
    let task = Task.detached(priority: .utility) {
        func finish(_ r: DmDiagResult) {
            r.detail.withCString { box.done(token, r.outcome.rawValue, $0, r.expiresAt, (r.duplicate ? 1 : 0) | (r.maybeDelivered ? 2 : 0), r.retryAfter, r.retentionDays) }
        }
        guard let uploader = DmDiagUploader(origin: originText) else { finish(DmDiagResult(outcome: .configuration, detail: "origin")); return }
        let key: Curve25519.Signing.PrivateKey
        do { key = try (keyFile.map { DmDiagFileKeyStore(path: $0) as DmDiagKeyStore } ?? DmDiagKeychainStore.shared).loadOrCreate() }
        catch DmDiagError.identity(let why) { finish(DmDiagResult(outcome: .prepare, detail: why)); return }
        catch { finish(DmDiagResult(outcome: .prepare, detail: "identity")); return }
        var report = jobs.lock.withLock { jobs.job == job ? jobs.report : nil }
        let result = await uploader.attempt(report: &report, key: key, metadata: metadata) { limit in
            guard let p = box.collect(limit, box.ptr) else { return nil }
            defer { free(p) }
            return String(validatingCString: p)
        }
        jobs.lock.withLock { if jobs.job == job { jobs.report = report } }
        finish(result)
    }
    jobs.task = task
    jobs.lock.unlock()
}

/// 自动上报崩溃的一次尝试（dm/host/dm_crashreport.c 的外壳调；后台开始、立即返回）。done 与上面同一个类型 | one attempt of the automatic crash report (called by the shell of dm_crashreport.c; starts in the background, returns at once)
@_cdecl("dm_crashreport_net_start")
public func dmCrashReportNetStart(_ token: UInt32, _ origin: UnsafePointer<CChar>?, _ meta: UnsafePointer<CChar>?, _ keyPath: UnsafePointer<CChar>?, _ dir: UnsafePointer<CChar>?,
                                  _ collect: DmDiagCollectFn, _ ctx: UnsafeMutableRawPointer?, _ done: DmDiagDoneFn) {
    let originText = dmDiagCString(origin) ?? "", metaText = dmDiagCString(meta) ?? "", keyFile = dmDiagCString(keyPath), dirText = dmDiagCString(dir) ?? "", box = DmDiagCtx(ptr: ctx, collect: collect, done: done)
    let metadata = Dictionary(metaText.split(separator: "\n").compactMap { line in line.firstIndex(of: "=").map { (String(line[..<$0]), String(line[line.index(after: $0)...])) } },
                              uniquingKeysWith: { first, _ in first })
    let jobs = DmDiagJobs.shared
    jobs.lock.lock()
    let task = Task.detached(priority: .utility) {
        func finish(_ r: DmDiagResult) { r.detail.withCString { box.done(token, r.outcome.rawValue, $0, r.expiresAt, (r.duplicate ? 1 : 0) | (r.maybeDelivered ? 2 : 0), r.retryAfter, r.retentionDays) } }
        guard !dirText.isEmpty, let uploader = DmDiagUploader(origin: originText) else { finish(DmDiagResult(outcome: .configuration, detail: "origin")); return }
        let key: Curve25519.Signing.PrivateKey
        do { key = try (keyFile.map { DmDiagFileKeyStore(path: $0) as DmDiagKeyStore } ?? DmDiagKeychainStore.shared).loadOrCreate() }
        catch DmDiagError.identity(let why) { finish(DmDiagResult(outcome: .prepare, detail: why)); return }
        catch { finish(DmDiagResult(outcome: .prepare, detail: "identity")); return }
        let metered = await DmCrashReportStore.isMetered()
        finish(await DmCrashReportStore.attempt(dir: dirText, limit: 256 * 1024, uploader: uploader, key: key, metadata: metadata, metered: metered) { limit in
            guard let p = box.collect(limit, box.ptr) else { return nil }
            defer { free(p) }
            return String(validatingCString: p)
        })
    }
    jobs.crashTask = task
    jobs.lock.unlock()
}
@_cdecl("dm_crashreport_net_prepare")
public func dmCrashReportNetPrepare(_ meta: UnsafePointer<CChar>?, _ dir: UnsafePointer<CChar>?, _ collect: DmDiagCollectFn, _ ctx: UnsafeMutableRawPointer?) {
    let metaText = dmDiagCString(meta) ?? "", dirText = dmDiagCString(dir) ?? "", box = DmDiagCtx(ptr: ctx, collect: collect, done: { _, _, _, _, _, _, _ in })
    let metadata = Dictionary(metaText.split(separator: "\n").compactMap { line in line.firstIndex(of: "=").map { (String(line[..<$0]), String(line[line.index(after: $0)...])) } }, uniquingKeysWith: { first, _ in first })
    Task.detached(priority: .utility) {
        guard !dirText.isEmpty else { return }
        DmCrashReportStore.prepare(dir: dirText, limit: 256 * 1024, metadata: metadata) { limit in
            guard let p = box.collect(limit, box.ptr) else { return nil }
            defer { free(p) }
            return String(validatingCString: p)
        }
    }
}
@_cdecl("dm_crashreport_net_cancel")
public func dmCrashReportNetCancel() {
    let jobs = DmDiagJobs.shared
    jobs.lock.lock(); let t = jobs.crashTask; jobs.lock.unlock()
    t?.cancel()
}

/// 地址合不合格（外壳据此决定开不开上传；不写日志） | whether the origin is valid (the shell decides on upload from it; never logged)
@_cdecl("dm_diagup_net_origin_ok")
public func dmDiagupNetOriginOk(_ origin: UnsafePointer<CChar>?) -> Int32 { dmDiagCString(origin).flatMap(DmDiagUploader.validOrigin) != nil ? 1 : 0 }

/// 取消进行中的尝试（玩家开了新任务等）；回调照样来一次（cancelled），宿主按 token 丢弃 | cancel the running attempt; the callback still comes once (cancelled) and the host drops it by token
@_cdecl("dm_diagup_net_cancel")
public func dmDiagupNetCancel() {
    let jobs = DmDiagJobs.shared
    jobs.lock.lock(); let t = jobs.task; jobs.lock.unlock()
    t?.cancel()
}

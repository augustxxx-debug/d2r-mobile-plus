// 上传诊断报告的协议单测（dm_diag_upload.swift）：不连真服务端，用 URLProtocol 假传输逐个请求作答。
// 覆盖：地址规则、规范 base64url、签名内容逐字节与验签、请求体正好五个字段、载荷正好三个字段与六项元数据、整份载荷不超挑战上限、
// 丢响应后重试（同一公钥 / upload_id / 载荷、新挑战）、challenge_rejected 只重取一次、413 缩小重打包（新 upload_id、绝不发超限）、
// 429 / 维护 / 冲突 / 其他 4xx / 5xx / 离线 / DNS / TLS / 数据受限 / 坏回执 的归类、文件私钥 0600 与拒用不安全文件、C 接口回调。
// Protocol unit test for the diagnostic upload (dm_diag_upload.swift): no real server; a URLProtocol fake answers each request.
// 编译 | build: xcrun swiftc -swift-version 6 -parse-as-library ../Sources/dm_diag_upload.swift t_diag_upload.swift -o /tmp/dm-test-diag-upload
import CryptoKit
import Foundation

enum Fake { case reply(Int, [String: String], Data), fail(URLError.Code) }
final class FakeServer: @unchecked Sendable {
    static let shared = FakeServer()
    let lock = NSLock()
    var script: [(URLRequest, Data?) -> Fake] = []
    var seen: [(method: String, path: String, headers: [String: String], body: Data?)] = []
    var issued: [String] = []
    func next(_ r: URLRequest, _ body: Data?) -> Fake {
        lock.withLock {
            seen.append((r.httpMethod ?? "", r.url?.path ?? "", r.allHTTPHeaderFields ?? [:], body))
            return script.isEmpty ? .fail(.cannotConnectToHost) : script.removeFirst()(r, body)
        }
    }
    func reset(_ s: [(URLRequest, Data?) -> Fake]) { lock.withLock { script = s; seen = []; issued = [] } }
    func challenge(max: Int = 614400, keep: Int = 7) -> (URLRequest, Data?) -> Fake {
        { _, _ in
            var raw = [UInt8](repeating: 0, count: 32); for i in 0..<32 { raw[i] = UInt8.random(in: 0...255) }
            let c = DmDiagUploader.base64URL(Data(raw)); self.issued.append(c)
            return .reply(200, ["Content-Type": "application/json"], try! JSONSerialization.data(withJSONObject: ["challenge": c, "expires_in": 120, "max_payload_bytes": max, "retention_days": keep]))
        }
    }
}
final class FakeProtocol: URLProtocol, @unchecked Sendable {
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        var body = request.httpBody
        if body == nil, let s = request.httpBodyStream {
            s.open(); var d = Data(); var buf = [UInt8](repeating: 0, count: 65536)
            while s.hasBytesAvailable { let n = s.read(&buf, maxLength: buf.count); if n <= 0 { break }; d.append(buf, count: n) }
            s.close(); body = d
        }
        switch FakeServer.shared.next(request, body) {
        case .fail(let code): client?.urlProtocol(self, didFailWithError: URLError(code))
        case .reply(let status, let headers, let data):
            client?.urlProtocol(self, didReceive: HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: headers)!, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: data); client?.urlProtocolDidFinishLoading(self)
        }
    }
    override func stopLoading() {}
}

nonisolated(unsafe) var checks = 0, fails = 0
func check(_ ok: Bool, _ what: String, line: Int = #line) { checks += 1; if !ok { fails += 1; print("FAIL line \(line): \(what)") } }
func json(_ d: Data?) -> [String: Any] { (d.flatMap { try? JSONSerialization.jsonObject(with: $0) } as? [String: Any]) ?? [:] }
func receipt(_ id: String, dup: Bool) -> (URLRequest, Data?) -> Fake {
    { _, _ in .reply(201, ["Content-Type": "application/json"], try! JSONSerialization.data(withJSONObject: ["report_id": id, "expires_at": 1893456000, "duplicate": dup])) }
}
func err(_ status: Int, _ code: String, retryAfter: String? = nil) -> (URLRequest, Data?) -> Fake {
    { _, _ in .reply(status, retryAfter.map { ["Retry-After": $0] } ?? [:], Data("{\"error\":\"\(code)\"}".utf8)) }
}
func fail(_ c: URLError.Code) -> (URLRequest, Data?) -> Fake { { _, _ in .fail(c) } }
let meta = ["app_version": "1.0.0", "build": "28", "source_revision": "e8eefb7", "model": "iPhone99,1", "os_version": "27.2", "locale": "zh-Hans"]
func fixed(_ text: String) -> (Int) -> String? { { _ in text } }   // 收集端缩不了（头部本身就超） | a collector that cannot shrink
func collector(_ text: String) -> (Int) -> String? { { limit in text.count <= limit ? text : "【省略前 \(text.utf8.count - limit) 字节】\n" + String(text.suffix(limit / 3)) } }

@main struct Main {
    static func main() async {
        // ---- 地址、编码、签名内容 | origin, encoding, transcript ----
        check(DmDiagUploader.validOrigin("https://diag.example.invalid") != nil, "https origin")
        check(DmDiagUploader.validOrigin("http://diag.example.invalid") == nil, "plain http refused")
        check(DmDiagUploader.validOrigin("http://127.0.0.1:18771") != nil, "loopback http")
        check(DmDiagUploader.validOrigin("https://diag.example.invalid/x") == nil, "path refused")
        check(DmDiagUploader.validOrigin("https://u:p@diag.example.invalid") == nil, "credentials refused")
        check(DmDiagUploader.validOrigin("https://diag.example.invalid?a=1") == nil, "query refused")
        check(DmDiagUploader.validOrigin("") == nil, "empty refused")
        let raw = Data((0..<32).map { UInt8($0 * 7 & 0xff) }), enc = DmDiagUploader.base64URL(raw)
        check(!enc.contains("=") && !enc.contains("+") && !enc.contains("/") && enc.count == 43, "unpadded base64url")
        check(DmDiagUploader.decodeBase64URL(enc) == raw, "roundtrip")
        check(DmDiagUploader.decodeBase64URL(enc + "=") == nil, "padding refused")
        var bad = Array(enc); bad[42] = bad[42] == "A" ? "B" : "A"; check(DmDiagUploader.decodeBase64URL(String(bad)) == nil || DmDiagUploader.base64URL(DmDiagUploader.decodeBase64URL(String(bad))!) == String(bad), "non-canonical tail refused")
        let payload = Data("{\"log\":\"x\"}".utf8)
        check(DmDiagUploader.transcript(challenge: "C", uploadID: "U", payload: payload) == Data(("D2R-DIAGNOSTICS-V1\nC\nU\n" + SHA256.hash(data: payload).map { String(format: "%02x", $0) }.joined() + "\n").utf8), "transcript bytes")
        let id = DmDiagUploader.newUploadID(); check(id.range(of: "^[0-9a-f]{32}$", options: .regularExpression) != nil && id != DmDiagUploader.newUploadID(), "upload_id 32 random lowercase hex")
        let m = DmDiagUploader.cleanMetadata(["model": "Tester's iPhone", "os_version": "x", "locale": "zh Hans", "source_revision": "E8EEFB7", "extra": "1"])
        check(Set(m.keys) == Set(DmDiagUploader.metadataKeys) && m["model"] == "unknown" && m["os_version"] == "0.0" && m["locale"] == "und" && m["source_revision"] == "unknown" && m["app_version"] == "unknown", "metadata cleaned to the six allowed fields")
        check(DmDiagUploader.cleanLog("a\u{1b}[31mb\u{0}c\td\r\ne") == "a?[31mb?c\td\r\ne", "control characters replaced")
        let p = json(try? DmDiagUploader.payload(metadata: meta, log: "日志"))
        check(Set(p.keys) == ["schema", "metadata", "log"] && (p["schema"] as? NSNumber) == 1 && CFGetTypeID(p["schema"] as! NSNumber) != CFBooleanGetTypeID() && (p["metadata"] as? [String: String]) == meta, "payload shape")
        // 整份缩小：多字节文本，载荷 ≤ 上限、仍是合法 JSON / UTF-8、带省略说明 | shrinking: multibyte text within the limit, still valid, with the omission note
        let big = String(repeating: "第一幕 光追阴影：帧 1234 用时 5.6 毫秒 😀\n", count: 40000)
        do { let r = try DmDiagUploader.buildReport(limit: 600 * 1024, metadata: meta, collect: collector(big))
             check(r.payload.count <= 600 * 1024 && String(data: r.payload, encoding: .utf8) != nil && (json(r.payload)["log"] as? String)?.hasPrefix("【省略前 ") == true, "fits 600 KiB with an omission note (\(r.payload.count))") }
        catch { check(false, "buildReport threw \(error)") }
        do { _ = try DmDiagUploader.buildReport(limit: 3000, metadata: meta) { _ in String(repeating: "x", count: 5000) }; check(false, "a fixed oversize header must fail") }
        catch { check((error as? DmDiagError) == .tooLarge, "tooLarge when the collector cannot shrink") }

        // ---- 假服务端上的尝试 | attempts against the fake server ----
        let config = URLSessionConfiguration.ephemeral; config.protocolClasses = [FakeProtocol.self]
        let up = DmDiagUploader(origin: "https://diag.example.invalid", configuration: config)!
        let key = Curve25519.Signing.PrivateKey(), fs = FakeServer.shared, rid = "0123456789abcdef0123456789abcdef"
        let log = "[perf] 温控 1 3D 0.85 帧率 59.8\n光追阴影：生效 关\n"
        // A 正常：请求体正好五个字段、签名对得上实际发送的载荷字节、没有凭据头 | A normal: exactly five fields, the signature covers the sent payload bytes, no credential headers
        fs.reset([fs.challenge(), receipt(rid, dup: false)])
        var report: DmDiagReport? = nil
        var res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log))
        check(res.outcome == .ok && res.detail == rid && !res.duplicate && res.retentionDays == 7, "A ok \(res)")
        let post = fs.seen.last!, b = json(post.body)
        check(fs.seen.map { "\($0.method) \($0.path)" } == ["GET /diagnostics/v1/challenge", "POST /diagnostics/v1/reports"], "two requests")
        check(Set(b.keys) == ["public_key", "challenge", "upload_id", "payload", "signature"], "five body fields")
        check(post.headers["Content-Type"] == "application/json" && post.headers["Authorization"] == nil && post.headers["Cookie"] == nil, "json, no credentials")
        if let pk = DmDiagUploader.decodeBase64URL(b["public_key"] as? String ?? ""), let sig = DmDiagUploader.decodeBase64URL(b["signature"] as? String ?? ""),
           let pl = DmDiagUploader.decodeBase64URL(b["payload"] as? String ?? ""), let c = b["challenge"] as? String, let u = b["upload_id"] as? String {
            check(pk.count == 32 && sig.count == 64 && c == fs.issued.last, "raw key 32, signature 64, fresh challenge")
            check((try? Curve25519.Signing.PublicKey(rawRepresentation: pk))?.isValidSignature(sig, for: DmDiagUploader.transcript(challenge: c, uploadID: u, payload: pl)) == true, "signature verifies")
            check(pl == report?.payload && u == report?.uploadID, "the stored report is what was sent")
        } else { check(false, "body fields decode") }
        // B 丢响应 → 结果未确认；重试 = 同一公钥 / upload_id / 载荷 + 新挑战 → duplicate | B lost response → unconfirmed; retry with the same report and a new challenge → duplicate
        fs.reset([fs.challenge(), fail(.networkConnectionLost)]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log))
        check(res.outcome == .unconfirmed && res.maybeDelivered && res.detail == "response_lost" && report?.maybeDelivered == true, "B lost → unconfirmed")
        let first = json(fs.seen.last!.body); let firstChallenge = fs.issued.last
        fs.reset([fs.challenge(), receipt(rid, dup: true)])
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log))
        let second = json(fs.seen.last!.body)
        check(res.outcome == .ok && res.duplicate && res.detail == rid, "B retry ok duplicate")
        check(second["upload_id"] as? String == first["upload_id"] as? String && second["payload"] as? String == first["payload"] as? String && second["public_key"] as? String == first["public_key"] as? String, "same id / payload / key")
        check(second["challenge"] as? String != firstChallenge && second["challenge"] as? String == fs.issued.last, "new challenge, never the old one")
        fs.reset([fs.challenge(), fail(.timedOut)]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unconfirmed && res.detail == "timeout", "timeout → unconfirmed")
        // 可能已送达之后取挑战离线：仍是「结果未确认」，不能说「没发出」 | maybe delivered, then offline at the challenge: still unconfirmed
        fs.reset([fail(.notConnectedToInternet)])
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unconfirmed && res.detail == "offline" && res.maybeDelivered, "maybe + offline → unconfirmed")
        // challenge_rejected：重取一次；两次就交回 | challenge_rejected: one refetch, then give up
        fs.reset([fs.challenge(), err(403, "challenge_rejected"), fs.challenge(), receipt(rid, dup: false)]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .ok && fs.seen.count == 4, "one refetch")
        fs.reset([fs.challenge(), err(403, "challenge_rejected"), fs.challenge(), err(403, "challenge_rejected")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .rejected && res.detail == "challenge_rejected" && fs.seen.count == 4, "no loop on challenge_rejected")
        // 413：缩小、新 upload_id；每次发出的载荷都不超当时的上限 | 413: shrink with a new upload_id; every sent payload is within its limit
        fs.reset([fs.challenge(), err(413, "payload_too_large"), fs.challenge(), receipt(rid, dup: false)]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(big))
        let posts = fs.seen.filter { $0.method == "POST" }.map { json($0.body) }
        let sizes = posts.compactMap { DmDiagUploader.decodeBase64URL($0["payload"] as? String ?? "")?.count }
        check(res.outcome == .ok && sizes.count == 2 && sizes[0] <= 614400 && sizes[1] <= sizes[0] / 2 && posts[0]["upload_id"] as? String != posts[1]["upload_id"] as? String, "413 → smaller report, new id \(sizes)")
        fs.reset([fs.challenge(max: 2048)]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: fixed(String(repeating: "头部", count: 2000))); check(res.outcome == .tooLarge && fs.seen.count == 1, "nothing sent when it cannot fit the challenge limit")
        // 已有一份可能已送达的报告、上限变小：不换 ID 另发 | a maybe-delivered report and a smaller limit: no second copy under a new id
        report = DmDiagReport(uploadID: DmDiagUploader.newUploadID(), payload: Data(count: 5000), maybeDelivered: true)
        fs.reset([fs.challenge(max: 4096)])
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .tooLarge && res.detail == "limit_shrank" && fs.seen.count == 1, "limit shrank after maybe-delivered")
        // 限流 / 维护 / 冲突 / 其他 4xx / 5xx | rate limit / maintenance / conflict / other 4xx / 5xx
        fs.reset([fs.challenge(), err(429, "rate_limited", retryAfter: "17")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .rateLimited && res.retryAfter == 17 && fs.seen.count == 2, "429 with Retry-After, no loop")
        fs.reset([err(429, "rate_limited")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .rateLimited && res.retryAfter == 60, "429 at the challenge, default wait")
        fs.reset([err(503, "diagnostic_maintenance", retryAfter: "60")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .maintenance && res.retryAfter == 60 && fs.seen.count == 1, "maintenance at the challenge")
        fs.reset([fs.challenge(), err(503, "diagnostic_maintenance")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .maintenance, "maintenance at the upload")
        fs.reset([fs.challenge(), err(409, "upload_conflict")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .conflict && fs.seen.count == 2, "conflict, no new id")
        fs.reset([fs.challenge(), err(400, "invalid_metadata")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .rejected && res.detail == "invalid_metadata", "other 4xx keeps the code")
        fs.reset([fs.challenge(), err(500, "request_failed")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unconfirmed && report?.maybeDelivered == true, "500 on upload → unconfirmed")
        fs.reset([fs.challenge(), { _, _ in .reply(502, [:], Data("bad gateway".utf8)) }]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unconfirmed && res.detail == "http_502", "gateway error → unconfirmed")
        fs.reset([fs.challenge(), err(503, "storage_capacity")]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unavailable && res.detail == "storage_capacity", "explicit 503 → not stored")
        fs.reset([fs.challenge(), { _, _ in .reply(201, [:], Data("{\"report_id\":\"x\"}".utf8)) }]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unconfirmed && res.detail == "bad_receipt", "invalid receipt is never success")
        fs.reset([fs.challenge(), { _, _ in .reply(201, [:], Data("{\"report_id\":\"\(rid)\",\"expires_at\":1,\"duplicate\":1}".utf8)) }]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unconfirmed, "duplicate must be a JSON boolean")
        fs.reset([{ _, _ in .reply(200, [:], Data("{\"challenge\":\"abc\",\"expires_in\":120,\"max_payload_bytes\":614400,\"retention_days\":7}".utf8)) }]); report = nil
        res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unavailable && res.detail == "bad_challenge", "malformed challenge")
        // 网络错误归类 | network error classes
        for (code, want, sent) in [(URLError.Code.notConnectedToInternet, "offline", false), (.dataNotAllowed, "data_restricted", false), (.cannotFindHost, "dns", false),
                                   (.dnsLookupFailed, "dns", false), (.secureConnectionFailed, "tls", false), (.serverCertificateUntrusted, "tls", false), (.cannotConnectToHost, "connect", false)] {
            fs.reset([fail(code)]); report = nil
            res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unavailable && res.detail == want && !res.maybeDelivered, "\(code) at the challenge → \(want)")
            fs.reset([fs.challenge(), fail(code)]); report = nil
            res = await up.attempt(report: &report, key: key, metadata: meta, collect: collector(log)); check(res.outcome == .unavailable && res.detail == want && res.maybeDelivered == sent, "\(code) at the upload → not sent")
        }
        // 文件私钥 | file key store
        let dir = NSTemporaryDirectory() + "dm-diag-key-" + UUID().uuidString, path = dir + "/k/diag_identity.key"
        do { let k1 = try DmDiagFileKeyStore(path: path).loadOrCreate(), k2 = try DmDiagFileKeyStore(path: path).loadOrCreate()
             var st = stat(); lstat(path, &st)
             check(k1.publicKey.rawRepresentation == k2.publicKey.rawRepresentation && st.st_mode & 0o777 == 0o600, "stable 0600 key")
             chmod(path, 0o644); check((try? DmDiagFileKeyStore(path: path).loadOrCreate()) == nil, "0644 key refused")
             chmod(path, 0o600); let link = dir + "/link"; symlink(path, link); check((try? DmDiagFileKeyStore(path: link).loadOrCreate()) == nil, "symlink refused") }
        catch { check(false, "file key \(error)") }
        try? FileManager.default.removeItem(atPath: dir)
        // 自动上报的盘上报告（DmCrashReportStore）：第一次打包立即存盘、失败留着；下次启动同一 upload_id + 同一载荷 + 新挑战；成功不再存；开关关的路径根本不调这里（dm_crashreport.c，t_crashreport）| the automatic report on disk
        let crDir = NSTemporaryDirectory() + "dm-cr-\(getpid())"; try? FileManager.default.createDirectory(atPath: crDir, withIntermediateDirectories: true)
        let crCollects = CrCount(); let crMeta = ["app_version": "1.0.0", "build": "12", "source_revision": "abcdef1", "model": "iPhone19,4", "os_version": "27.2.0", "locale": "en"]
        let crCfg = URLSessionConfiguration.ephemeral; crCfg.protocolClasses = [FakeProtocol.self]
        guard let crUp = DmDiagUploader(origin: "https://diag.example.invalid", configuration: crCfg) else { check(false, "uploader"); exit(1) }
        let crKey = Curve25519.Signing.PrivateKey()
        FakeServer.shared.reset([FakeServer.shared.challenge(), { _, _ in .fail(.timedOut) }])
        var r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in crCollects.n += 1; return "fence cause=crash\nlog line" }
        let saved1 = DmCrashReportStore.load(dir: crDir)
        check(r1.outcome == .unconfirmed && crCollects.n == 1, "first attempt lost its response")
        check(saved1 != nil && saved1!.maybeDelivered, "the report stays on disk flagged maybe-delivered")
        let sent1 = FakeServer.shared.seen.last.flatMap { json($0.body)["upload_id"] as? String }
        check(sent1 == saved1?.uploadID, "the persisted id is the one sent")
        FakeServer.shared.reset([FakeServer.shared.challenge(), receipt("0123456789abcdef0123456789abcdef", dup: true)])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in crCollects.n += 1; return "the log files have rotated" }
        let sent2 = FakeServer.shared.seen.last.map { json($0.body) } ?? [:]
        check(r1.outcome == .ok && r1.duplicate && crCollects.n == 1, "next launch resends without collecting again")
        check(sent2["upload_id"] as? String == saved1?.uploadID, "same upload_id"); check((sent2["payload"] as? String).flatMap(DmDiagUploader.decodeBase64URL) == saved1?.payload, "byte-identical payload")
        // 缩小重打包：日志文件已换，只能从盘上的载荷截；413 后新 upload_id、绝不再收集 | smaller re-pack from the payload on disk: new upload_id after a 413, no fresh collection
        try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report.payload"); try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report.id")
        let bigLog = "first line\nHEAD-MARK\n" + String(repeating: "x", count: 100_000) + "\nTAIL-MARK"
        FakeServer.shared.reset([FakeServer.shared.challenge(), err(413, "payload_too_large"), FakeServer.shared.challenge(), { r, b in
            let p = (json(b)["payload"] as? String).flatMap(DmDiagUploader.decodeBase64URL) ?? Data(); check(p.count <= 60_000, "smaller after 413: \(p.count)")
            let log = (json(p)["log"] as? String) ?? ""; check(log.contains("HEAD-MARK") && log.hasSuffix("TAIL-MARK"), "keeps head and tail")
            return receipt("fedcba9876543210fedcba9876543210", dup: false)(r, b) }])
        crCollects.n = 0
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in crCollects.n += 1; return bigLog }
        check(r1.outcome == .ok && crCollects.n == 1, "re-pack did not collect again: \(r1.outcome) \(crCollects.n)")
        // 总开关暂停：只打包存盘、零请求；恢复后同一份报告发出 | server switch paused: pack only, zero requests; the same report goes out on resume
        try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report.payload"); try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report.id")
        FakeServer.shared.reset([]); crCollects.n = 0
        DmCrashReportStore.prepare(dir: crDir, limit: 256 * 1024, metadata: crMeta) { _ in crCollects.n += 1; return "packed while paused" }
        DmCrashReportStore.prepare(dir: crDir, limit: 256 * 1024, metadata: crMeta) { _ in crCollects.n += 1; return "again" }
        let packed = DmCrashReportStore.load(dir: crDir)
        check(FakeServer.shared.seen.isEmpty && crCollects.n == 1 && packed != nil, "prepare: no request, collected once")
        FakeServer.shared.reset([FakeServer.shared.challenge(), receipt("0123456789abcdef0123456789abcdef", dup: false)])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in "must not collect" }
        check(r1.outcome == .ok && (FakeServer.shared.seen.last.map { json($0.body) }?["upload_id"] as? String) == packed?.uploadID, "resume sends the packed report")
        // 蜂窝：摘要版 ≤ 16 KiB、第一行标 summary；到了就不再发；完整版 Wi-Fi 才发、第一行 summary_of、不同 upload_id | metered: summary at most 16 KiB; once arrived nothing more; the full report on Wi-Fi with summary_of and another upload_id
        for f in ["payload", "id", "summary.payload", "summary.id"] { try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report." + f) }
        let longLog = "D2R Mobile Plus crash report (automatic)\nfence cause=crash\n" + (0..<4000).map { "line \($0) of the log" }.joined(separator: "\n")
        FakeServer.shared.reset([FakeServer.shared.challenge(), receipt("0123456789abcdef0123456789abcdef", dup: false)])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta, metered: true) { _ in longLog }
        check(r1.outcome == .summarySent, "summary sent \(r1.outcome)")
        let sb = FakeServer.shared.seen.last.map { json($0.body) } ?? [:], sp = (sb["payload"] as? String).flatMap(DmDiagUploader.decodeBase64URL) ?? Data(), sl = (json(sp)["log"] as? String) ?? ""
        check(sp.count <= 16 * 1024 && sl.hasPrefix("D2R Mobile Plus crash report (automatic, summary)\nfence cause=crash") && !sl.contains("line 3999"), "summary shape \(sp.count)")
        FakeServer.shared.reset([])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta, metered: true) { _ in "x" }
        check(r1.outcome == .waitWifi && FakeServer.shared.seen.isEmpty, "metered after the summary: no request")
        FakeServer.shared.reset([FakeServer.shared.challenge(), receipt("fedcba9876543210fedcba9876543210", dup: false)])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta, metered: false) { _ in "x" }
        let fb = FakeServer.shared.seen.last.map { json($0.body) } ?? [:], fp = (fb["payload"] as? String).flatMap(DmDiagUploader.decodeBase64URL) ?? Data(), fl = (json(fp)["log"] as? String) ?? ""
        check(r1.outcome == .ok && fl.hasPrefix("D2R Mobile Plus crash report (automatic, full) summary_of=\(sb["upload_id"] as? String ?? "?")\n") && fl.contains("line 3999") && (fb["upload_id"] as? String) != (sb["upload_id"] as? String), "full report on Wi-Fi")
        // Wi-Fi 上丢响应后换到蜂窝：完整版可能已到，不发摘要；回 Wi-Fi 同 ID 同载荷 | a lost response on Wi-Fi then metered: no summary; back on Wi-Fi the same id and bytes
        for f in ["payload", "id", "summary.payload", "summary.id"] { try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report." + f) }
        FakeServer.shared.reset([FakeServer.shared.challenge(), { _, _ in .fail(.timedOut) }])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in "log" }
        let held = DmCrashReportStore.load(dir: crDir); FakeServer.shared.reset([])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta, metered: true) { _ in "x" }
        check(r1.outcome == .waitWifi && FakeServer.shared.seen.isEmpty, "maybe delivered: wait for Wi-Fi")
        FakeServer.shared.reset([FakeServer.shared.challenge(), receipt("0123456789abcdef0123456789abcdef", dup: true)])
        r1 = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in "x" }
        check(r1.outcome == .ok && (FakeServer.shared.seen.last.map { json($0.body) }?["upload_id"] as? String) == held?.uploadID, "same id");
        // 4xx / 5xx / 限流：归类交回策略层，报告留着由策略层决定 | 4xx / 5xx / rate limit classified and handed back
        for (status, code, want, after) in [(429, "rate_limited", DmDiagOutcome.rateLimited, "120"), (503, "diagnostic_maintenance", .maintenance, "90"), (400, "invalid_metadata", .rejected, nil), (409, "upload_conflict", .conflict, nil), (500, "request_failed", .unconfirmed, nil)] as [(Int, String, DmDiagOutcome, String?)] {
            try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report.payload"); try? FileManager.default.removeItem(atPath: crDir + "/dm_crash_report.id")
            FakeServer.shared.reset([FakeServer.shared.challenge(), err(status, code, retryAfter: after)])
            let r = await DmCrashReportStore.attempt(dir: crDir, limit: 256 * 1024, uploader: crUp, key: crKey, metadata: crMeta) { _ in "log" }
            check(r.outcome == want && r.detail == code, "\(status) \(code) → \(r.outcome)")
            if let after { check(r.retryAfter == Int32(after), "Retry-After \(after)") }
        }
        try? FileManager.default.removeItem(atPath: crDir)
        // C 接口：地址不合格 → 配置错误回调一次 | C interface: invalid origin → one configuration callback
        check(dmDiagupNetOriginOk("https://diag.example.invalid") == 1 && dmDiagupNetOriginOk("http://diag.example.invalid") == 0 && dmDiagupNetOriginOk(nil) == 0, "origin check")
        let collectC: DmDiagCollectFn = { _, _ in strdup("log") }
        let doneC: DmDiagDoneFn = { token, outcome, detail, _, _, _, _ in CDone.shared.put(token, outcome, String(cString: detail!)) }
        dmDiagupNetStart(9, 1, "ftp://x", "model=iPhone99,1", nil, collectC, nil, doneC)
        for _ in 0..<200 where CDone.shared.get() == nil { try? await Task.sleep(nanoseconds: 10_000_000) }
        let got = CDone.shared.get(); check(got?.0 == 9 && got?.1 == DmDiagOutcome.configuration.rawValue && got?.2 == "origin", "C callback for an invalid origin")
        print("t_diag_upload: \(checks) checks, \(fails) failed")
        exit(fails == 0 ? 0 : 1)
    }
}
final class CrCount: @unchecked Sendable { var n = 0 }
final class CDone: @unchecked Sendable {
    static let shared = CDone(); let lock = NSLock(); var v: (UInt32, Int32, String)?
    func put(_ t: UInt32, _ o: Int32, _ d: String) { lock.withLock { v = (t, o, d) } }
    func get() -> (UInt32, Int32, String)? { lock.withLock { v } }
}

// 光追可行性样机（Mac 离线）：读一帧抓取的世界空间三角形，建 Metal 加速结构，按游戏相机发主射线 + 太阳阴影射线 / 环境光遮蔽射线，出图并计时。
// 只用于评估：不进 App、不在真机跑。输入 scene.bin 由 cap2scene.py 从翻译层抓帧（DM_NVN_RTCAP）转换。
// 用法：swiftc -O rt_proto.swift -o rt_proto && ./rt_proto scene.bin out.png [ao射线数=0]
// RT feasibility prototype (offline on the Mac): reads one captured frame of world-space triangles, builds a Metal acceleration structure,
// shoots primary rays from the game camera + sun shadow rays / ambient-occlusion rays, writes an image and prints timings.
// Evaluation only: not part of the App, never run on the device. scene.bin comes from cap2scene.py (translation-layer capture DM_NVN_RTCAP).
// Usage: swiftc -O rt_proto.swift -o rt_proto && ./rt_proto scene.bin out.png [ao rays=0]
import Foundation
import Metal
import simd
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

let kernelSrc = """
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;
struct Params { float4x4 invVP; float4 sun; uint2 size; uint aoRays; uint frame; };
static float3 unproject(float4x4 invVP, float2 ndc, float z) { float4 p = invVP * float4(ndc, z, 1); return p.xyz / p.w; }
static uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
kernel void trace(texture2d<float, access::write> out [[texture(0)]], primitive_acceleration_structure as [[buffer(0)]], constant Params &P [[buffer(1)]],
                  device atomic_uint *stats [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= P.size.x || gid.y >= P.size.y) return;
    float2 ndc = float2((float(gid.x) + 0.5) / float(P.size.x) * 2 - 1, 1 - (float(gid.y) + 0.5) / float(P.size.y) * 2);
    float3 o = unproject(P.invVP, ndc, 0.0), f = unproject(P.invVP, ndc, 1.0);   // NVN/D3D 深度 0..1 | 0..1 depth
    ray r; r.origin = o; r.direction = normalize(f - o); r.min_distance = 0; r.max_distance = length(f - o) * 1.01;
    intersector<triangle_data> isect; isect.assume_geometry_type(geometry_type::triangle); isect.force_opacity(forced_opacity::opaque);
    intersection_result<triangle_data> h = isect.intersect(r, as);
    float4 c = float4(0.05, 0.05, 0.08, 1);
    if (h.type != intersection_type::none) {
        atomic_fetch_add_explicit(stats, 1, memory_order_relaxed);
        float3 p = r.origin + r.direction * h.distance;
        // 几何法线：三角形的三个顶点由 primitive_id 从几何缓冲取不到（这里没传顶点），用屏幕导数替代会跨线程；改用光线命中的重心梯度不可得，简单起见对太阳方向做一次偏移
        // Geometric normal: we did not pass the vertex buffer to the kernel, so offset along the sun direction and along the view ray instead
        float3 sun = normalize(P.sun.xyz);
        ray s; s.origin = p - r.direction * 0.02; s.direction = sun; s.min_distance = 0.02; s.max_distance = 1e4;
        intersector<triangle_data> si; si.assume_geometry_type(geometry_type::triangle); si.force_opacity(forced_opacity::opaque); si.accept_any_intersection(true);
        intersection_result<triangle_data> sh = si.intersect(s, as);
        float lit = sh.type == intersection_type::none ? 1.0 : 0.0;
        float ao = 1.0;
        if (P.aoRays) {
            uint seed = hash(gid.x * 1973 + gid.y * 9277 + P.frame * 26699);
            float occ = 0;
            for (uint i = 0; i < P.aoRays; i++) {
                seed = hash(seed); float u1 = float(seed & 0xffff) / 65535.0; seed = hash(seed); float u2 = float(seed & 0xffff) / 65535.0;
                float ph = u2 * 6.2831853, ct = sqrt(1 - u1), st = sqrt(u1);
                float3 d = float3(cos(ph) * st, ct, sin(ph) * st);   // 以世界 +Y 为上（D2R 的世界上方向待抓帧核对） | world +Y as up (to be verified from the capture)
                ray a; a.origin = p - r.direction * 0.02; a.direction = d; a.min_distance = 0.02; a.max_distance = 3.0;
                intersection_result<triangle_data> ah = si.intersect(a, as);
                if (ah.type != intersection_type::none) occ += 1;
            }
            ao = 1 - occ / float(P.aoRays);
        }
        float depth = saturate(h.distance / r.max_distance);
        c = float4(lit, ao, 1 - depth, 1);
    }
    out.write(c, gid);
}
"""

func die(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(1) }
let args = CommandLine.arguments
if args.count < 3 { die("用法：rt_proto scene.bin out.png [ao射线数]") }
let data = try! Data(contentsOf: URL(fileURLWithPath: args[1]))
let aoRays = args.count > 3 ? UInt32(args[3]) ?? 0 : 0
var off = 0
func rd<T>(_: T.Type) -> T { let v = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: off, as: T.self) }; off += MemoryLayout<T>.size; return v }
let magic = rd(UInt32.self); if magic != 0x54524d44 { die("不是 scene.bin（魔数 %x）".replacingOccurrences(of: "%x", with: String(magic, radix: 16))) }
_ = rd(UInt32.self)
let ntri = Int(rd(UInt32.self)); let W = Int(rd(UInt32.self)); let H = Int(rd(UInt32.self)); let ninst = Int(rd(UInt32.self))
var m = [Float](repeating: 0, count: 16); for i in 0..<16 { m[i] = rd(Float.self) }
let sun = simd_float4(rd(Float.self), rd(Float.self), rd(Float.self), 0)
let triBytes = ntri * 9 * 4
let triData = data.subdata(in: off..<off + triBytes); off += triBytes

let dev = MTLCreateSystemDefaultDevice()!
print("设备 \(dev.name)  supportsRaytracing=\(dev.supportsRaytracing)  supportsRaytracingFromRender=\(dev.supportsRaytracingFromRender)  三角形 \(ntri)  实例(抓帧时的绘制数) \(ninst)  出图 \(W)x\(H)  AO 射线 \(aoRays)")
if !dev.supportsRaytracing { die("此设备不支持 Metal 光追") }
let q = dev.makeCommandQueue()!
let vb = dev.makeBuffer(bytes: (triData as NSData).bytes, length: triBytes, options: .storageModeShared)!

// BLAS：单个三角形几何（评估用；正式方案按绘制分 BLAS + TLAS 实例）| single triangle geometry (evaluation; the real design is per-draw BLAS + TLAS instances)
let geo = MTLAccelerationStructureTriangleGeometryDescriptor()
geo.vertexBuffer = vb; geo.vertexStride = 12; geo.vertexFormat = .float3; geo.triangleCount = ntri; geo.opaque = true
let asd = MTLPrimitiveAccelerationStructureDescriptor(); asd.geometryDescriptors = [geo]
let sizes = dev.accelerationStructureSizes(descriptor: asd)
let accel = dev.makeAccelerationStructure(size: sizes.accelerationStructureSize)!
let scratch = dev.makeBuffer(length: sizes.buildScratchBufferSize, options: .storageModePrivate)!
func timed(_ label: String, _ body: (MTLCommandBuffer) -> Void) -> Double {
    let cb = q.makeCommandBuffer()!; body(cb); cb.commit(); cb.waitUntilCompleted()
    let ms = (cb.gpuEndTime - cb.gpuStartTime) * 1000; print(String(format: "%@：GPU %.3f ms", label, ms)); return ms
}
var buildMs = [Double]()
for _ in 0..<3 { buildMs.append(timed("加速结构构建（\(ntri) 三角形，\(sizes.accelerationStructureSize / 1024) KiB）") { cb in
    let e = cb.makeAccelerationStructureCommandEncoder()!; e.build(accelerationStructure: accel, descriptor: asd, scratchBuffer: scratch, scratchBufferOffset: 0); e.endEncoding() }) }
// 重拟合（动态角色每帧要做的事）| refit (what animated characters need each frame)
for _ in 0..<2 { _ = timed("加速结构重拟合 refit") { cb in
    let e = cb.makeAccelerationStructureCommandEncoder()!; e.refit(sourceAccelerationStructure: accel, descriptor: asd, destinationAccelerationStructure: accel, scratchBuffer: scratch, scratchBufferOffset: 0); e.endEncoding() } }

let lib = try! dev.makeLibrary(source: kernelSrc, options: nil)
let pso = try! dev.makeComputePipelineState(function: lib.makeFunction(name: "trace")!)
let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: W, height: H, mipmapped: false); td.usage = [.shaderWrite]; td.storageMode = .shared
let tex = dev.makeTexture(descriptor: td)!
struct Params { var invVP: simd_float4x4; var sun: simd_float4; var size: simd_uint2; var aoRays: UInt32; var frame: UInt32 }
let invVP = simd_float4x4(columns: (simd_float4(m[0], m[1], m[2], m[3]), simd_float4(m[4], m[5], m[6], m[7]), simd_float4(m[8], m[9], m[10], m[11]), simd_float4(m[12], m[13], m[14], m[15])))
var params = Params(invVP: invVP, sun: sun, size: simd_uint2(UInt32(W), UInt32(H)), aoRays: aoRays, frame: 0)
let stats = dev.makeBuffer(length: 16, options: .storageModeShared)!
var traceMs = [Double]()
for i in 0..<5 {
    params.frame = UInt32(i); memset(stats.contents(), 0, 16)
    traceMs.append(timed("追踪（主射线 + 太阳阴影射线\(aoRays > 0 ? " + \(aoRays) 条 AO 射线" : "")，\(W)x\(H)）") { cb in
        let e = cb.makeComputeCommandEncoder()!; e.setComputePipelineState(pso); e.setTexture(tex, index: 0); e.setAccelerationStructure(accel, bufferIndex: 0)
        e.setBytes(&params, length: MemoryLayout<Params>.size, index: 1); e.setBuffer(stats, offset: 0, index: 2)
        e.dispatchThreads(MTLSize(width: W, height: H, depth: 1), threadsPerThreadgroup: MTLSize(width: 8, height: 8, depth: 1)); e.endEncoding() })
}
let hits = stats.contents().load(as: UInt32.self)
print(String(format: "命中像素 %u / %d（%.1f%%）；构建中位 %.3f ms；追踪中位 %.3f ms", hits, W * H, 100.0 * Double(hits) / Double(W * H), buildMs.sorted()[1], traceMs.sorted()[2]))
var px = [UInt8](repeating: 0, count: W * H * 4)
tex.getBytes(&px, bytesPerRow: W * 4, from: MTLRegionMake2D(0, 0, W, H), mipmapLevel: 0)
let cs = CGColorSpaceCreateDeviceRGB()
let ctx = CGContext(data: &px, width: W, height: H, bitsPerComponent: 8, bytesPerRow: W * 4, space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
let img = ctx.makeImage()!
let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dst, img, nil); CGImageDestinationFinalize(dst)
print("已写 \(args[2])（R = 太阳可见，G = AO，B = 近远）")

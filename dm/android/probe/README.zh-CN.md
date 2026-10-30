# 安卓真机探针（P1：CPU 与系统探针）

宿主方案在一台安卓机上成不成立，一次跑完、逐项报 OK / FAIL。同一套探针两种跑法：

- **探针 App**（`app/`，包名 `com.xiaoboluo.d2r.probe`，无权限、不联网、不含游戏内容）：在**应用身份**下跑（SELinux `untrusted_app`、ART 的信号链在场、App 的内存与地址空间限制），这是游戏 App 实际面对的环境。报告写到 App 的 `files/probe.txt` 与外部私有目录 `Android/data/com.xiaoboluo.d2r.probe/files/probe.txt`（`adb pull`），同时打 logcat（标签 `dmprobe`）并显示在屏幕上。
- **命令行 `dmprobe`**（`main.c`）：`adb push` 到 `/data/local/tmp` 在 adb shell 里跑（**shell 身份**，只作对照：shell 下成功不代表 App 沙箱成功）。

探项（`dm_probe.c`，接口 `dm_probe.h`）：

1. 身份：页大小、CPU 特性（HWCAP）、各核 MIDR、内核、系统 / SoC 属性、SELinux、seccomp、内存与资源上限。支持记录的单位是「型号 + RAM + 系统 build + GPU deviceID + driverVersion + 页大小 + 包哈希」，这些都在报告里（包哈希由 App 侧算自己的 APK）。
2. bionic 键表：改写器 `DM_TARGET=android` 的线程槽取法（`tpidr_el0` → TCB[-1] → bionic_tls + 键号 × 16 + 8），多键、32 线程并发、线程创建退出 1000 轮，都经一段运行时生成的补丁同款代码实读。
3. 可执行内存：匿名页写入后改只读+执行（装载器 `map_image` 的做法）、RX↔RW 来回改码 100 轮、RWX（只记录）、文件映射执行（应用数据目录预期被拒，只记录）。
4. 门调用形状：生成代码经字面量指针 `blr` 调宿主 C 函数、生成代码里发 `svc #0`；`cntfrq_el0` / `cntvct_el0` 用户态能否读。
5. x18 跨系统调用是否被改；新线程有没有备用信号栈。
6. 信号与脏页：SIGSEGV 现场里 **ESR（写位 WnR）在不在**——`crash.c` 只把写缺页交给 `nvn_dirty.c` 的脏页跟踪，没有 ESR 就等于脏页跟踪永远不接手；写保护页 → 缺页 → 解保护 → 重执行，主线程、工作线程、8 线程并发各一遍；野地址能与脏页范围区分。
7. 页语义：4 KB 子范围 `mprotect` 在 16 KB 页系统上的表现（客体 4 KB 语义必须按宿主页对齐）；`MADV_FREE` + `PROT_NONE` 回收再开。
8. 客体地址空间：照 `hle_svc.c` 预留 5120 MiB 逐档降级到 4864 MiB、一次 `mprotect` 开 3285 MiB 堆、触碰 256 MiB 看 RSS、回收；更大的 NORESERVE 保留只记录。
9. 映像布局：代码 RX / 键页 RO / 数据 RW / 补丁区 RX 四段照装载器切出，代码区跳补丁区、补丁区读数据区。
10. Vulkan（`probe_vk.c`）：设备 / 驱动身份（deviceID、driverVersion、driverName、UUID）、子组、队列族、内存类型、描述符与各项上限、特性、关键格式（D16 / X8D24 / D24S8 / D32 的采样与线性过滤、BC1–7、ASTC、无格式存储图像）、扩展清单。只查不画。

输出一行一项：`OK` / `FAIL` / `INFO` / `KV 键=值`，最后一行 `RESULT OK` 或 `RESULT FAIL <失败数>`；`dm-work/run/android/tools/probe_to_json.py` 把报告汇总成 JSON 进证据。

构建（工具链同主 App：SDK 36、NDK 29.0.14206865、CMake 3.31.6）：

```sh
gradle -p dm/android/probe :app:assembleDebug        # 探针 App
cmake -S dm/android/probe -B <仓外目录> -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 && ninja -C <仓外目录> dmprobe_cli
```

`dm_uctx.h` 直接用宿主的（`../../host/dm_uctx.h`），ESR 的取法与 `crash.c` 完全相同。探针不读别的 App 的数据，报告里没有用户名、账号、路径以外的个人信息；设备型号 / 系统 build / GPU 身份是支持记录需要的，属本机私人材料，不进仓库。

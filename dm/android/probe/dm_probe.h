// 安卓真机探针（接口）：同一套探针既编成命令行程序（adb shell，shell 身份），也编进探针 App（应用身份、SELinux untrusted_app、ART 信号链在场）。
// 输出一行一项：OK / FAIL / INFO / KV（键=值，供 probe_to_json.py 汇总）；最后一行 RESULT OK|FAIL <失败数>。
// Android device probe (interface): the same probes build both as a command-line program (adb shell, shell identity) and into the probe App
// (app identity, SELinux untrusted_app, ART's signal chain present). One item per line: OK / FAIL / INFO / KV (key=value, for probe_to_json.py);
// the last line is RESULT OK|FAIL <failures>.
#pragma once
#include <stdio.h>
// scratch：可写目录（App 的 files 目录 / 命令行的 /data/local/tmp），文件映射执行的探项在这里建临时文件；NULL 跳过
// scratch: a writable directory (the App's files dir / /data/local/tmp for the command line) for the file-backed execution probe; NULL skips it
int dm_probe_run(FILE *out, const char *scratch);
void dm_probe_vulkan(FILE *out);   // probe_vk.c：设备 / 驱动身份与能力 | device / driver identity and capabilities

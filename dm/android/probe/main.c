// 命令行探针：dmprobe [可写目录 [外部目录]]（缺省 /data/local/tmp，外部目录下 shaders/ 放 .xgs 给 M6）。adb push 后在 adb shell 里跑（shell 身份）；应用身份用探针 App。
// Command-line probe: dmprobe [writable dir] (default /data/local/tmp). adb push and run from adb shell (shell identity); the probe App covers the app identity.
#include "dm_probe.h"
int main(int argc, char **argv) { return dm_probe_run(stdout, argc > 1 ? argv[1] : "/data/local/tmp", argc > 2 ? argv[2] : NULL) != 0; }

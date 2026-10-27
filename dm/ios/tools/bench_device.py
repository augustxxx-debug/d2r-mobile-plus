#!/usr/bin/env python3
"""功耗基准的设备状态查询（power_bench.sh 在设备锁内调用）：按目标 App 的实际安装目录识别进程，不按可执行文件名。
Device state for the power benchmark (called by power_bench.sh while holding the device lock): processes are matched by the target App's
installed bundle directory, never by executable name (several ports share the name DmIOS). Same rules as backup_saves.py.

用法 | Usage:
  bench_device.py state <device> <bundle>   打印两行：app=<absent|idle|running:<pid>>、savetest=<yes|no>；设备读不到退出码 2
                                            prints app=<absent|idle|running:<pid>> and savetest=<yes|no>; exit 2 when the device cannot be read
  bench_device.py pid <device> <bundle>     打印目标 App 的进程号（没在跑则不打印）；设备读不到退出码 2
  bench_device.py pids <device> <bundle>    打印目标 App 的全部进程号（空格分隔）；设备读不到退出码 2
                                            prints all of the target App's pids (space-separated); exit 2 when the device cannot be read
  bench_device.py ready <device> <bundle>   真的查一次设备（带超时的只读查询）；能读到退出码 0，否则 2。设备“在清单里”不等于“连得上”（二审 R02）
                                            does one real read-only query with a timeout; exit 0 when it answers, 2 otherwise. Being listed is not being reachable (second review R02)
  bench_device.py mtime <device> <bundle> <dir> <name>
                                            打印 App 容器里 <dir>/<name> 的修改时间（设备给的原文，没有该文件则不打印）；设备读不到退出码 2
                                            prints the modification time of <dir>/<name> in the App container (as the device reports it; nothing if absent); exit 2 when unreadable
"""
import json
import subprocess
import sys
from pathlib import PurePosixPath
from urllib.parse import unquote, urlparse


class DeviceError(RuntimeError):
    pass


def device_path(value):
    parsed = urlparse(value or '')
    if parsed.scheme not in ('', 'file') or parsed.netloc not in ('', 'localhost'):
        raise DeviceError('无法核对路径：%s' % value)
    path = PurePosixPath(unquote(parsed.path))
    if not path.is_absolute() or '..' in path.parts:
        raise DeviceError('路径异常：%s' % value)
    text = str(path)
    return '/var/' + text[len('/private/var/'):] if text.startswith('/private/var/') else text


def app_folder(apps, bundle):
    """apps 为 devicectl info apps --bundle-id 的结果；没装返回 None，不能唯一核对时抛错 | None when not installed; raises when it cannot be identified uniquely"""
    if not apps:
        return None
    if len(apps) != 1 or apps[0].get('bundleIdentifier') != bundle:
        raise DeviceError('无法唯一核对目标 App')
    folder = device_path(apps[0].get('url', ''))
    if not folder.endswith('.app'):
        raise DeviceError('目标 App 路径不是 .app')
    return folder


def target_pids(folder, processes):
    if not folder:
        return []
    return [p.get('processIdentifier') for p in processes
            if p.get('executable') and device_path(p['executable']).startswith(folder + '/')]


def has_savetest(documents):
    return any(f.get('name') == 'save.test' and f.get('resources', {}).get('isDirectory') for f in documents)


def file_mtime(files, name):
    """files 为 devicectl info files 的结果；返回 name 的 metadata.lastModDate（原文），没有返回 None | lastModDate of name as reported, None when absent"""
    for f in files:
        if f.get('name') == name and not f.get('resources', {}).get('isDirectory'):
            return f.get('metadata', {}).get('lastModDate') or None
    return None


def device_json(device, *arguments, timeout=30):
    # 外层再兜一道（多给 2 秒起进程）：devicectl 自己的超时不一定守得住，查询总时长要计进调用方的预算（四审 Q4-03）
    # An outer guard (2 extra seconds for process start-up): devicectl's own timeout is not always honoured, and the query time counts against the caller's budget (fourth review Q4-03)
    try:
        r = subprocess.run(['xcrun', 'devicectl', 'device', *arguments, '--device', device, '--timeout', str(timeout), '--json-output', '-'],
                           capture_output=True, text=True, timeout=timeout + 2)
    except subprocess.TimeoutExpired:
        raise DeviceError('devicectl 超时（%d 秒）：%s' % (timeout, ' '.join(arguments)))
    if r.returncode:
        raise DeviceError('devicectl 失败：%s' % ' '.join(arguments))
    data = json.loads(r.stdout)
    if data.get('info', {}).get('outcome') != 'success':
        raise DeviceError('设备返回失败：%s' % ' '.join(arguments))
    return data['result']


def query(device, bundle, want_savetest):
    folder = app_folder(device_json(device, 'info', 'apps', '--bundle-id', bundle)['apps'], bundle)
    pids = target_pids(folder, device_json(device, 'info', 'processes')['runningProcesses']) if folder else []
    save = None
    if folder and want_savetest:
        docs = device_json(device, 'info', 'files', '--domain-type', 'appDataContainer', '--domain-identifier', bundle,
                           '--subdirectory', 'Documents', '--no-recurse')['files']
        save = has_savetest(docs)
    return folder, pids, save


def main(argv):
    if len(argv) in (4, 5) and argv[1] == 'ready':   # 可选第 5 个参数 = 这次查询最多几秒（5–30，缺省 30） | optional 5th argument = seconds this query may take (5–30, default 30)
        try:
            timeout = int(argv[4]) if len(argv) == 5 else 30
            if not 5 <= timeout <= 30: raise ValueError('查询超时要在 5–30 秒：%s' % argv[4])
            device_json(argv[2], 'info', 'apps', '--bundle-id', argv[3], timeout=timeout); return 0
        except (DeviceError, ValueError, KeyError) as e:
            print('🔴 ' + str(e), file=sys.stderr); return 2
    if len(argv) == 6 and argv[1] == 'mtime':
        try:
            m = file_mtime(device_json(argv[2], 'info', 'files', '--domain-type', 'appDataContainer', '--domain-identifier', argv[3],
                                       '--subdirectory', argv[4], '--no-recurse')['files'], argv[5])
        except (DeviceError, ValueError, KeyError) as e:
            print('🔴 ' + str(e), file=sys.stderr); return 2
        if m: print(m)
        return 0
    if len(argv) != 4 or argv[1] not in ('state', 'pid', 'pids'):
        print(__doc__, file=sys.stderr); return 64
    try:
        folder, pids, save = query(argv[2], argv[3], argv[1] == 'state')
    except DeviceError as e:
        print('🔴 ' + str(e), file=sys.stderr); return 2
    if argv[1] == 'pid':
        if pids: print(pids[0])
        return 0
    if argv[1] == 'pids':
        print(' '.join(str(p) for p in pids))
        return 0
    print('app=' + ('absent' if not folder else 'running:%s' % pids[0] if pids else 'idle'))
    print('savetest=' + ('yes' if save else 'no'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

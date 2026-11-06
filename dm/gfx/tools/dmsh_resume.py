#!/usr/bin/env python3
# 着色器翻译断点续做（B218）：手机端生成被切后台 / 杀进程打断后，下一轮只补没做完的着色器，做过的不重译。
#   - 完成清单 <输出目录>/.done.tsv：每行「键 \t 文件名 \t 字节数 \t sha256」，一个着色器写完（先写临时文件再改名）才追加一行并刷盘；
#     读清单时逐个核文件还在、大小与哈希对得上，半截文件或被改过的一律当没做（文件删掉重做）。
#   - 清单里没有的 .xgs（上一轮写到一半被杀的、或别的来源）不信，删掉重做。
#   - 分小块跑：每块先翻译再打包，块做完就落盘，被杀最多丢正在跑的几块。
# Shader translation resume (B218): when on-phone generation is interrupted (backgrounded / killed), the next round only does the shaders that are not
# finished; finished ones are never translated again.
#   - Done list <out>/.done.tsv: one line "key \t file \t bytes \t sha256", appended and flushed only after a shader is fully written (temp file + rename);
#     on load every entry is checked (file present, size and hash match); a partial or altered file counts as not done (deleted and redone).
#   - A .xgs not on the list (half-written by a killed round, or from elsewhere) is not trusted: deleted and redone.
#   - Work runs in small chunks, each translated then packed and written; a kill loses at most the chunks in flight.
import concurrent.futures as cf, hashlib, os

DONE = '.done.tsv'


def _sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()


def load_done(out):
    """核过的完成项 {键: 文件名}；坏行、缺文件、大小或哈希不符的丢掉，清单外的 .xgs 删掉 | verified done items; untrusted .xgs files are deleted"""
    good, path = {}, os.path.join(out, DONE)
    try: lines = open(path, encoding='utf-8').read().splitlines()
    except OSError: lines = []
    for ln in lines:
        w = ln.split('\t')
        if len(w) != 4 or not w[2].isdigit() or '/' in w[1] or not w[1].endswith('.xgs'): continue
        f = os.path.join(out, w[1])
        try:
            if os.path.getsize(f) == int(w[2]) and _sha(f) == w[3]: good[w[0]] = w[1]
        except OSError: pass
    keep = set(good.values())
    if os.path.isdir(out):
        for n in os.listdir(out):
            if n.endswith('.xgs') and n not in keep: os.remove(os.path.join(out, n))
    # 清单只留核过的，免得坏行越积越多 | rewrite the list with verified entries only
    os.makedirs(out, exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        for k, n in sorted(good.items()):
            p = os.path.join(out, n); f.write('%s\t%s\t%d\t%s\n' % (k, n, os.path.getsize(p), _sha(p)))
    os.replace(tmp, path)
    return good


def write_output(out, name, data):
    """先写临时文件再改名，写完才算数 | temp file then rename"""
    tmp = os.path.join(out, '.' + name + '.part')
    with open(tmp, 'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.replace(tmp, os.path.join(out, name))


class DoneLog:
    """追加完成项（线程安全，每行刷盘）| appends done items (thread-safe, flushed per line)"""
    def __init__(self, out):
        import threading
        self.out, self.lock = out, threading.Lock()
        self.f = open(os.path.join(out, DONE), 'a', encoding='utf-8')
    def add(self, key, name, data):
        line = '%s\t%s\t%d\t%s\n' % (key, name, len(data), hashlib.sha256(data).hexdigest())
        with self.lock: self.f.write(line); self.f.flush(); os.fsync(self.f.fileno())
    def close(self): self.f.close()


def pending(keys, done):
    """本轮要做的键：清单里核过的跳过 | keys this round: verified done ones are skipped"""
    return [k for k in keys if k not in done]


def commit(out, log, key, name, data):
    """一个着色器做完：先写成品（临时文件 + 改名），写成了才记清单——顺序反了，被杀时清单会指向不存在 / 半截的文件
    a finished shader: write the bundle first (temp + rename), record it only once written -- the other order would leave the list pointing at a missing / partial file"""
    write_output(out, name, data)
    log.add(key, name, data)


# 续做缓存的输入指纹（B218 终审 P1）：凡是会改变翻译产物或决定产物收不收的输入，内容都进指纹，任一变了就换新缓存目录、旧的作废
#   files = 单个文件（翻译 / 打包脚本及其本地依赖、翻译器、主程序、着色器表、spirv-opt / spirv-val；缺的记「缺」），dirs = 整个目录逐文件（电脑上 dotnet publish 的翻译器目录），
#   packs = 着色器包目录（逐个包的名字与全部内容），options = 影响产物的翻译选项与运行时版本。只看大小 / 修改时间不够：同长度改动必须换指纹
# Input fingerprint of the resume cache (B218 final review P1): the content of every input that changes a translation or decides whether it is accepted;
# any change means a new cache directory. Sizes / mtimes are not enough: a same-length change must change the fingerprint
VERSION = 'dmsh-resume-v2'


def _feed(h, tag, path):
    h.update(('%s\0' % tag).encode())
    if path is None or not os.path.isfile(path): h.update(b'missing\0'); return
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    h.update(b'\0')


def input_fingerprint(product, files, dirs, packs, options):
    h = hashlib.sha256(('%s\0%s\0' % (VERSION, product)).encode())
    for i, f in enumerate(files): _feed(h, 'file%d' % i, f)
    for i, d in enumerate(dirs):
        for root, ds, fs in os.walk(d):
            ds.sort()
            for n in sorted(fs): p = os.path.join(root, n); _feed(h, 'dir%d/%s' % (i, os.path.relpath(p, d).replace(os.sep, '/')), p)
    for n in sorted(os.listdir(packs)): _feed(h, 'pack/' + n, os.path.join(packs, n))
    for k in sorted(options): h.update(('opt %s=%s\0' % (k, options[k])).encode())
    return h.hexdigest()


# dmsh_vk.py 与它在本仓里的依赖（相对 dm/gfx/tools）：内容都进输入指纹；test_dmsh_resume 核这张表覆盖 dmsh_vk 的全部本地导入
# dmsh_vk.py and its in-repo dependencies (relative to dm/gfx/tools): their content goes into the fingerprint; the test checks it covers every local import
SOURCES = ('dmsh_vk.py', 'dmsh_resume.py', 'dmsh_facts.py', 'pack_game_shaders.py', '../../verify/nsoimg.py')
INPUTS = '.inputs'


def check_inputs(out, fp):
    """输出目录记的输入指纹和这次的不同（或没记）：完成清单、成品、临时文件全删，记上新指纹，返回 False；相同返回 True
    if the output directory's recorded fingerprint differs from this one (or is missing): delete the done list, bundles and temp files, record the new one, return False"""
    os.makedirs(out, exist_ok=True); path = os.path.join(out, INPUTS)
    try: old = open(path, encoding='utf-8').read().strip()
    except OSError: old = None
    if old == fp: return True
    for n in os.listdir(out):
        if n.endswith('.xgs') or n.endswith('.part') or n in (DONE, DONE + '.tmp'): os.remove(os.path.join(out, n))
    with open(path + '.tmp', 'w', encoding='utf-8') as f: f.write(fp + '\n'); f.flush(); os.fsync(f.fileno())
    os.replace(path + '.tmp', path)
    return False


def run_chunks(keys, chunk, nj, translate, finish):
    """keys 分成每块 chunk 个、nj 路并发；每块 translate(块) 之后对块里每个键 finish(键) → (键, 状态, 说明)。返回全部结果
    split keys into chunks of `chunk`, `nj` in parallel; each chunk is translate(chunk) then finish(key) for each key -> (key, status, note)"""
    blocks = [keys[i:i + chunk] for i in range(0, len(keys), chunk)]
    def one_block(b):
        errs = translate(b) or {}
        return [(k, '翻译失败', errs[k]) if k in errs else finish(k) for k in b]
    res = []
    with cf.ThreadPoolExecutor(max(1, nj)) as ex:
        for r in ex.map(one_block, blocks): res.extend(r)
    return res

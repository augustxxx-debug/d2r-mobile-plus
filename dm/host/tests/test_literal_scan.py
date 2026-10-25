"""Player-facing interpolated strings must not disappear on Python 3.12+."""
import importlib.util
from pathlib import Path

path = Path(__file__).resolve().parents[2] / 'i18n/scan_literals.py'
spec = importlib.util.spec_from_file_location('literal_scan', path)
scan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scan)
source = '''"""模块说明不显示给玩家。"""
label = f"画质 {quality}"
label2 = rf"帧率 {{fps}} {fps}"
label3 = f"提示 {f'内部 {value}'}"
print(f"调试 {value}")
raise ValueError(f"构建失败 {value}")
label4 = "普通文本"
'''
text, literals = scan.strip_py(source)
visible = [value for pos, value in literals
           if scan.CJK.search(value) and scan.caller(text, pos) not in scan.SINK]
assert len(visible) == 4, visible
for expected in ('画质', '帧率', '提示', '普通文本'):
    assert any(expected in value for value in visible), (expected, visible)
assert not any('模块说明' in value for value in visible), visible
print('PASS localized literal audit: ordinary/raw/nested f-strings remain visible; logs and docstrings excluded')

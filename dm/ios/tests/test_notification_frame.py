"""用合成像素检查通知端部保真与文字区域不透明。
Notification art must preserve game caps and keep its text area opaque."""
import sys
from pathlib import Path
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from gen_notification_frame import stretch_button, MIN_LAYOUT_WIDTH

source=np.random.default_rng(7).integers(30,180,(59,235,4),dtype=np.uint8)
source[:,:,3]=255
for width in (584, MIN_LAYOUT_WIDTH//2):
    result=np.array(stretch_button(Image.fromarray(source),width,122))
    assert result.shape==(122,width,4)
    for sx,dx in ((0,0),(209,width-26)):
        assert np.array_equal(result[:12,dx:dx+26],source[:12,sx:sx+26])
        assert np.array_equal(result[-12:,dx:dx+26],source[-12:,sx:sx+26])
        assert np.array_equal(result[51:70,dx:dx+26],source[20:39,sx:sx+26])
    assert np.all(result[9:-9,26:-26,3]==255)
    assert result[10,26:-26,:3].mean()>result[-11,26:-26,:3].mean()+40
try:
    stretch_button(Image.fromarray(source),60,122)
except ValueError:
    pass
else:
    raise AssertionError('undersized frame accepted')
print('PASS notification art: four caps, two medallions, opaque text area, gradient and minimum size')

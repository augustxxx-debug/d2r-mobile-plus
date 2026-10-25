"""从原游戏无字按钮素材构建完整通知精灵。
Build a single notification sprite from the game's unlabelled button art."""
import numpy as np
from PIL import Image

SOURCE = 'hd/global/ui/FrontEnd/HD/Final/FrontEnd_ButtonMed.sprite'
TARGET = 'data/hd/global/ui/ztouch_notification/frame.sprite'
MIN_LAYOUT_WIDTH = 1680  # Longer localized sentences need space beyond the seven-slot HUD row.


def stretch_button(button, width, height):
    """保留角部和蓝色装饰，仅拉伸连接条。 | Preserve corners and blue medallions; extend only connecting strips."""
    button = button.convert('RGBA')
    sw, sh = button.size
    if (sw, sh) != (235, 59) or width < 80 or height < 59:
        raise ValueError('Unexpected button art or undersized notification')
    xs, ys = (0, 26, sw - 26, sw), (0, 12, 20, 39, 47, sh)
    upper = (height - 43) // 2
    dx, dy = (0, 26, width - 26, width), (0, 12, 12 + upper, 31 + upper, height - 12, height)
    out = Image.new('RGBA', (width, height))
    for row in range(5):
        for col in range(3):
            tile = button.crop((xs[col], ys[row], xs[col + 1], ys[row + 1]))
            size = (dx[col + 1] - dx[col], dy[row + 1] - dy[row])
            if tile.size != size:
                tile = tile.resize(size, Image.Resampling.BICUBIC)
            out.paste(tile, (dx[col], dy[row]))
    # 保留原版石材纹理，文字区域使用连续暗色纵向渐变；端部和侧面装饰保持不变。
    # Keep the game's stone grain, but give the text area a continuous dark
    # vertical gradient. The decorative caps and side medallions stay untouched.
    stone = button.crop((26, 9, sw - 26, sh - 9)).resize((width - 52, height - 18), Image.Resampling.BICUBIC)
    pixels = np.array(stone, dtype=np.float32)
    means = pixels[:, :, :3].mean(axis=(1, 2)).clip(1)
    tone = np.linspace(76, 22, stone.height, dtype=np.float32)
    pixels[:, :, :3] *= (tone / means)[:, None, None]
    pixels[:, :, 3] = 255
    out.paste(Image.fromarray(pixels.clip(0, 255).astype(np.uint8)), (26, 9))
    return out


def build(romfs, output, width, height):
    import romfs_art
    import gen_widestash_art as codec
    original = romfs_art.read(romfs, SOURCE)
    image = stretch_button(romfs_art.frame(romfs, SOURCE), width, height)
    with open(output, 'wb') as f:
        f.write(codec.pack(codec.lib(), original, codec.bc3(image), width, height, width))
    return image

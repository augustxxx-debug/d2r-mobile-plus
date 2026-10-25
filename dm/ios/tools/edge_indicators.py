"""按原版布局基准将角落标志预藏屏外，等待首次定位。
Keep corner indicators outside the viewport until their first placement."""
import re

LAYOUTS = {
    'controller/console/loadscreenpanelhd.json': {
        'FullGlow': (-400, -400), 'Logo': (-399, -392),
        'ResurrectedText': (-370, -226), 'BottomFlames': (-319, -208)},
    'mainmenuloadingpanelhd.json': {
        'FullGlow': (-400, -420), 'Logo': (-399, -412),
        'ResurrectedText': (-370, -246), 'BottomFlames': (-319, -228)},
    'saveindicatorpanelhd.json': {'SaveIndicatorPanel': (0, 150)},
}


def prepare(text, positions):
    for name, (x, y) in positions.items():
        name_match = re.search(r'"name"\s*:\s*"' + re.escape(name) + '"', text)
        if not name_match:
            raise ValueError('Missing corner indicator: ' + name)
        rect = re.compile(r'"rect"\s*:\s*\{[^}]*\}').search(text, name_match.end())
        if not rect or rect.start() - name_match.end() > 200:
            raise ValueError('Missing corner rectangle: ' + name)
        fields = rect.group()
        if not re.search(r'"x"\s*:\s*' + str(x) + r'\s*[,}]', fields) or not re.search(r'"y"\s*:\s*' + str(y) + r'\s*[,}]', fields):
            raise ValueError('Unexpected game layout for ' + name)
        fields = re.sub(r'("x"\s*:\s*)-?\d+', r'\g<1>99999', fields, count=1)
        text = text[:rect.start()] + fields + text[rect.end():]
    return text

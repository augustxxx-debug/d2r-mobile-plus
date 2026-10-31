#!/usr/bin/env python3
# 物品栏纵向图标条的两个图标（用户定：背包 B1 皮囊、护身符 C2 三枚叠放最终切图）。全部自绘，只有铜框纹理取原版按钮图，运行时传进来，图和框都不进仓库。
# The two icons of the inventory's vertical strip (user-chosen: bag B1 pouch, charm C2 three stacked, final cut). All self-drawn; only the copper frame texture comes from an original button image, passed in at run time, so neither goes into the repo.
from PIL import Image,ImageDraw,ImageFilter,ImageChops,ImageOps
import math
SS=384
fr=None   # 铜框，icons() 从原版 RepairAll_Button 取 | Copper frame, taken by icons() from the original RepairAll_Button
def noise(s,sig,amp=60):
    n=Image.effect_noise((SS,SS),sig).convert('L'); return n
def tint(mask,c0,c1):
    return ImageOps.colorize(mask,c0,c1)
def bevel(mask,rad,hi=.8,lo=.8):
    b=mask.filter(ImageFilter.GaussianBlur(rad))
    h=ImageChops.subtract(b,ImageChops.offset(b,int(rad),int(rad))); l=ImageChops.subtract(b,ImageChops.offset(b,-int(rad),-int(rad)))
    return h.point(lambda v:min(255,int(v*hi*2))),l.point(lambda v:min(255,int(v*lo*2)))
def sack(d,cx,top,w,h,fill=255):
    # 皮囊：收口在上、下鼓，用曲线折线
    pts=[]; neck=w*.26
    for i in range(0,41):
        t=i/40; y=top+h*.20+t*h*.80
        r=neck+(w/2-neck)*min(1,math.sin(min(1,t*1.5)*math.pi/2))
        r*=math.sqrt(max(0,1-max(0,(t-.62)/.38)**2))
        pts.append((cx+r,y))
    pts2=[(2*cx-x,y) for x,y in reversed(pts)]
    d.polygon(pts+pts2,fill=fill)
    # 口沿外翻
    d.polygon([(cx-neck*.9,top+h*.2),(cx-neck*1.5,top+h*.02),(cx-neck*.5,top+h*.08),(cx,top),(cx+neck*.5,top+h*.08),(cx+neck*1.5,top+h*.02),(cx+neck*.9,top+h*.2)],fill=fill)
def make(kind):
    im=Image.new('RGB',(SS,SS),(0,0,0))
    # 底：深皮革 / 暗羊皮
    n=noise(SS,70).filter(ImageFilter.GaussianBlur(1.2)); bg=tint(n,(14,10,7),(52,38,26)); im.paste(bg)
    v=Image.new('L',(SS,SS),0); ImageDraw.Draw(v).ellipse((30,30,SS-30,SS-30),fill=255); v=v.filter(ImageFilter.GaussianBlur(50)); im=Image.composite(im,Image.new('RGB',(SS,SS),(6,4,3)),v)
    m=Image.new('L',(SS,SS),0); d=ImageDraw.Draw(m)
    if kind=='bag': sack(d,SS/2,66,214,266)
    else: sack(d,SS/2,84,178,226)
    n2=noise(SS,50).filter(ImageFilter.GaussianBlur(1)); 
    col=((46,28,15),(122,80,46)) if kind=='bag' else ((30,22,28),(92,70,80))
    body=tint(n2,*col)
    # 明暗：左上亮，右下暗
    g=Image.new('L',(SS,SS),0); ImageDraw.Draw(g).ellipse((SS*.18,SS*.22,SS*.68,SS*.8),fill=255); g=g.filter(ImageFilter.GaussianBlur(44))
    body=Image.composite(ImageChops.add(body,Image.new('RGB',(SS,SS),(46,34,22))),ImageChops.multiply(body,Image.new('RGB',(SS,SS),(120,120,120))),g)
    # 暗描边
    sh=m.filter(ImageFilter.GaussianBlur(9)); im=Image.composite(Image.new('RGB',(SS,SS),(0,0,0)),im,sh.point(lambda x:int(x*.85)))
    im=Image.composite(body,im,m)
    fold=Image.new('L',(SS,SS),0); fd=ImageDraw.Draw(fold); tp=66 if kind=='bag' else 84; hh=266 if kind=='bag' else 226
    for dx,dy in ((-46,70),(-20,88),(16,92),(44,66)): fd.line((SS/2+dx*.3,tp+hh*.24,SS/2+dx,tp+hh*.24+dy),fill=255,width=5)
    fold=ImageChops.multiply(fold.filter(ImageFilter.GaussianBlur(3)),m); body2=ImageChops.multiply(im,Image.new('RGB',(SS,SS),(150,140,130))); im=Image.composite(body2,im,fold)
    h,l=bevel(m,5); im=ImageChops.add(im,Image.merge('RGB',[h.point(lambda v:int(v*.5))]*3)); im=ImageChops.subtract(im,Image.merge('RGB',[l.point(lambda v:int(v*.6))]*3))
    # 铜：取原版框的纹理
    cop=fr.convert('RGB').crop((10,120,70,264)).resize((SS,SS),Image.LANCZOS)
    cop=ImageChops.add(cop,Image.new('RGB',(SS,SS),(40,26,8)))
    t=Image.new('L',(SS,SS),0); e=ImageDraw.Draw(t); top=66 if kind=='bag' else 84; h0=266 if kind=='bag' else 226
    y=top+h0*.2; hw=(214 if kind=='bag' else 178)*.17
    e.rounded_rectangle((SS/2-hw,y-9,SS/2+hw,y+9),8,fill=255)   # 束口铜箍
    if kind=='bag':
        e.line((SS/2+hw*.5,y+8,SS/2+hw*1.3,y+62),fill=255,width=8); e.ellipse((SS/2+hw*1.3-9,y+56,SS/2+hw*1.3+9,y+74),fill=255)   # 垂绳铜坠
        pass
    else:
        c=(SS/2,top+h0*.62); r=44
        e.polygon([(c[0],c[1]-r),(c[0]+r*.78,c[1]),(c[0],c[1]+r),(c[0]-r*.78,c[1])],fill=255)   # 菱形护符
        e.polygon([(c[0],c[1]-r*.52),(c[0]+r*.4,c[1]),(c[0],c[1]+r*.52),(c[0]-r*.4,c[1])],fill=0)
        e.ellipse((c[0]-10,c[1]-10,c[0]+10,c[1]+10),fill=255)
        e.line((c[0],y+8,c[0],c[1]-r),fill=255,width=6)
    ct=Image.composite(cop,im,t); h,l=bevel(t,3); ct=ImageChops.add(ct,Image.merge('RGB',[h]*3)); ct=ImageChops.subtract(ct,Image.merge('RGB',[l]*3)); im=ct
    # 原版铜框压上
    ring=Image.new('L',(SS,SS),255); ImageDraw.Draw(ring).rectangle((54,54,SS-54,SS-54),fill=0); ring=ring.filter(ImageFilter.GaussianBlur(2))
    a=ImageChops.multiply(ring,fr.split()[3]); im=Image.composite(fr.convert('RGB'),im,a)
    return im
def base():
    n=noise(SS,70).filter(ImageFilter.GaussianBlur(1.2)); im=tint(n,(14,10,7),(52,38,26))
    v=Image.new('L',(SS,SS),0); ImageDraw.Draw(v).ellipse((30,30,SS-30,SS-30),fill=255); v=v.filter(ImageFilter.GaussianBlur(50)); return Image.composite(im,Image.new('RGB',(SS,SS),(6,4,3)),v)
cop=None
def lay(im,m,tex,rad=5,sh=True):
    if sh:
        s=m.filter(ImageFilter.GaussianBlur(8)); im=Image.composite(Image.new('RGB',(SS,SS),(0,0,0)),im,s.point(lambda x:int(x*.85)))
    im=Image.composite(tex,im,m); h,l=bevel(m,rad); im=ImageChops.add(im,Image.merge('RGB',[h.point(lambda v:int(v*.6))]*3)); return ImageChops.subtract(im,Image.merge('RGB',[l.point(lambda v:int(v*.7))]*3))
def bone(c0=(92,74,48),c1=(214,190,142)):
    return tint(noise(SS,46).filter(ImageFilter.GaussianBlur(1)),c0,c1)
def carve(im,m,dark=(38,24,12)):
    return Image.composite(Image.new('RGB',(SS,SS),dark),im,m.filter(ImageFilter.GaussianBlur(1)))
def ringframe(im):
    ring=Image.new('L',(SS,SS),255); ImageDraw.Draw(ring).rectangle((54,54,SS-54,SS-54),fill=0); ring=ring.filter(ImageFilter.GaussianBlur(2))
    return Image.composite(fr.convert('RGB'),im,ImageChops.multiply(ring,fr.split()[3]))
def tablet(d,cx,cy,w,h,f=255): d.rounded_rectangle((cx-w/2,cy-h/2,cx+w/2,cy+h/2),w*.28,fill=f)
def c1():
    im=base(); m=Image.new('L',(SS,SS),0); tablet(ImageDraw.Draw(m),SS/2,SS/2,120,240); im=lay(im,m,bone())
    k=Image.new('L',(SS,SS),0); e=ImageDraw.Draw(k); cx=SS/2
    for y0 in (118,262): e.line((cx-38,y0,cx+38,y0),fill=255,width=7)
    e.polygon([(cx,132),(cx+38,192),(cx,252),(cx-38,192)],outline=255,width=8); e.polygon([(cx,162),(cx+17,192),(cx,222),(cx-17,192)],fill=255)
    im=carve(im,k); return ringframe(im)
def c2():
    im=base(); cx=SS/2
    for (x,y,w,h,c) in ((cx+56,SS/2+48,70,120,((70,54,34),(160,132,92))),(cx-58,SS/2+22,84,174,((82,64,40),(190,160,112))),(cx+2,SS/2-10,104,230,((92,74,48),(222,198,150)))):
        m=Image.new('L',(SS,SS),0); tablet(ImageDraw.Draw(m),x,y,w,h); im=lay(im,m,bone(*c))
        k=Image.new('L',(SS,SS),0); e=ImageDraw.Draw(k); e.polygon([(x,y-h*.2),(x+w*.24,y),(x,y+h*.2),(x-w*.24,y)],outline=255,width=6); im=carve(im,k)
    return ringframe(im)
def c3():
    im=base(); cx=cy=SS/2; R=124
    m=Image.new('L',(SS,SS),0); ImageDraw.Draw(m).ellipse((cx-R,cy-R,cx+R,cy+R),fill=255); ImageDraw.Draw(m).ellipse((cx-R+24,cy-R+24,cx+R-24,cy+R-24),fill=0); im=lay(im,m,cop,rad=4)
    d0=Image.new('L',(SS,SS),0); ImageDraw.Draw(d0).ellipse((cx-R+24,cy-R+24,cx+R-24,cy+R-24),fill=255); im=Image.composite(tint(noise(SS,30),(22,14,10),(46,30,22)),im,d0)
    m2=Image.new('L',(SS,SS),0); tablet(ImageDraw.Draw(m2),cx,cy,92,150); im=lay(im,m2,bone())
    k=Image.new('L',(SS,SS),0); e=ImageDraw.Draw(k); e.polygon([(cx,cy-46),(cx+26,cy),(cx,cy+46),(cx-26,cy)],outline=255,width=7); e.ellipse((cx-8,cy-8,cx+8,cy+8),fill=255); im=carve(im,k)
    for a in range(0,360,45):
        x=cx+(R-12)*math.cos(math.radians(a)); y=cy+(R-12)*math.sin(math.radians(a)); ImageDraw.Draw(im).ellipse((x-5,y-5,x+5,y+5),fill=(70,44,14))
    return ringframe(im)
def c2b():
    # 最终切图：仍是三枚，放大、拉开明暗，黑边加粗，只留最大一枚的刻纹
    im=base(); cx=SS/2
    L=((cx+74,SS/2+52,88,128,((62,46,28),(136,110,74)),False),(cx-72,SS/2+30,100,176,((76,58,36),(176,148,102)),False),(cx+0,SS/2-6,122,244,((100,80,52),(236,212,164)),True))
    for (x,y,w,h,c,cv) in L:
        m=Image.new('L',(SS,SS),0); tablet(ImageDraw.Draw(m),x,y,w,h)
        o=Image.new('L',(SS,SS),0); tablet(ImageDraw.Draw(o),x,y,w+24,h+24); im=Image.composite(Image.new('RGB',(SS,SS),(6,4,2)),im,o)
        im=lay(im,m,bone(*c),sh=False)
        if cv:
            k=Image.new('L',(SS,SS),0); e=ImageDraw.Draw(k); e.polygon([(x,y-h*.24),(x+w*.3,y),(x,y+h*.24),(x-w*.3,y)],outline=255,width=11); im=carve(im,k)
    return ringframe(im)

def icons(button):
    """button = 原版 RepairAll_Button 第 0 帧（58×58）；返回 [背包, 护身符]，SS×SS。 | button = frame 0 of the original RepairAll_Button (58×58); returns [bag, charm], SS×SS."""
    global fr
    fr = button.convert('RGBA').resize((SS, SS), Image.LANCZOS)
    global cop
    cop = ImageChops.add(fr.convert('RGB').crop((10,120,70,264)).resize((SS,SS),Image.LANCZOS),Image.new('RGB',(SS,SS),(40,26,8)))
    return [make('bag'), c2b()]

/* BC3 → ASTC 4x4 逐块转码。每块先按引擎软解（xbg_texture.c）的同一套整数算式解出 16 个 RGBA，再编成两种 ASTC 块之一：
 * - 块内透明度一样（不透明块、整块同一透明度）：单平面，CEM 12（LDR RGBA 直接），8 位端点原样保留 BC1 的两个 565 端点，
 *   2 位权重（0/21/43/64，对应 BC1 的 0、1/3、2/3、1）：与软解差 ≤ 2 级；
 * - 透明度有变化：双平面（CCS = 3，透明度单独一组权重），端点只剩 45 位 → 48 级（1 个三进制位 + 4 位，ISE），两平面各 2 位权重；
 *   透明度端点取块内最小 / 最大值，有损（透明度每块 4 级，颜色端点约 ±3 级）。
 * 每像素的权重按量化后的端点取误差最小的那个；端点按 CEM 12 的规则排好（第二端点 RGB 和不小于第一端点），避开蓝色收缩。
 * 位布局按 Khronos ASTC 规范：块模式 11 位（4x4 网格、权重 4 级；0x042 单平面 / 0x442 双平面）、分区数 2 位、CEM 4 位，端点从第 17 位起，
 * 权重从第 127 位往下倒序，双平面的 CCS 紧贴在权重下面。
 * Per-block BC3 -> ASTC 4x4 transcode. Each block is first decoded with the same integer arithmetic as the engine's software decode
 * (xbg_texture.c) to 16 RGBA texels, then encoded as one of two ASTC block kinds:
 * - uniform alpha (opaque blocks, one alpha for the whole block): single plane, CEM 12 (LDR RGBA direct), 8-bit endpoints keeping BC1's two
 *   565 endpoints exactly, 2-bit weights (0/21/43/64 for BC1's 0, 1/3, 2/3, 1): within 2 levels of the software decode;
 * - varying alpha: dual plane (CCS = 3, alpha has its own weights), only 45 bits left for endpoints -> 48 levels (one trit + 4 bits, ISE),
 *   2-bit weights per plane; alpha endpoints are the block's min / max, lossy (4 alpha levels per block, colour endpoints about +-3 levels).
 * Each texel takes the weight with the least error against the quantised endpoints; endpoints are ordered per CEM 12 (second endpoint's RGB
 * sum not below the first) so blue contraction never applies.
 * Bit layout per the Khronos ASTC specification: 11-bit block mode (4x4 grid, 4-level weights; 0x042 single / 0x442 dual plane), 2-bit
 * partition count, 4-bit CEM, endpoints from bit 17, weights reversed downward from bit 127, the dual-plane CCS just below the weights. */
#include "xbg_astc.h"
#include <string.h>

static void put(uint8_t *o, unsigned pos, unsigned n, uint32_t v) {
    for(unsigned i=0;i<n;i++) if((v>>i)&1u) o[(pos+i)>>3]|=(uint8_t)(1u<<((pos+i)&7));
}
static unsigned get(const uint8_t *o, unsigned pos, unsigned n) {
    unsigned v=0; for(unsigned i=0;i<n;i++) v|=((o[(pos+i)>>3]>>((pos+i)&7))&1u)<<i; return v;
}

/* 5 个三进制位 ↔ 8 位 T（规范的 trit 解码；编码取解码结果相同的最小 T，未用到的高位因此为 0）
 * 5 trits <-> 8-bit T (the specification's trit decode; encoding takes the smallest T decoding to the tuple, so unused high bits are 0) */
static void trits_from_T(unsigned T, unsigned t[5]) {
    unsigned C;
    if(((T>>2)&7)==7){C=(((T>>5)&7)<<2)|(T&3);t[4]=2;t[3]=2;}
    else{C=T&31;if(((T>>5)&3)==3){t[4]=2;t[3]=(T>>7)&1;}else{t[4]=(T>>7)&1;t[3]=(T>>5)&3;}}
    if((C&3)==3){t[2]=2;t[1]=(C>>4)&1;t[0]=(((C>>3)&1)<<1)|((C>>2)&1&~(C>>3)&1);}
    else if(((C>>2)&3)==3){t[2]=2;t[1]=2;t[0]=C&3;}
    else{t[2]=(C>>4)&1;t[1]=(C>>2)&3;t[0]=(((C>>1)&1)<<1)|(C&1&~(C>>1)&1);}
}
static unsigned T_from_trits(const unsigned t[5]) {
    for(unsigned T=0;T<256;T++){unsigned d[5];trits_from_T(T,d);if(!memcmp(d,t,sizeof d))return T;}
    return 0;   /* 每个组合都能编到（单测核过） | every tuple is reachable (checked by the tests) */
}
/* 规范的「颜色反量化」：48 级 = 1 个三进制位 + 4 位 dcba；B = dcb000dcb，C = 22 | the specification's colour unquantisation: 48 levels */
uint8_t xbg_astc_unquant48(unsigned trit, unsigned m) {
    unsigned a=m&1,b=(m>>1)&1,c=(m>>2)&1,d=(m>>3)&1;
    unsigned A=a?0x1FFu:0u, B=(d<<8)|(c<<7)|(b<<6)|(d<<2)|(c<<1)|b;
    unsigned T=trit*22u+B; T^=A; return (uint8_t)((A&0x80u)|(T>>2));
}
static struct { uint8_t trit,bits; } q48[256];
static int q48_ready;
static void q48_init(void) {   /* 每个 8 位值最近的 48 级码 | nearest 48-level code for every 8-bit value */
    for(unsigned v=0;v<256;v++){int best=1000;for(unsigned t=0;t<3;t++)for(unsigned m=0;m<16;m++){int e=(int)xbg_astc_unquant48(t,m)-(int)v;if(e<0)e=-e;
        if(e<best){best=e;q48[v].trit=(uint8_t)t;q48[v].bits=(uint8_t)m;}}}
    q48_ready=1;
}
static const unsigned weight4[4]={0,21,43,64};   /* 2 位权重的反量化 | unquantised 2-bit weights */
static unsigned interp(unsigned e0, unsigned e1, unsigned w, int srgb) {   /* LDR：端点展开到 16 位后插值，取高 8 位 | LDR interpolation at 16 bits, top 8 bits */
    unsigned c0=srgb?(e0<<8)|0x80u:e0*257u, c1=srgb?(e1<<8)|0x80u:e1*257u;
    return ((c0*(64-w)+c1*w+32)>>6)>>8;
}

/* 与 xbg_texture.c 的 BC3 软解同一套算式 | the same arithmetic as the BC3 software decode in xbg_texture.c */
static void bc3_decode(const uint8_t *b, uint8_t px[16][4], unsigned c565[2][3]) {
    const uint8_t *cb=b+8; unsigned c0=cb[0]|cb[1]<<8, c1=cb[2]|cb[3]<<8, v[4][3];
    for(unsigned k=0;k<2;k++){unsigned c=k?c1:c0,r=(c>>11)&31,g=(c>>5)&63,bl=c&31;v[k][0]=(r<<3)|(r>>2);v[k][1]=(g<<2)|(g>>4);v[k][2]=(bl<<3)|(bl>>2);}
    for(unsigned j=0;j<3;j++){v[2][j]=(2*v[0][j]+v[1][j])/3;v[3][j]=(v[0][j]+2*v[1][j])/3;c565[0][j]=v[0][j];c565[1][j]=v[1][j];}
    uint32_t bits=(uint32_t)cb[4]|(uint32_t)cb[5]<<8|(uint32_t)cb[6]<<16|(uint32_t)cb[7]<<24;
    uint32_t a16[8]={b[0]*257u,b[1]*257u};
    for(unsigned k=2;k<8;k++)a16[k]=b[0]>b[1]?((8-k)*a16[0]+(k-1)*a16[1]+3)/7:k<6?((6-k)*a16[0]+(k-1)*a16[1]+2)/5:k==6?0:65535;
    uint64_t ab=0;for(unsigned k=0;k<6;k++)ab|=(uint64_t)b[k+2]<<(k*8);
    for(unsigned i=0;i<16;i++){unsigned ci=(bits>>(i*2))&3;for(unsigned j=0;j<3;j++)px[i][j]=(uint8_t)v[ci][j];px[i][3]=(uint8_t)((a16[(ab>>(i*3))&7]+128)/257);}
}

int xbg_bc3_to_astc(const uint8_t bc3[16], uint8_t out[16]) {
    if(!q48_ready)q48_init();
    uint8_t px[16][4]; unsigned c[2][3]; bc3_decode(bc3,px,c);
    unsigned amin=255,amax=0; for(unsigned i=0;i<16;i++){if(px[i][3]<amin)amin=px[i][3];if(px[i][3]>amax)amax=px[i][3];}
    const int dual=amin!=amax;
    /* 端点 v[0..7] = r0 r1 g0 g1 b0 b1 a0 a1（反量化后的值）；双平面另记 48 级码 | endpoints (unquantised); 48-level codes for dual plane */
    unsigned v[8], tr[8]={0}, bi[8]={0};
    unsigned raw[8]={c[0][0],c[1][0],c[0][1],c[1][1],c[0][2],c[1][2],dual?amin:amin,dual?amax:amin};
    for(unsigned k=0;k<8;k++){if(dual){tr[k]=q48[raw[k]].trit;bi[k]=q48[raw[k]].bits;v[k]=xbg_astc_unquant48(tr[k],bi[k]);}else v[k]=raw[k];}
    if(v[1]+v[3]+v[5]<v[0]+v[2]+v[4])   /* 第二端点 RGB 和更小会触发蓝色收缩：两端点对调 | a smaller second-endpoint sum triggers blue contraction: swap */
        for(unsigned k=0;k<8;k+=2){unsigned x=v[k];v[k]=v[k+1];v[k+1]=x;x=tr[k];tr[k]=tr[k+1];tr[k+1]=x;x=bi[k];bi[k]=bi[k+1];bi[k+1]=x;}
    unsigned wc[16], wa[16];
    for(unsigned i=0;i<16;i++){
        unsigned best=~0u;wc[i]=0;
        for(unsigned w=0;w<4;w++){unsigned e=0;for(unsigned j=0;j<3;j++){int d=(int)interp(v[j*2],v[j*2+1],weight4[w],0)-(int)px[i][j];e+=(unsigned)(d*d);}
            if(e<best){best=e;wc[i]=w;}}
        best=~0u;wa[i]=0;
        if(dual)for(unsigned w=0;w<4;w++){int d=(int)interp(v[6],v[7],weight4[w],0)-(int)px[i][3];unsigned e=(unsigned)(d*d);if(e<best){best=e;wa[i]=w;}}
    }
    memset(out,0,16);
    put(out,0,11,dual?0x442u:0x042u); put(out,11,2,0); put(out,13,4,12);
    unsigned wbits;
    if(!dual){for(unsigned k=0;k<8;k++)put(out,17+8*k,8,v[k]);wbits=32;}
    else{   /* ISE：每 5 个值一组，组内 m0 T[1:0] m1 T[3:2] m2 T[4] m3 T[6:5] m4 T[7] | ISE: groups of five values */
        static const unsigned tb[5]={2,2,1,2,1}; unsigned pos=17;
        for(unsigned g=0;g<8;g+=5){unsigned t[5]={0},n=8-g<5?8-g:5;for(unsigned i=0;i<n;i++)t[i]=tr[g+i];unsigned T=T_from_trits(t),sh=0;
            for(unsigned i=0;i<n;i++){put(out,pos,4,bi[g+i]);pos+=4;put(out,pos,tb[i],(T>>sh)&((1u<<tb[i])-1));pos+=tb[i];sh+=tb[i];}}
        wbits=64; put(out,128-wbits-2,2,3);   /* CCS = 3：第二平面是透明度 | CCS = 3: plane 2 is alpha */
    }
    uint8_t ws[16]={0}; unsigned p=0;
    for(unsigned i=0;i<16;i++){put(ws,p,2,wc[i]);p+=2;if(dual){put(ws,p,2,wa[i]);p+=2;}}
    for(unsigned i=0;i<wbits;i++)if(get(ws,i,1))out[(127-i)>>3]|=(uint8_t)(1u<<((127-i)&7));
    return dual?2:1;
}

int xbg_bc3_to_astc_linear(uint32_t w, uint32_t h, uint32_t depth, const void *source, size_t source_bytes,
                           size_t row, size_t image, void *output, size_t output_bytes) {
    if(!source || !output || !w || !h || !depth) return -1;
    size_t blocks=((size_t)w+3)/4, rows=((size_t)h+3)/4, minimum=blocks*16;
    if(!row)row=minimum;
    if(row<minimum || row%16 || row>SIZE_MAX/rows) return -1;
    if(!image)image=row*rows;
    if(image<row*rows || image>SIZE_MAX/depth || blocks*rows>SIZE_MAX/16/depth) return -1;
    if(image*(depth-1)+row*(rows-1)+minimum>source_bytes || blocks*rows*depth*16>output_bytes) return -1;
    const uint8_t *s=source; uint8_t *d=output;
    for(size_t z=0;z<depth;z++)for(size_t y=0;y<rows;y++)for(size_t x=0;x<blocks;x++)
        xbg_bc3_to_astc(s+z*image+y*row+x*16,d+((z*rows+y)*blocks+x)*16);
    return 0;
}

int xbg_astc_decode_own(const uint8_t b[16], int srgb, uint8_t rgba[64]) {
    unsigned mode=get(b,0,11); int dual=mode==0x442u;
    if((mode!=0x042u && !dual) || get(b,11,2) || get(b,13,4)!=12) return -1;
    unsigned v[8], wbits=dual?64:32;
    if(!dual)for(unsigned k=0;k<8;k++)v[k]=get(b,17+8*k,8);
    else{static const unsigned tb[5]={2,2,1,2,1};unsigned pos=17;
        for(unsigned g=0;g<8;g+=5){unsigned n=8-g<5?8-g:5,m[5]={0},T=0,sh=0,t[5];
            for(unsigned i=0;i<n;i++){m[i]=get(b,pos,4);pos+=4;T|=get(b,pos,tb[i])<<sh;pos+=tb[i];sh+=tb[i];}
            trits_from_T(T,t);for(unsigned i=0;i<n;i++)v[g+i]=xbg_astc_unquant48(t[i],m[i]);}
        if(get(b,128-wbits-2,2)!=3)return -1;}
    unsigned e0[4]={v[0],v[2],v[4],v[6]}, e1[4]={v[1],v[3],v[5],v[7]};
    if(v[1]+v[3]+v[5]<v[0]+v[2]+v[4]){   /* 蓝色收缩（本编码器不会产出，解码照规范做） | blue contraction (never emitted here; decoded per the spec) */
        unsigned a[4]={(v[1]+v[5])>>1,(v[3]+v[5])>>1,v[5],v[7]},c[4]={(v[0]+v[4])>>1,(v[2]+v[4])>>1,v[4],v[6]};memcpy(e0,a,sizeof a);memcpy(e1,c,sizeof c);}
    uint8_t ws[16]={0}; for(unsigned i=0;i<wbits;i++)if((b[(127-i)>>3]>>((127-i)&7))&1)ws[i>>3]|=(uint8_t)(1u<<(i&7));
    for(unsigned i=0;i<16;i++){unsigned w0=weight4[get(ws,i*(dual?4:2),2)],w1=dual?weight4[get(ws,i*4+2,2)]:w0;
        for(unsigned j=0;j<4;j++)rgba[i*4+j]=(uint8_t)interp(e0[j],e1[j],j==3?w1:w0,srgb && j<3);}
    return 0;
}

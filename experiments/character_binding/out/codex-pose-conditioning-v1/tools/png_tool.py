import zlib, struct, sys

def read_png(path):
    d = open(path,'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, pal, trns = 8, b'', None, None
    w=h=bd=ct=0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]; body = d[pos+8:pos+8+ln]
        if typ==b'IHDR': w,h,bd,ct = struct.unpack('>IIBB', body[:10])
        elif typ==b'PLTE': pal=body
        elif typ==b'tRNS': trns=body
        elif typ==b'IDAT': idat+=body
        elif typ==b'IEND': break
        pos += 12+ln
    assert bd==8, (path,bd)
    ch = {0:1,2:3,3:1,4:2,6:4}[ct]
    raw = zlib.decompress(idat)
    stride = w*ch
    out = bytearray(); prev = bytearray(stride)
    p=0
    for y in range(h):
        f = raw[p]; p+=1
        line = bytearray(raw[p:p+stride]); p+=stride
        for x in range(stride):
            a = line[x-ch] if x>=ch else 0
            b = prev[x]
            c = prev[x-ch] if x>=ch else 0
            if f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+((a+b)>>1))&255
            elif f==4:
                pp=a+b-c; pa=abs(pp-a); pb=abs(pp-b); pc=abs(pp-c)
                pr = a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[x]=(line[x]+pr)&255
        out+=line; prev=line
    # expand to RGBA
    rgba = bytearray(w*h*4)
    for i in range(w*h):
        if ct==6: rgba[i*4:i*4+4]=out[i*4:i*4+4]
        elif ct==2: rgba[i*4:i*4+3]=out[i*3:i*3+3]; rgba[i*4+3]=255
        elif ct==0: v=out[i]; rgba[i*4:i*4+3]=bytes([v,v,v]); rgba[i*4+3]=255
        elif ct==4: v=out[i*2]; rgba[i*4:i*4+3]=bytes([v,v,v]); rgba[i*4+3]=out[i*2+1]
        elif ct==3:
            idx=out[i]; rgba[i*4:i*4+3]=pal[idx*3:idx*3+3]
            rgba[i*4+3]= trns[idx] if (trns and idx<len(trns)) else 255
    return w,h,rgba

def write_png(path,w,h,rgba):
    raw=bytearray()
    for y in range(h):
        raw.append(0); raw+=rgba[y*w*4:(y+1)*w*4]
    def chunk(t,b): 
        return struct.pack('>I',len(b))+t+b+struct.pack('>I', zlib.crc32(t+b)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB',w,h,8,6,0,0,0))
        + chunk(b'IDAT', zlib.compress(bytes(raw),9)) + chunk(b'IEND', b''))

def bbox(w,h,rgba,alpha_thresh=8,dark=False):
    x0,y0,x1,y1 = w,h,-1,-1
    for y in range(h):
        for x in range(w):
            i=(y*w+x)*4
            a=rgba[i+3]
            on = a>alpha_thresh if not dark else (a>alpha_thresh and (rgba[i]+rgba[i+1]+rgba[i+2])>24)
            if on:
                if x<x0:x0=x
                if x>x1:x1=x
                if y<y0:y0=y
                if y>y1:y1=y
    return x0,y0,x1,y1

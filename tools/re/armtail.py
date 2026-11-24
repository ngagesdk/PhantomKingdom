#!/usr/bin/env python3
"""Tiny ARM decoder for code Ghidra left undisassembled.
usage: PK_RE_BIN=6rah.bin armtail.py <addr> [...]"""
import os,struct,sys
c=open(os.environ.get('PK_RE_BIN','6rah.bin'),'rb').read()
def rot(imm):
    r=(imm>>8)*2; v=imm&0xff; return ((v>>r)|(v<<(32-r)))&0xffffffff
def dis(a):
    w=struct.unpack_from('<I',c,a-0x10000000)[0]; d='?'
    if (w&0x0e000000)==0x0a000000:
        off=(w&0xffffff); off=off-(1<<24) if off&0x800000 else off
        d=('bl ' if w&0x01000000 else 'b ')+hex(a+8+off*4)
    elif (w&0x0fe00000)==0x03a00000: d='mov r%d,#%#x'%((w>>12)&15,rot(w&0xfff))
    elif (w&0x0fe00000)==0x03e00000: d='mvn r%d,#%#x'%((w>>12)&15,rot(w&0xfff))
    elif (w&0x0c500000)==0x04000000: d='str r%d,[r%d,#%s%#x]'%((w>>12)&15,(w>>16)&15,'' if w&0x800000 else '-',w&0xfff)
    elif (w&0x0c500000)==0x04100000:
        d='ldr r%d,[r%d,#%#x]'%((w>>12)&15,(w>>16)&15,w&0xfff)
        if (w>>16)&15==15: d+=' =%#x'%struct.unpack_from('<I',c,a+8+(w&0xfff)-0x10000000)[0]
    elif (w&0x0de00000)==0x01a00000 and not (w&0x02000000): d='mov r%d,r%d'%((w>>12)&15,w&15)
    elif (w&0x0de00000)==0x03800000: d='orr r%d,r%d,#%#x'%((w>>12)&15,(w>>16)&15,rot(w&0xfff))
    elif (w&0x0de00000)==0x03c00000: d='bic r%d,r%d,#%#x'%((w>>12)&15,(w>>16)&15,rot(w&0xfff))
    elif (w&0x0de00000)==0x02800000: d='add r%d,r%d,#%#x'%((w>>12)&15,(w>>16)&15,rot(w&0xfff))
    elif (w&0x0de00000)==0x02400000: d='sub r%d,r%d,#%#x'%((w>>12)&15,(w>>16)&15,rot(w&0xfff))
    elif (w&0x0df00000)==0x03500000: d='cmp r%d,#%#x'%((w>>16)&15,rot(w&0xfff))
    cond=w>>28
    return w,('' if cond==14 else 'c%d '%cond)+d
for arg in sys.argv[1:]:
    a=int(arg,16); print('--',arg)
    for k in range(24):
        w,d=dis(a); print(hex(a),'%08x'%w,d)
        if d.startswith('b ') and not d.startswith('bl') and (w>>28)==14: break
        a+=4

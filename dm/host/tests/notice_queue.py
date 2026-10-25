# Game-file-backed regression. Executes the original game's queue code in Unicorn;
# libc/allocator hooks are explicit, not a reconstruction of the queue function.
import struct,sys,hashlib
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
b=Path(sys.argv[1]).read_bytes()
U=Uc(UC_ARCH_ARM64,UC_MODE_ARM);U.mem_map(0,0x4000000);U.mem_write(0,b);U.mem_map(0x7000000,0x80000)
R=0x7000200;note=0x7001000;ring=0x7002000;block=0x7003000;arg=0x7010000;msg=0x7020000;heap=0x7040000;stack=0x707fff0
q=lambda n:struct.pack('<Q',n)
U.mem_write(0x3211700,b'\x01');U.mem_write(0x32116f8,q(0x7004000));U.mem_write(0x7004000,q(0x7005000));U.mem_write(0x7005010,q(0x7000100));U.mem_write(0x7005028,q(0x7000110))
def hook(u,a,n,ctx):
 global heap
 if a==R:u.emu_stop();return
 if a==0x1b76070:
  p=u.reg_read(UC_ARM64_REG_X0);data=bytes(u.mem_read(p,4096));u.reg_write(UC_ARM64_REG_X0,data.index(0))
 elif a==0x1b76040:
  d=u.reg_read(UC_ARM64_REG_X0);s=u.reg_read(UC_ARM64_REG_X1);l=u.reg_read(UC_ARM64_REG_X2);u.mem_write(d,bytes(u.mem_read(s,l)))
 elif a==0x7000100:
  n=u.reg_read(UC_ARM64_REG_X1);u.reg_write(UC_ARM64_REG_X0,heap);heap+=(n+15)&~15
 elif a==0x7000110:pass
 else:return
 u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
U.hook_add(UC_HOOK_CODE,hook)
texts=['No items to deposit','存放：没有可以存放的物品','Нечего помещать в хранилище','x'*511]
for text in texts:
 data=text.encode();U.mem_write(note,bytes(0x200));U.mem_write(note+0x199,b'\x01');U.mem_write(note+0x1b0,q(ring)+q(8)+q(0)+q(0));U.mem_write(ring,q(block));U.mem_write(block,bytes(0x9c0));U.mem_write(arg,data+b'\0');U.mem_write(msg,q(0xd106c9eabf113da5)+q(0x8fa6af1ab5b75b23)+q(0)+q(1))
 U.reg_write(UC_ARM64_REG_SP,stack);U.reg_write(UC_ARM64_REG_LR,R);U.reg_write(UC_ARM64_REG_X0,note);U.reg_write(UC_ARM64_REG_X1,arg);U.reg_write(UC_ARM64_REG_X2,msg)
 U.emu_start(0x3d8d34,R+4,count=20000);assert U.reg_read(UC_ARM64_REG_PC)==R
 ptr,length=struct.unpack('<QQ',U.mem_read(block+0x20,16));assert length==len(data)
 U.mem_write(arg,bytes(len(data)+1));U.mem_write(stack-0x1000,bytes(0x1000));U.mem_write(msg,bytes(32))
 assert bytes(U.mem_read(ptr,length))==data
 assert struct.unpack('<QQQQ',U.mem_read(block,32))==(0xd106c9eabf113da5,0x8fa6af1ab5b75b23,0,1)
 assert struct.unpack('<Q',U.mem_read(note+0x1c8,8))[0]==1
 print('PASS native queued notification owns text and message after caller buffers are cleared:',len(data),'UTF-8 bytes')
print('Original image SHA256',hashlib.sha256(b).hexdigest())

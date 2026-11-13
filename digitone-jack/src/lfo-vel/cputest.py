import json, struct, sys
sys.path.insert(0, '/tmp/elekloader')
import elekloader.syx as syx
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import *
import os
F=os.environ.get('JACKF','/tmp/build29/Digitone_OS1.44-JACK11.syx')
img = syx.Syx.load(F).section(3); mp = json.load(open(F+'.map.json')); H=lambda k:int(mp[k],16); LOAD=0x40000400
u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
u.mem_map(0x40000000,0x400000); u.mem_write(LOAD,img)
u.mem_map(0x41900000,0x200000); u.mem_map(0x47be0000,0x40000); u.mem_map(0x50000000,0x10000)
u.mem_write(0x47be0000, img[H('__run_load')-LOAD:H('__run_load')-LOAD+H('__run_words')*4])
u.mem_write(H('__bss_start'), bytes(H('__bss_end')-H('__bss_start')))
MAGIC=0x50000100; u.mem_write(MAGIC,b'\x4e\x71'); sent=[]
def hook(uc,addr,size,_):
    if addr==0x400f3c4a:
        sp=uc.reg_read(UC_M68K_REG_A7); a=struct.unpack('>6I',uc.mem_read(sp,24)); sent.append(a[1:]); uc.reg_write(UC_M68K_REG_A7,sp+4); uc.reg_write(UC_M68K_REG_PC,a[0])
    elif addr==MAGIC: uc.emu_stop()
u.hook_add(UC_HOOK_CODE,hook)
def slots(track, s):   # s: [(dest, amount), (dest, amount)]
    for k,(d,a) in enumerate(s):
        u.mem_write(0x419eb1fc+80*track+24+40*k, struct.pack('>ii',d,a))
def note(vel, track, index=0):
    u.mem_write(0x50000400, bytes(35)+bytes([vel]))
    sp=0x5000f000; u.mem_write(sp, struct.pack('>I',MAGIC)+bytes(100))
    u.reg_write(UC_M68K_REG_A7,sp); u.reg_write(UC_M68K_REG_A2,0x50000400); u.reg_write(UC_M68K_REG_D2,track); u.reg_write(UC_M68K_REG_D3,index)
    u.emu_start(H('lfovel_note'),0,count=5000)
    assert u.reg_read(UC_M68K_REG_A7)==sp+4
    return u.mem_read(sp+4+71,1)[0]
bad=0
def chk(name,got,exp):
    global bad; ok=got==exp; bad+=not ok; print(('ok ' if ok else 'BAD'),name,'got',got,'expected',exp)
for t in range(4): slots(t,[(0,0),(0,0)])
chk('no LFO, vel 100', note(100,0), 100)
slots(0,[(22, 20<<8),(0,0)]);  chk('AT +20 slot1, vel 100', note(100,0), 120)
slots(0,[(22, 40<<8),(0,0)]);  chk('AT +40 clamps', note(100,0), 127)
slots(0,[(22, -30<<8),(0,0)]); chk('AT -30', note(100,0), 70)
slots(0,[(22, -200<<8),(0,0)]);chk('AT -200 clamps to 1', note(100,0), 1)
slots(0,[(0,0),(22, 10<<8)]);  chk('AT +10 in slot2', note(64,0), 74)
slots(0,[(23, 50<<8),(0,0)]);  chk('MW dest: velocity untouched', note(64,0), 64)
slots(2,[(22, -5<<8),(0,0)]);  chk('track 3 independent', note(90,2), 85)
chk('track 1 unaffected by track 3', note(90,1), 90)
slots(0,[(22, (7<<8)+128),(0,0)]); chk('fraction ignored', note(50,0), 57)
def mod(dest):
    sent.clear(); sp=0x5000e000
    u.mem_write(sp, struct.pack('>6I', MAGIC, 0, 13, dest, 0x4000, 0))   # ret, track, ch, dest, value, 0
    u.reg_write(UC_M68K_REG_A7, sp); u.emu_start(H('lfovel_mod'),0,count=5000)
    return sent[:], u.reg_read(UC_M68K_REG_A7)-sp
s,d=mod(22); chk('AT modulation not sent', len(s), 0); chk('stack after AT (+24 = ret+20 popped)', d, 24)
s,d=mod(25); chk('CC1 modulation still sent', len(s), 1); chk('args passed through', s[0][:4] if s else None, (0,13,25,0x4000)); chk('stack after CC1', d, 24)
HUM=H('lfovel_hum')
def hum(t,v): u.mem_write(HUM+t, bytes([v&255]))
for t in range(4): slots(t,[(0,0),(0,0)]); hum(t,0)
chk('HUM 0: unchanged', note(100,0), 100)
hum(0,40); vals=[note(100,0) for _ in range(400)]
chk('HUM 40: min >= 60', min(vals)>=60, True); chk('HUM 40: max <= 140 clamp 127', max(vals)<=127, True)
chk('HUM 40: not constant', len(set(vals))>30, True)
hum(0,10); vals=[note(64,0) for _ in range(300)]
chk('HUM 10 stays within 54..74', (min(vals)>=54, max(vals)<=74), (True,True))
first=note(64,0); chk('chord notes 2,3 reuse note 1 random', (note(64,0,1), note(64,0,2)), (first, first))
hum(0,40); lo=[note(5,0) for _ in range(200)]; chk('low VEL clamps at 1', min(lo)>=1, True)
hum(0,0); hum(2,20); chk('track 1 HUM 0 while track 3 HUM 20', note(90,0), 90)
slots(0,[(22, 20<<8),(0,0)]); hum(0,0); chk('LFO +20 with HUM 0', note(100,0), 120)
hum(0,5); v=[note(100,0) for _ in range(200)]; chk('LFO +20 and HUM 5 -> 115..125', (min(v)>=115, max(v)<=125), (True,True))

def stats(label, base, h, n=6000):
    hum(0,h); slots(0,[(0,0),(0,0)])
    v=[note(base,0) for _ in range(n)]
    r=[x-base for x in v]
    outer=sum(1 for x in r if abs(x)>h/2)/n
    top=sum(1 for x in v if x==127)/n
    print('HIST %-4s base %3d HUM %2d: outer half %.1f%%  exactly 127: %.1f%%  min %d max %d  within +/-%d: %.0f%%'%(label,base,h,100*outer,100*top,min(v),max(v),h//3,100*sum(1 for x in r if abs(x)<=h//3)/n))
    return v
lab='new' if 'JACK11' in F else 'old'
stats(lab,64,40); stats(lab,64,60); stats(lab,110,40); stats(lab,127,40)
hum(0,40); lo=[note(8,0) for _ in range(2000)]
chk('low VEL 8 / HUM 40: all in 1..127', (min(lo)>=1, max(lo)<=127), (True,True))
if lab=='new':
    v=stats('new',64,40,3000); chk('bell: HUM 40 at VEL 64 stays in 24..104', (min(v)>=24, max(v)<=104), (True,True))
    v=[note(110,0) for _ in range(3000)]; chk('reflect: VEL 110 / HUM 40 never above 127', max(v)<=127, True)
hum(0,60); r=[note(100,0) for _ in range(3000)]; chk('HUM 60 stays within 1..127 and reflects', (min(r)>=1, max(r)<=127), (True,True))
print('FAILURES', bad)

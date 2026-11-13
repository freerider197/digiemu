import json, struct, sys
sys.path.insert(0, '/tmp/elekloader')
import elekloader.syx as syx
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import *
img = syx.Syx.load('/tmp/build11/Digitone_OS1.44-JACK5c.syx').section(3)
mp = json.load(open('/tmp/build11/Digitone_OS1.44-JACK5c.syx.map.json'))
H = lambda k: int(mp[k], 16)
LOAD = 0x40000400
u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
try: u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
except Exception as e: print('cpu model', e)
u.mem_map(0x40000000, 0x00400000); u.mem_write(LOAD, img)
u.mem_map(0x41900000, 0x00200000)          # firmware RAM incl. 0x419b18ed
u.mem_map(0x47be0000, 0x00010000)          # mod RAM
u.mem_map(0x50000000, 0x00010000)          # test stack/buffers
run = img[H('__run_load')-LOAD : H('__run_load')-LOAD + H('__run_words')*4]
u.mem_write(0x47be0000, run)
u.mem_write(H('__bss_start'), bytes(H('__bss_end')-H('__bss_start')))
sent = []
MAGIC = 0x50000100
def hook(uc, addr, size, _):
    if addr == 0x400f3c1a:
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret, ln, buf = struct.unpack('>III', uc.mem_read(sp, 12))
        sent.append(bytes(uc.mem_read(buf, ln)))
        uc.reg_write(UC_M68K_REG_A7, sp + 4); uc.reg_write(UC_M68K_REG_PC, ret)
    elif addr == MAGIC:
        uc.emu_stop()
u.hook_add(UC_HOOK_CODE, hook)
CACHE = 0x50000800
def call_hook(msg, outlen=0):
    sent.clear()
    u.mem_write(0x419b18ed, bytes([outlen]))
    u.mem_write(0x50000400, bytes(msg))
    sp = 0x5000f000
    u.mem_write(sp, struct.pack('>III', MAGIC, 3, 0x50000400))   # ret, len, buf (as firmware pushes)
    u.mem_write(CACHE, b'\xee')
    u.reg_write(UC_M68K_REG_A7, sp); u.reg_write(UC_M68K_REG_A2, CACHE)
    u.reg_write(UC_M68K_REG_D5, 0); u.reg_write(UC_M68K_REG_D3, 0x55)
    u.mem_write(MAGIC, b'\x4e\x71')
    u.emu_start(H('ob6_nrpn_hook'), 0, count=20000)
    assert u.reg_read(UC_M68K_REG_A7) == sp + 4, 'stack not balanced'
    return [s for s in sent], u.mem_read(CACHE, 1)[0]
# expected model
d = json.load(open('/tmp/nrpn/map.json')); N = {int(k): v for k, v in d['nrpn'].items()}
def scale(v, lo, hi, cen):
    if cen:
        c = (lo + hi)//2
        return lo + (v*(c-lo)+32)//64 if v <= 64 else c + ((v-64)*(hi-c)+31)//63
    return lo + (v*(hi-lo)+63)//127
bad = 0; checks = 0; last = None; G = [None, None]
for cc in range(128):
    for v in (0, 1, 63, 64, 100, 126, 127):
        out, cache = call_hook([0xBD, cc, v]); checks += 1
        if cc in N and not (98 <= cc <= 101):
            n, m, l, lo, hi, note = N[cc]
            val = scale(v, lo, hi, note == 'centered' and hi - lo > 127)
            vm, vl = val >> 7, val & 127
            if G[0] is None: lf = False
            elif G[0] == vm: lf = True
            elif G[1] == vl: lf = False
            else: lf = (G[0]*128 + vl) < (vm*128 + G[1])
            exp = bytes([0xBD, 99, m, 0xBD, 98, l])
            exp += bytes([0xBD, 38, vl, 0xBD, 6, vm]) if lf else bytes([0xBD, 6, vm, 0xBD, 38, vl])
            G[0], G[1] = vm, vl
            ok = out == [exp] and cache == 0x55
        else:
            if 98 <= cc <= 101: last = None
            ok = out == [bytes([0xBD, cc, v])] and cache == 0x55
        if not ok:
            bad += 1
            if bad < 8: print('MISMATCH cc', cc, 'v', v, [o.hex(' ') for o in out], 'cache', hex(cache))
# other channel: untouched
for cc in (20, 102, 1):
    out, cache = call_hook([0xB0, cc, 77]); checks += 1
    if out != [bytes([0xB0, cc, 77])] or cache != 0x55: bad += 1; print('ch1 changed!', cc, out)
# full buffer: nothing sent, cache NOT updated
out, cache = call_hook([0xBD, 20, 90], outlen=241); checks += 1
if out != [] or cache != 0xee: bad += 1; print('full-buffer case wrong', out, hex(cache))
print('checks', checks, 'mismatches', bad)
for cc, v in ((20, 0), (20, 127), (102, 64), (102, 127), (76, 64), (3, 127), (25, 127)):
    print('ch14 CC', cc, '=', v, '->', [o.hex(' ') for o in call_hook([0xBD, cc, v])[0]])

# simulated OB-6: one pair of data-entry registers, value applied on every half
def sweep(seq):
    reg = {6: 0, 38: 0}; worst = 0; prev = None
    for v in seq:
        out, _ = call_hook([0xBD, 102, v])
        b = out[0]; msgs = [(b[i+1], b[i+2]) for i in range(0, len(b), 3)]
        target = None
        applied = []
        for cc, val in msgs:
            if cc in (6, 38):
                reg[cc] = val; applied.append(reg[6]*128 + reg[38])
        target = applied[-1]
        lo, hi = (min(prev, target), max(prev, target)) if prev is not None else (target, target)
        for a in applied[:-1]:
            if a > hi: worst = max(worst, a - hi)       # spike above the move
        prev = target
    return worst
up = sweep(list(range(0, 128)))
down = sweep(list(range(127, -1, -1)))
print('cutoff sweep: worst upward spike above the move  up:', up, ' down:', down)

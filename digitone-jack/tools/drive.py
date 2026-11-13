"""Headless Digitone driver: replay actions on the settled 1.44 snapshot,
print the OLED at checkpoints, log MIDI OUT bytes and who sent them.

Paths: DIGIEMU_DIR (digiemu checkout), WORK_DIR (png/wav/pkl output), MAIN_BIN (dn144_main.bin, only for the ts action).
See docs/BUILD_AND_TEST.md for the actions and environment variables."""
import sys, time, json, struct, os
DIGIEMU = os.environ.get('DIGIEMU_DIR', '/tmp/digiemu')      # the digiemu checkout
WORK = os.environ.get('WORK_DIR', '/tmp/work')              # where png / wav / pkl / trace output goes
sys.path.insert(0, DIGIEMU)
from emu import portable
_p = portable.open_paths(os.environ.get('FWDIR', DIGIEMU + '/portable/firmware/dn1-1.44-d4f200d0'))
portable.apply_env(_p)
os.environ['DIGIEMU_DEVICES'] = portable.devices_dir()
from emu import gui, midi
from unicorn import UC_HOOK_CODE, UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_PC

SNAP = os.environ.get('SNAP', DIGIEMU + '/portable/firmware/dn1-1.44-d4f200d0/snapshots/Digitone_OS1.44/gui.snap')
SYX = os.environ.get('SYX', '/tmp/Digitone_and_Digitone_Keys_OS1.44.syx')
WATCH = [int(x, 16) for x in os.environ.get('WATCH', '').split(',') if x]
DRV = [int(x, 16) for x in os.environ.get('DRV', '40002a3a').split(',') if x]

CODES = dict(FUNC=1, MIDI=2, KEYBOARD=3, PTN=4, BANK=5, SONG=6, GLOBAL=7, VOICE=8, TEMPO=9,
             RECORD=10, PLAY=11, STOP=12, YES=13, NO=14, UP=15, DOWN=16, LEFT=17, RIGHT=18,
             PAGE=19, TRIG=20, SYN1=21, SYN2=22, FLTR=23, AMP=24, LFO=25,
             T1=42, T2=43, T3=44, T4=45)
for i in range(16): CODES['TRIG%d' % (i + 1)] = 26 + i
for i, n in enumerate('ABCDEFGH'): CODES['E' + n] = 46 + i
CODES['ELEV'] = 54

log = []
calls = []
UC = {}
blocks = set()
TH = {}

class TapOut(midi.MidiOut):
    def install(self, m):
        super().install(m)
        uc = getattr(m, 'uc', m)
        UC['uc'] = uc
        def hook(u, addr, size, _):
            sp = u.reg_read(UC_M68K_REG_A7)
            ret = struct.unpack('>I', u.mem_read(sp, 4))[0]
            r2 = struct.unpack('>I', u.mem_read(sp + 4, 4))[0]
            buf = u.mem_read(r2, 3) if addr == 0x400f37ce else b''
            ln = struct.unpack('>I', u.mem_read(sp + 8, 4))[0] if addr == 0x400f37ce else 0
            if addr == 0x400f37ce:
                buf = bytes(u.mem_read(r2, min(ln, 12)))
            if addr != 0x400f37ce:
                args = struct.unpack('>8I', u.mem_read(sp + 4, 32))
                extra = ' '.join('%08x' % v for v in args[:4]) + ' F=%x FM=%x' % (args[4], args[5]) + (' S=' + bytes(u.mem_read(args[5], 24)).split(b'\0')[0].decode('latin1') if 0x40000000 <= args[5] < 0x42000000 else '')
                from unicorn.m68k_const import UC_M68K_REG_A2, UC_M68K_REG_A3, UC_M68K_REG_A4, UC_M68K_REG_A5
                extra += ' | a2=%08x a3=%08x a4=%08x a5=%08x' % tuple(u.reg_read(r) for r in (UC_M68K_REG_A2, UC_M68K_REG_A3, UC_M68K_REG_A4, UC_M68K_REG_A5))
                if addr in (0x40016fc0, 0x40017012):
                    ev = args[1]
                    f = struct.unpack('>6I', u.mem_read(ev, 24))
                    extra += ' EV ' + ' '.join('%x' % v for v in f)
                if os.environ.get('DUMPARG') and addr == int(os.environ['DUMPARG'].split(':')[0], 16):
                    i = int(os.environ['DUMPARG'].split(':')[1]); n = int(os.environ['DUMPARG'].split(':')[2])
                    p = args[i]
                    extra += ' ARG%d@%08x: ' % (i, p) + u.mem_read(p, n).hex(' ')
                for wa in WATCH:
                    extra += ' [%x]=%s' % (wa, u.mem_read(wa, 4).hex())
                buf = extra.encode()
                calls.append((len(log), addr, ret, buf.decode()))
                return
            calls.append((len(log), addr, ret, buf.hex(' ')))
        for a in DRV:
            uc.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
        if os.environ.get('RW'):
            from unicorn import UC_HOOK_MEM_READ
            rlo, rhi = [int(x, 16) for x in os.environ['RW'].split('-')]
            RD = {}
            TH['RD'] = RD
            def rhook(u, access, addr, size, value, _):
                if not TH.get('rwon'): return
                pc = u.reg_read(UC_M68K_REG_PC)
                k = (pc, (addr - rlo) // 60 * 60 + rlo)
                RD[k] = RD.get(k, 0) + 1
            uc.hook_add(UC_HOOK_MEM_READ, rhook, begin=rlo, end=rhi)
        if os.environ.get('WW'):
            lo, hi = [int(x, 16) for x in os.environ['WW'].split('-')]
            def whook(u, access, addr, size, value, _):
                pc = u.reg_read(UC_M68K_REG_PC)
                sp = u.reg_read(UC_M68K_REG_A7)
                ret = struct.unpack('>I', u.mem_read(sp, 4))[0]
                calls.append((len(log), addr, pc, 'WRITE size %d value %x (sp0 %08x)' % (size, value, ret)))
            uc.hook_add(UC_HOOK_MEM_WRITE, whook, begin=lo, end=hi)

def sink(data):
    log.append(bytes(data))

def ascii(fb):
    rows = []
    for y in range(0, 64):
        rows.append(''.join('#' if fb[y*128+x] else '.' for x in range(0,128)).rstrip('.'))
    return '\n'.join(rows)

def main(actions):
    emu = gui.Emulator(SNAP, syx=SYX, audio=(os.environ.get('AUDIO','0')=='1'), realtime=os.environ.get('RT','0')=='1', fast=os.environ.get('FAST','1')=='1')
    emu.midi_out = TapOut(sink)
    emu.start()
    emu.ready.wait(300)
    if emu.error:
        print('ERROR', emu.error); return
    def wait(s): time.sleep(s)
    wait(3)
    for act in actions:
        kind, *rest = act.split(':')
        if kind == 'tap':
            for name in rest[0].split('+'):
                emu.inbox.append(('press', CODES[name], None))
            wait(0.8)
            emu.inbox.append(('release_all', 0, None)); wait(1.2)
        elif kind == 'chord':   # chord:FUNC+EA  (FUNC first, then the other)
            parts = rest[0].split('+')
            for name in parts:
                emu.inbox.append(('press', CODES[name], None)); wait(0.12)
            wait(0.12)
            emu.inbox.append(('release_all', 0, None)); wait(0.5)
        elif kind == 'qtap':
            emu.inbox.append(('press', CODES[rest[0]], None)); wait(float(rest[1]) if len(rest)>1 else 0.05)
            emu.inbox.append(('release_all', 0, None)); wait(1.2)
        elif kind == 'hold':
            for name in rest[0].split('+'):
                emu.inbox.append(('press', CODES[name], None))
            wait(0.5)
        elif kind == 'rel':
            emu.inbox.append(('release_all', 0, None)); wait(0.6)
        elif kind == 'enc':      # enc:<1..9>:<delta>
            emu.inbox.append(('encoder', int(rest[0]), int(rest[1]))); wait(0.6)
        elif kind == 'wait':
            wait(float(rest[0]))
        elif kind == 'show':
            print('=== %s  (midi msgs so far: %d)' % (rest[0] if rest else '', len(log)))
            print(ascii(emu.fb))
        elif kind == 'lab':   # header + the two label rows only
            print('=== %s  (midi msgs so far: %d)' % (rest[0] if rest else '', len(log)))
            a = ascii(emu.fb).split('\n')
            print('\n'.join(a[0:10] + ['-'] + a[33:38] + ['-'] + a[59:64]))
        elif kind == 'ts':
            from emu import symbols
            if 'ts' not in TH:
                img = open(os.environ.get('MAIN_BIN', '/tmp/dn144_main.bin'),'rb').read()
                prof = symbols.resolve(img, load_addr=0x40000400)
                TH['ts'] = getattr(prof, 'transport_state', None)
                print('transport_state at', hex(TH['ts']) if TH['ts'] else None)
            if TH['ts']:
                print('TS', rest[0] if rest else '', struct.unpack('>I', UC['uc'].mem_read(TH['ts'], 4))[0])
        elif kind == 'leds':
            print('LEDS', rest[0] if rest else '', {k: v for k, v in sorted(emu.leds.items()) if any(v)})
        elif kind == 'poke':   # poke:ADDR:VALUE (32-bit)
            UC['uc'].mem_write(int(rest[0], 16), struct.pack('>I', int(rest[1], 16)))
        elif kind == 'pokeb':  # pokeb:ADDR:VALUE (one byte)
            UC['uc'].mem_write(int(rest[0], 16), bytes([int(rest[1], 16)]))
        elif kind == 'min':    # min:FA  (MIDI bytes into DIN in)
            emu.midi_in.put(bytes.fromhex(rest[0]))
        elif kind == 'apos':     # apos:NAME -> bytes of audio recorded so far
            with emu._audio_lock: print('APOS', rest[0], len(emu._audio_pcm))
        elif kind == 'wav':      # wav:NAME -> /tmp/work/NAME.wav from the emulator's audio recording (needs AUDIO=1)
            import wave
            with emu._audio_lock:
                pcm = bytes(emu._audio_pcm)
            rate = int((emu.audio_cfg or {}).get('rate', 48000))
            w = wave.open(WORK + '/%s.wav' % rest[0], 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm); w.close()
            print('WAV', rest[0], len(pcm), 'bytes', rate, 'Hz')
        elif kind == 'view2':
            u = UC['uc']
            def w(a): return struct.unpack('>I', u.mem_read(a, 4))[0]
            A = w(0x41a0a7f8); B = w(0x41a0a7fc)
            print('V2', rest[0], 'A=%08x(-16:%08x,0:%08x)' % (A, w(A-16), w(A)), 'B=%08x(-16:%08x,0:%08x)' % (B, w(B-16), w(B)))
        elif kind == 'view':   # view:LABEL -> brain+0x58/0x5c objects and their vtables
            u = UC['uc']
            out = []
            for a in (0x41a0a7f8, 0x41a0a7fc):
                o = struct.unpack('>I', u.mem_read(a, 4))[0]
                vt = struct.unpack('>I', u.mem_read(o, 4))[0] if 0x40000000 <= o < 0x48000000 else 0
                out.append('%08x(vt %08x)' % (o, vt))
            print('VIEW', rest[0], ' '.join(out))
        elif kind == 'mem':
            a = int(rest[0], 16)
            print('MEM', rest[0], rest[1] if len(rest) > 1 else '', UC['uc'].mem_read(a, int(rest[2]) if len(rest) > 2 else 4).hex())
        elif kind == 'dump':   # dump:NAME:LO-HI
            import pickle
            lo, hi = [int(x, 16) for x in rest[1].split('-')]
            out = {}
            for b, e, _p in UC['uc'].mem_regions():
                b2, e2 = max(b, lo), min(e + 1, hi)
                if b2 < e2:
                    out[b2] = bytes(UC['uc'].mem_read(b2, e2 - b2))
            pickle.dump(out, open(WORK + '/%s.pkl' % rest[0], 'wb'))
        elif kind == 'rwon':
            TH['rwon'] = True
        elif kind == 'rwoff':
            TH['rwon'] = False
            for (pc, rec), n in sorted(TH['RD'].items()):
                print('RD pc %08x rec %08x n %d' % (pc, rec, n))
        elif kind == 'png':     # png:NAME  -> /tmp/work/shots/NAME.png (4x)
            from PIL import Image
            os.makedirs(WORK + '/shots', exist_ok=True)
            im = Image.new('L', (128, 64))
            im.putdata([255 if b else 0 for b in emu.fb[:128*64]])
            im.resize((512, 256), Image.NEAREST).save(WORK + '/shots/%s.png' % rest[0])
        elif kind == 'px':
            import hashlib
            print('PX', rest[0] if rest else '', sum(1 for b in emu.fb if b), hashlib.md5(bytes(emu.fb)).hexdigest()[:8])
        elif kind == 'tron':
            def bh(u, addr, size, _):
                if 0x40000400 <= addr < 0x40400000: blocks.add(addr)
            TH['h'] = UC['uc'].hook_add(UC_HOOK_BLOCK, bh)
            try:
                UC['uc'].ctl_flush_tb()
            except Exception as e:
                print('flush failed', e)
        elif kind == 'troff':
            UC['uc'].hook_del(TH.pop('h'))
            open(os.environ.get('TRACE_OUT', WORK + '/blocks.txt'), 'w').write('\n'.join('%08x' % a for a in sorted(blocks)))
            print('traced blocks:', len(blocks))
        elif kind == 'mark':
            log.append(b'MARK:' + rest[0].encode())
    emu.stop_flag.set(); time.sleep(1)
    print('MIDI LOG:')
    for i, b in enumerate(log):
        if b.startswith(b'MARK'): print(i, b.decode()); continue
        h = bytes(x for x in b if x not in (0xF8, 0xFE)).hex(' ')
        if h: print(i, h)
    print('total msgs', len(log))
    print('DRIVER CALLS (log index, addr, return):')
    for c in calls[:int(os.environ.get('MAXC','60'))]:
        print(c[0], hex(c[1]), hex(c[2]), c[3] if len(c) > 3 else '')

if __name__ == '__main__':
    main(sys.argv[1:])

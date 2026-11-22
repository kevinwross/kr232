// Lint the kr232 documents: instruction encodings, first-byte map, memory map,
// device page table, and markdown table shapes.
//
//   node tools/check-isa.mjs [kr232-isa.md] [kr232-devices.md]
//
// Checks, all derived from the tables in the documents themselves:
//   1. every Appendix A vector decodes back to the assembly recorded for it,
//   2. every Appendix A vector's length matches the first-byte length map,
//   3. Appendix B covers 0x00-0xFF contiguously and agrees with the length map,
//   4. Appendix C tiles 0x00000000-0xFFFFFFFF with no gap or overlap,
//   5. the device page tables (ISA §14.1 and kr232-devices.md §1) agree and
//      tile 0xFFF00000-0xFFFEFFFF with 240 pages of 4 KiB,
//   6. every markdown table in both documents has a consistent shape.
//
// This is a spec lint, not a test suite: it catches arithmetic slips in the
// tables, not interpretation errors in the prose.

import { readFileSync, existsSync } from 'node:fs';

const isaPath = process.argv[2] ?? 'kr232-isa.md';
const devPath = process.argv[3] ?? 'kr232-devices.md';
const lines = readFileSync(isaPath, 'utf8').split(/\r?\n/);
const devLines = existsSync(devPath) ? readFileSync(devPath, 'utf8').split(/\r?\n/) : [];

const fail = [];
const note = (m) => fail.push(m);

// ---------------------------------------------------------------- assembly

const parseNum = (s) => {
  s = s.trim();
  let sign = 1;
  if (s[0] === '+') s = s.slice(1);
  else if (s[0] === '-') { sign = -1; s = s.slice(1); }
  if (!/^(0x[0-9a-f]+|\d+)$/i.test(s)) throw new Error(`bad number ${s}`);
  return sign * (/^0x/i.test(s) ? parseInt(s, 16) : parseInt(s, 10));
};

const parseOperand = (s) => {
  s = s.trim();
  let m;
  if (s.startsWith('#')) return { t: 'imm', v: parseNum(s.slice(1)) };
  if ((m = /^-\[r(\d+)\]$/i.exec(s))) return { t: 'mem', form: 'pre', base: 'r', n: +m[1] };
  if ((m = /^\[r(\d+)\]\+$/i.exec(s))) return { t: 'mem', form: 'post', base: 'r', n: +m[1] };
  if ((m = /^\[r(\d+)\]$/i.exec(s))) return { t: 'mem', form: 'ind', base: 'r', n: +m[1] };
  if ((m = /^\[r(\d+)([+-](?:0x[0-9a-f]+|\d+))\]$/i.exec(s)))
    return { t: 'mem', form: 'disp', base: 'r', n: +m[1], disp: parseNum(m[2]) };
  if ((m = /^\[(pc|sp)([+-](?:0x[0-9a-f]+|\d+))\]$/i.exec(s)))
    return { t: 'mem', form: 'disp', base: m[1].toLowerCase(), disp: parseNum(m[2]) };
  if ((m = /^\[(0x[0-9a-f]+|\d+)\]$/i.exec(s)))
    return { t: 'mem', form: 'abs', addr: parseNum(m[1]) };
  if ((m = /^f([0-3])$/i.exec(s))) return { t: 'freg', n: +m[1] };
  if ((m = /^r(\d+)$/i.exec(s))) return { t: 'reg', n: +m[1] };
  if ((m = /^fp$/i.exec(s))) return { t: 'reg', n: 13 };
  if ((m = /^sp$/i.exec(s))) return { t: 'reg', n: 15 };
  if ((m = /^pc$/i.exec(s))) return { t: 'reg', n: 14 };
  if ((m = /^([+-](?:0x[0-9a-f]+|\d+))$/i.exec(s))) return { t: 'rel', v: parseNum(m[1]) };
  if ((m = /^(0x[0-9a-f]+|\d+)$/i.exec(s))) return { t: 'abs', v: parseNum(m[1]) };
  throw new Error(`unparsed operand "${s}"`);
};

const parseAsm = (text) => {
  const m = /^(\S+)\s*(.*)$/.exec(text.trim());
  const mn = m[1].toLowerCase();
  const rest = m[2].trim();
  const ops = rest === '' ? [] : rest.split(',').map(parseOperand);
  return { mn, ops };
};

// ---------------------------------------------------------------- decoding

const R = (n) => ({ t: 'reg', n });
const FR = (n) => ({ t: 'freg', n });
const IMM = (v) => ({ t: 'imm', v });
const AM = (a) => ({ t: 'mem', form: 'abs', addr: a });
const mem = (form, base, n, disp) =>
  form === 'disp' ? { t: 'mem', form, base, n, disp } : { t: 'mem', form, base, n };

const ut = (bs, o, n) => {
  let v = 0;
  for (let i = 0; i < n; i++) v |= bs[o + i] << (8 * i);
  return v >>> 0;
};
const s8 = (bs, o) => (bs[o] << 24) >> 24;
const s16 = (bs, o) => ((bs[o] | (bs[o + 1] << 8)) << 16) >> 16;
const s32 = (bs, o) => ut(bs, o, 4) | 0;

const SYS = {
  0x70: 'ret', 0x71: 'nop', 0x72: 'brk', 0x73: 'sys',
  0x74: 'ei', 0x75: 'di', 0x76: 'halt', 0x77: 'iret', 0x78: 'rdcycle',
  0x79: 'syscall',
};

const COND = ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le'];

// Compact op table: [mnemonic, operand shape]
const COMPACT = [
  ['mov', 'rr'], ['add', 'rr'], ['addc', 'rr'], ['sub', 'rr'], ['subb', 'rr'],
  ['and', 'rr'], ['or', 'rr'], ['xor', 'rr'], ['cmp', 'rr'], ['mul', 'rr'],
  ['mulh', 'rr'], ['shl', 'rr'], ['shr', 'rr'], ['sar', 'rr'], ['ld', 'ld'],
  ['st', 'st'], ['ldbu', 'ld'], ['ldbs', 'ld'], ['stb', 'st'], ['ld', 'post'],
  ['st', 'post'], ['ldbu', 'post'], ['ldbs', 'post'], ['stb', 'post'], ['ld', 'pre'],
  ['st', 'pre'], ['ldbu', 'pre'], ['ldbs', 'pre'], ['stb', 'pre'], ['neg', 'un'],
  ['not', 'un'], ['jmp', 'jmp'],
];

const F0 = {
  0x00: 'mov', 0x01: 'add', 0x02: 'addc', 0x03: 'sub', 0x04: 'subb', 0x05: 'and',
  0x06: 'or', 0x07: 'xor', 0x08: 'cmp', 0x09: 'mul', 0x0a: 'mulh', 0x0b: 'shl',
  0x0c: 'shr', 0x0d: 'sar', 0x0e: 'ld', 0x0f: 'st', 0x10: 'ldbu', 0x11: 'ldbs',
  0x12: 'stb', 0x13: 'lea', 0x14: 'neg', 0x15: 'not', 0x16: 'sxtb', 0x17: 'div',
  0x18: 'divs', 0x19: 'jmp', 0x1a: 'call', 0x1b: 'sxth', 0x1c: 'ldhu',
  0x1d: 'ldhs', 0x1e: 'sth',
};
const F1 = {
  0x0: 'ldbu', 0x1: 'ldbs', 0x2: 'stb', 0x3: 'lea', 0x4: 'lea',
  0x5: 'ldhu', 0x6: 'sth', 0x7: 'ldhs',
};
const F3 = ['fadd', 'fsub', 'fmul', 'fdiv', 'fneg', 'fabs', 'fsqrt', 'fmov', 'fcmp', 'frnd'];
const F3_UNARY = new Set([4, 5, 6, 9]);
const F4 = { 0: 'fld', 1: 'fst', 2: 'flds', 3: 'fsts' };
const F5 = ['i2f', 'u2f', 'f2i', 'f2u'];
const F6 = ['fpush', 'fpop'];
const F9 = {
  0x0: 'jmp', 0x1: 'call', 0x2: 'jmp', 0x3: 'call', 0x4: 'lea', 0x5: 'lea',
  0x6: 'ld', 0x7: 'ldbu', 0x8: 'ldbs', 0x9: 'ldhu', 0xa: 'add', 0xb: 'sub',
  0xc: 'cmp', 0xd: 'and', 0xe: 'or', 0xf: 'xor',
};
const FA = { 0: 'fld', 1: 'fst', 2: 'flds', 3: 'fsts' };

function decode(bs) {
  const b0 = bs[0];
  // Short class
  if (b0 <= 0x4f) {
    const rd = b0 & 0xf;
    if (b0 <= 0x0f) return { mn: 'push', ops: [R(rd)] };
    if (b0 <= 0x1f) return { mn: 'pop', ops: [R(rd)] };
    if (b0 <= 0x2f) return { mn: 'inc', ops: [R(rd)] };
    if (b0 <= 0x3f) return { mn: 'dec', ops: [R(rd)] };
    return { mn: 'call', ops: [R(rd)] };
  }
  if (b0 >= 0x70 && b0 <= 0x7f) {
    const mn = SYS[b0];
    return mn ? { mn, ops: [] } : null;
  }
  // Register class
  if (b0 >= 0x50 && b0 <= 0x5f) return { mn: 'ld', ops: [R(b0 & 0xf), IMM(s32(bs, 1))] };
  if (b0 >= 0x60 && b0 <= 0x6f) return { mn: 'ld', ops: [R(b0 & 0xf), AM(ut(bs, 1, 4))] };
  if (b0 >= 0xe0 && b0 <= 0xef) return { mn: 'st', ops: [AM(ut(bs, 1, 4)), R(b0 & 0xf)] };
  // Compact register form
  if (b0 >= 0x80 && b0 <= 0x9f) {
    const [mn, shape] = COMPACT[b0 & 0x1f];
    const a = bs[1] >> 4, b = bs[1] & 0xf;
    switch (shape) {
      case 'rr': return { mn, ops: [R(a), R(b)] };
      case 'ld': return { mn, ops: [R(a), mem('ind', 'r', b)] };
      case 'st': return { mn, ops: [mem('ind', 'r', b), R(a)] };
      case 'post': return mn.startsWith('st')
        ? { mn, ops: [mem('post', 'r', b), R(a)] }
        : { mn, ops: [R(a), mem('post', 'r', b)] };
      case 'pre': return mn.startsWith('st')
        ? { mn, ops: [mem('pre', 'r', b), R(a)] }
        : { mn, ops: [R(a), mem('pre', 'r', b)] };
      case 'un': return b === 0 ? { mn, ops: [R(a)] } : null;
      case 'jmp': return b === 0 ? { mn, ops: [R(a)] } : null;
    }
    return null;
  }
  // Compact branch form
  if (b0 >= 0xa0 && b0 <= 0xbf) {
    const cond = (b0 >> 1) & 0xf;
    let d = ((b0 & 1) << 8) | bs[1];
    if (d >= 256) d -= 512;
    if (cond <= 13) return { mn: 'b' + COND[cond], ops: [{ t: 'rel', v: d }] };
    if (cond === 14) return { mn: 'call', ops: [{ t: 'rel', v: d }] };
    return { mn: 'bra', ops: [{ t: 'rel', v: d }] };
  }
  // Normal class
  if (b0 >= 0xc0 && b0 <= 0xdf) return decodeNormal(bs);
  // Escape class
  switch (b0) {
    case 0xf0: return decodeF0(bs);
    case 0xf1: return decodeF1(bs);
    case 0xf3: return decodeF3(bs);
    case 0xf4: return decodeF4(bs);
    case 0xf5: return decodeF5(bs);
    case 0xf6: return decodeF6(bs);
    case 0xf7: case 0xf8: case 0xfb: return decodeEnter(bs);
    case 0xf9: return decodeF9(bs);
    case 0xfa: return decodeFA(bs);
    default: return null;
  }
}

function decodeNormal(bs) {
  const op = bs[0] & 0x1f;
  const p = bs[1], q = bs[2];
  const rd = p >> 4, rb = p & 0xf;
  const dispMem = (d) => ({ t: 'mem', form: 'disp', base: 'r', n: rb, disp: d });
  switch (op) {
    case 0: return { mn: 'ld', ops: [R(rd), dispMem(s8(bs, 2))] };
    case 1: return { mn: 'st', ops: [dispMem(s8(bs, 2)), R(rd)] };
    case 2: return { mn: 'ldbu', ops: [R(rd), dispMem(s8(bs, 2))] };
    case 3: return { mn: 'ldbs', ops: [R(rd), dispMem(s8(bs, 2))] };
    case 4: return { mn: 'stb', ops: [dispMem(s8(bs, 2)), R(rd)] };
    case 5: return { mn: 'ld', ops: [R(rd), IMM(s8(bs, 2))] };
    case 6: return { mn: 'ld', ops: [R(rd), IMM(bs[2])] };
    case 7: return { mn: 'add', ops: [R(rd), IMM(s8(bs, 2))] };
    case 8: return { mn: 'sub', ops: [R(rd), IMM(s8(bs, 2))] };
    case 9: return { mn: 'cmp', ops: [R(rd), IMM(s8(bs, 2))] };
    case 10: return { mn: 'cmp', ops: [R(rd), IMM(bs[2])] };
    case 11: return { mn: 'and', ops: [R(rd), IMM(bs[2])] };
    case 12: return { mn: 'or', ops: [R(rd), IMM(bs[2])] };
    case 13: return { mn: 'xor', ops: [R(rd), IMM(bs[2])] };
    case 14: return { mn: 'lea', ops: [R(rd), dispMem(s8(bs, 2))] };
    case 15: return { mn: 'jmp', ops: [{ t: 'rel', v: s16(bs, 1) }] };
    case 16: return { mn: 'call', ops: [{ t: 'rel', v: s16(bs, 1) }] };
    case 17: return { mn: 'ldbu', ops: [R(rd), { t: 'mem', form: 'disp', base: 'pc', disp: s8(bs, 2) }] };
    case 18: return { mn: 'ldbs', ops: [R(rd), { t: 'mem', form: 'disp', base: 'pc', disp: s8(bs, 2) }] };
    case 19: {
      const c = p & 0xf;
      if (c > 13) return null;
      return { mn: 'set' + COND[c], ops: [R(rd)] };
    }
    case 20: return { mn: 'shl', ops: [R(rd), IMM(bs[2])] };
    case 21: return { mn: 'shr', ops: [R(rd), IMM(bs[2])] };
    case 22: return { mn: 'sar', ops: [R(rd), IMM(bs[2])] };
    case 23: return { mn: 'push', ops: [IMM(p | (q << 8))] };
    case 24: return { mn: 'pop', ops: [IMM(p | (q << 8))] };
    case 25: return { mn: 'div', ops: [R(rd), R(rb)] };
    case 26: return { mn: 'divs', ops: [R(rd), R(rb)] };
    case 27: return { mn: 'sxtb', ops: [R(rd)] };
    case 28: return { mn: 'leave', ops: [IMM(p | (q << 8))] };
    case 29: return { mn: 'ldhu', ops: [R(rd), dispMem(s8(bs, 2))] };
    case 30: return { mn: 'sth', ops: [dispMem(s8(bs, 2)), R(rd)] };
    case 31: return { mn: 'ldhs', ops: [R(rd), dispMem(s8(bs, 2))] };
    default: return null;
  }
}

const F0_STORE = new Set(['st', 'stb', 'sth']);
const F0_UNARY = new Set(['neg', 'not', 'sxtb', 'sxth']);
const extLen = (m) => (m === 1 || m === 6 ? 2 : m === 5 ? 1 : m === 7 ? 4 : 0);

function decodeF0(bs) {
  const op = bs[1];
  const mn = F0[op];
  if (!mn) return null;
  const spec = [bs[2], bs[3]].map((b) => ({ mode: (b >> 4) & 7, reg: b & 0xf }));
  const e1 = extLen(spec[0].mode), e2 = extLen(spec[1].mode);
  if (e1 && e2) return null; // illegal: both operands extended
  const len = 4 + e1 + e2;
  if (bs.length !== len) return { mn, ops: null, len };
  let pos = 4;
  const operand = (i) => {
    const { mode, reg } = spec[i];
    const ext = i === 0 ? e1 : e2;
    const d8 = bs[pos], d16 = s16(bs, pos), d32 = s32(bs, pos), w = ut(bs, pos, 2);
    pos += ext;
    switch (mode) {
      case 0: return R(reg);
      case 1: return IMM(d16);
      case 2: return mem('ind', 'r', reg);
      case 3: return mem('post', 'r', reg);
      case 4: return mem('pre', 'r', reg);
      case 5: return mem('disp', 'r', reg, s8(bs, pos - 1));
      case 6: return mem('disp', 'r', reg, d16);
      case 7: return mem('disp', 'r', reg, d32);
      default: return null;
    }
  };
  const o1 = operand(0), o2 = operand(1);
  if (!o1 || !o2) return { mn, ops: null, len };
  let ops;
  if (F0_STORE.has(mn)) ops = [o2, o1];
  else if (F0_UNARY.has(mn)) ops = [o1];
  else ops = [o1, o2];
  // d8 extension is one byte before pos advanced; recompute defensively
  return { mn, ops, len };
}

function decodeF1(bs) {
  const op = bs[1] >> 4;
  const rd = bs[1] & 0xf;
  const mn = F1[op];
  if (!mn) return null;
  const a = ut(bs, 2, 4);
  switch (op) {
    case 0: case 1: case 5: case 7: return { mn, ops: [R(rd), AM(a)] };
    case 2: case 6: return { mn, ops: [AM(a), R(rd)] };
    case 3: return { mn, ops: [R(rd), { t: 'mem', form: 'disp', base: 'pc', disp: a | 0 }] };
    case 4: return { mn, ops: [R(rd), { t: 'mem', form: 'disp', base: 'sp', disp: a | 0 }] };
    default: return null;
  }
}

function decodeF3(bs) {
  const op = bs[1] >> 4;
  const fd = (bs[1] >> 2) & 3, fs = bs[1] & 3;
  if (!(op in F3)) return null;
  const mn = F3[op];
  if (F3_UNARY.has(op)) return fs === 0 ? { mn, ops: [FR(fd)] } : null;
  return { mn, ops: [FR(fd), FR(fs)] };
}

const f4mem = (mm, base, d8) => {
  switch (mm) {
    case 0: return mem('ind', 'r', base);
    case 1: return mem('disp', 'r', base, d8);
    case 2: return mem('post', 'r', base);
    case 3: return mem('pre', 'r', base);
    default: return null;
  }
};

function decodeF4(bs) {
  const op = bs[1] >> 4;
  const fd = (bs[1] >> 2) & 3, mm = bs[1] & 3;
  const base = bs[2] >> 4;
  const mn = F4[op];
  if (!mn) return null;
  const len = mm === 1 ? 4 : 3;
  if (bs.length !== len) return { mn, ops: null, len };
  const m = f4mem(mm, base, bs[3]);
  if (!m) return null;
  return { mn, ops: [FR(fd), m] };
}

function decodeF5(bs) {
  const op = bs[1] >> 4;
  const fd = (bs[1] >> 2) & 3;
  const rb = bs[2] >> 4;
  const mn = F5[op];
  if (!mn) return null;
  if (op <= 1) return { mn, ops: [FR(fd), R(rb)] };
  return { mn, ops: [R(rb), FR(fd)] };
}

function decodeF6(bs) {
  const op = bs[1] >> 2;
  const fd = bs[1] & 3;
  const mn = F6[op];
  if (!mn) return null;
  return { mn, ops: [FR(fd)] };
}

function decodeEnter(bs) {
  const fb = bs[0];
  const widths = { 0xf7: 1, 0xf8: 2, 0xfb: 4 };
  const w = widths[fb];
  const frame = ut(bs, 1, w);
  const maskOff = 1 + w;
  const mask = ut(bs, maskOff, 2);
  const len = maskOff + 2;
  if (bs.length !== len) return { mn: 'enter', ops: null, len };
  return { mn: 'enter', ops: [IMM(frame), IMM(mask)] };
}

function decodeF9(bs) {
  const op = bs[1] >> 4;
  const r = bs[1] & 0xf;
  const mn = F9[op];
  if (!mn) return null;
  const payload = ut(bs, 2, 4);
  switch (op) {
    case 0: case 1: return { mn, ops: [{ t: 'abs', v: payload }] };
    case 2: case 3: return { mn, ops: [AM(payload)] };
    case 4: return { mn, ops: [R(r), { t: 'mem', form: 'disp', base: 'pc', disp: payload | 0 }] };
    case 5: return { mn, ops: [R(r), { t: 'mem', form: 'disp', base: 'sp', disp: payload | 0 }] };
    case 6: case 7: case 8: case 9:
      return { mn, ops: [R(r), { t: 'mem', form: 'disp', base: 'pc', disp: payload | 0 }] };
    default: return { mn, ops: [R(r), IMM(payload | 0)] };
  }
}

function decodeFA(bs) {
  const op = bs[1] >> 4;
  const fd = (bs[1] >> 2) & 3, mm = bs[1] & 3;
  const base = bs[2] >> 4;
  const mn = FA[op];
  if (!mn) return null;
  const d = s32(bs, 3);
  const m = mm === 0
    ? { t: 'mem', form: 'disp', base: 'r', n: base, disp: d }
    : mm === 1
      ? { t: 'mem', form: 'disp', base: 'pc', disp: d }
      : null;
  if (!m) return null;
  return { mn, ops: [FR(fd), m] };
}

// ---------------------------------------------------------------- checks

const norm = (o) => {
  if (o === null || typeof o !== 'object') return JSON.stringify(o);
  const keys = Object.keys(o).sort();
  return '{' + keys.map((k) => JSON.stringify(k) + ':' + norm(o[k])).join(',') + '}';
};
const same = (a, b) => norm(a) === norm(b);

const classLen = (b0) => {
  if (b0 <= 0x4f) return 1;
  if (b0 <= 0x5f) return 5;
  if (b0 <= 0x6f) return 5;
  if (b0 <= 0x7f) return 1;
  if (b0 <= 0x9f) return 2;
  if (b0 <= 0xbf) return 2;
  if (b0 <= 0xdf) return 3;
  if (b0 <= 0xef) return 5;
  switch (b0) {
    case 0xf1: return 6;
    case 0xf3: return 2;
    case 0xf5: return 3;
    case 0xf6: return 2;
    case 0xf7: return 4;
    case 0xf8: return 5;
    case 0xf9: return 6;
    case 0xfa: return 7;
    case 0xfb: return 7;
    default: return null; // 0xF0, 0xF4 and reserved prefixes are computed
  }
};

// Appendix A
const aStart = lines.findIndex((l) => l.startsWith('## Appendix A'));
const bStart = lines.findIndex((l) => l.startsWith('## Appendix B'));
const rows = [];
for (let i = aStart; i < bStart; i++) {
  const m = /^\|\s*`([^`]+)`[^|]*\|\s*`([0-9A-F]{2}(?: [0-9A-F]{2})*)`\s*\|/.exec(lines[i]);
  if (m) rows.push({ asm: m[1], bytes: m[2].split(' ').map((h) => parseInt(h, 16)), line: i + 1 });
}

let checked = 0;
for (const row of rows) {
  const want = parseAsm(row.asm);
  let got;
  try {
    got = decode(row.bytes);
  } catch (e) {
    note(`line ${row.line}: decode threw for \`${row.asm}\`: ${e.message}`);
    continue;
  }
  if (!got) {
    note(`line ${row.line}: \`${row.asm}\` -> ${row.bytes.map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(' ')} decodes to nothing`);
    continue;
  }
  const hex = row.bytes.map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  // length
  let len = classLen(row.bytes[0]);
  if (got.len !== undefined) len = got.len;
  if (row.bytes[0] === 0xf0 || row.bytes[0] === 0xf4) len = row.bytes.length;
  if (len === null || len === undefined) {
    note(`line ${row.line}: \`${row.asm}\` starts with an unallocated first byte ${hex}`);
    continue;
  }
  if (len !== row.bytes.length) {
    note(`line ${row.line}: \`${row.asm}\` is ${row.bytes.length} bytes, class says ${len} (${hex})`);
    continue;
  }
  if (got.mn !== want.mn || !got.ops || !same(got.ops, want.ops)) {
    note(`line ${row.line}: \`${row.asm}\` decodes as \`${got.mn} ${(got.ops ?? []).map(fmt).join(', ')}\` (${hex})`);
    continue;
  }
  checked++;
}

function fmt(o) {
  switch (o.t) {
    case 'reg': return 'r' + o.n;
    case 'freg': return 'f' + o.n;
    case 'imm': return '#' + o.v;
    case 'rel': return (o.v >= 0 ? '+' : '') + o.v;
    case 'abs': return '0x' + o.v.toString(16);
    case 'mem': {
      if (o.form === 'abs') return '[0x' + o.addr.toString(16) + ']';
      const base = o.base === 'r' ? 'r' + o.n : o.base;
      if (o.form === 'ind') return `[${base}]`;
      if (o.form === 'post') return `[${base}]+`;
      if (o.form === 'pre') return `-[${base}]`;
      return `[${base}${o.disp >= 0 ? '+' : ''}${o.disp}]`;
    }
    default: return JSON.stringify(o);
  }
}

// Appendix B: coverage and lengths
const bEnd = lines.findIndex((l, i) => i > bStart && l.startsWith('## Appendix'));
const bLines = [];
for (let i = bStart; i < (bEnd === -1 ? lines.length : bEnd); i++) {
  const m = /^(0x[0-9A-F]{2})(?:-(0x[0-9A-F]{2}))?\s+(\S.*?)\s{2,}(\S+)$/.exec(lines[i].trim());
  if (m) bLines.push({ lo: parseInt(m[1], 16), hi: parseInt(m[2] ?? m[1], 16), len: m[4] });
}
let expectNext = 0;
for (const e of bLines) {
  if (e.lo !== expectNext) note(`Appendix B: no entry for 0x${expectNext.toString(16).padStart(2, '0')} (entry at 0x${e.lo.toString(16)})`);
  if (e.hi < e.lo) note(`Appendix B: bad range 0x${e.lo.toString(16)}-0x${e.hi.toString(16)}`);
  const single = /^\d+$/.test(e.len) ? +e.len : null;
  if (single !== null) {
    for (let b = e.lo; b <= e.hi; b++) {
      const want = classLen(b);
      if (want !== null && want !== single) note(`Appendix B: 0x${b.toString(16).padStart(2, '0')} listed as ${single} bytes, length map says ${want}`);
    }
  }
  expectNext = e.hi + 1;
}
if (expectNext !== 0x100) note(`Appendix B: coverage ends at 0x${(expectNext - 1).toString(16)}, expected 0xFF`);

// markdown table shapes (escaped pipes do not split cells)
function checkTables(docLines, label) {
  let block = [];
  const flush = () => {
    if (block.length === 0) return;
    const cells = (r) => r.replace(/^\||\|$/g, '').replace(/\\\|/g, '').split('|').length;
    const n = cells(block[0].text);
    if (!/^\|[\s:|-]+\|$/.test(block[1].text.trim())) {
      note(`${label} line ${block[0].n}: table has no delimiter row`);
    } else {
      const dd = cells(block[1].text);
      if (dd !== n) note(`${label} line ${block[1].n}: delimiter has ${dd} cells, header ${n}`);
      for (const r of block.slice(2)) {
        const c = cells(r.text);
        if (c !== n) note(`${label} line ${r.n}: ${c} cells, table header has ${n}: ${r.text.slice(0, 60)}`);
      }
    }
    block = [];
  };
  docLines.forEach((t, i) => {
    if (/^\s*\|/.test(t)) block.push({ n: i + 1, text: t });
    else flush();
  });
  flush();
}
checkTables(lines, isaPath);
if (devLines.length) checkTables(devLines, devPath);

// ------------------------------------------------------------------ memory map
// Every `0x...–0x...` pair in the Appendix C table must tile the whole 32-bit
// space exactly once, with no gap and no overlap.
const hex = (s) => parseInt(s.replace(/_/g, ''), 16);
const RANGE = /`(0x[0-9A-Fa-f_]+)`\s*[–-]\s*`(0x[0-9A-Fa-f_]+)`/;
function rangesOf(docLines, startMarker) {
  const start = docLines.findIndex((l) => l.startsWith(startMarker));
  if (start === -1) return [];
  const out = [];
  for (let i = start + 1; i < docLines.length; i++) {
    if (/^## /.test(docLines[i])) break;
    if (!/^\s*\|/.test(docLines[i])) continue;
    const m = RANGE.exec(docLines[i]);
    if (m) out.push({ lo: hex(m[1]), hi: hex(m[2]), line: i + 1, text: docLines[i] });
  }
  return out;
}
const cStart = '## Appendix C';
const map = rangesOf(lines, cStart);
if (map.length === 0) note('Appendix C: no memory-map rows found');
let next = 0;
for (const r of map) {
  if (r.lo !== next) {
    note(`Appendix C line ${r.line}: expected 0x${next.toString(16).padStart(8, '0')}, row starts at 0x${r.lo.toString(16).padStart(8, '0')}`);
  }
  if (r.hi < r.lo) note(`Appendix C line ${r.line}: range ends before it starts`);
  next = r.hi + 1;
}
// The same map is listed in §2.3 and Appendix C; they must not drift apart.
const sectionMap = rangesOf(lines, '### 2.3 Memory');
const rangeKey = (rs) => rs.map((r) => `${r.lo.toString(16)}-${r.hi.toString(16)}`).join(',');
if (sectionMap.length && rangeKey(sectionMap) !== rangeKey(map)) {
  note(`§2.3 lists ${sectionMap.length} ranges and Appendix C lists ${map.length}; they disagree`);
}
if (map.length && next !== 0x100000000) {
  note(`Appendix C: coverage ends at 0x${(next - 1).toString(16).padStart(8, '0')}, expected 0xFFFFFFFF`);
}

// --------------------------------------------------------------- device pages
// Both documents carry a page table: page number + base + device. The bases must
// be 0xFFF00000 + page*0x1000, both tables must agree, and together they must
// tile the 960 KiB window.
function pageTable(docLines, label) {
  const rows = [];
  for (let i = 0; i < docLines.length; i++) {
    const m = /^\|\s*(\d+)(?:[–-](\d+))?\s*\|\s*`(0x[0-9A-Fa-f]{8})`(?:\s*[–-]\s*`(0x[0-9A-Fa-f]{8})`)?\s*\|/.exec(docLines[i]);
    if (m) rows.push({ lo: +m[1], hi: m[2] ? +m[2] : +m[1], base: hex(m[3]), end: m[4] ? hex(m[4]) : null, line: i + 1 });
  }
  for (const r of rows) {
    const want = 0xfff00000 + r.lo * 0x1000;
    if (r.base !== want) note(`${label} line ${r.line}: page ${r.lo} base 0x${r.base.toString(16)} should be 0x${want.toString(16)}`);
    const wantEnd = 0xfff00000 + r.hi * 0x1000 + 0xfff;
    if (r.end === null) {
      if (r.base + 0xfff !== wantEnd) note(`${label} line ${r.line}: single-page row for page ${r.lo} covers only its own page`);
    } else if (r.end !== wantEnd) {
      note(`${label} line ${r.line}: page range ${r.lo}-${r.hi} end 0x${r.end.toString(16)} should be 0x${wantEnd.toString(16)}`);
    }
  }
  return rows;
}
const isaPages = pageTable(lines, isaPath);
const devPages = pageTable(devLines, devPath);
if (isaPages.length === 0) note(`${isaPath}: no device page table found`);
if (devLines.length && devPages.length === 0) note(`${devPath}: no device page table found`);
if (isaPages.length && devPages.length) {
  const key = (rs) => rs.map((r) => `${r.lo}-${r.hi}`).join(',');
  if (key(isaPages) !== key(devPages)) note('device page tables disagree between the ISA document and the devices document');
}
let pageNext = 0;
for (const r of isaPages) {
  if (r.lo !== pageNext) note(`${isaPath} line ${r.line}: device pages jump from ${pageNext} to ${r.lo}`);
  pageNext = r.hi + 1;
}
if (isaPages.length && pageNext !== 240) note(`${isaPath}: device pages end at ${pageNext - 1}, expected 239`);

// ---------------------------------------------------------------- report

if (fail.length === 0) {
  console.log(`ok: ${checked}/${rows.length} vectors; Appendix B covers 0x00-0xFF; Appendix C tiles 4 GiB; ${isaPages.length} device page rows tile the window; tables well-formed`);
  process.exit(0);
}
console.log(`${fail.length} problem(s) across ${rows.length} vectors:\n`);
for (const f of fail) console.log('  - ' + f);
process.exit(1);

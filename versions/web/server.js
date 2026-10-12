#!/usr/bin/env node
/*
 * OLYMPIAD EVALUATOR - WEB VERSION (OFFLINE APP: NODE.JS, NO DEPENDENCIES)
 * USAGE: NODE SERVER.JS [--PORT 8765] [--NO-BROWSER] [--NO-HELPER]
 * LIMITS AND CPU TIME VIA THE OLIEVAL-RUN HELPER (HELPER/, PREBUILT IN BIN/)
 * FULL SPEC IN DOCS/SPECIFICATION.MD
 */
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const { spawn, spawnSync } = require('child_process');

const APP_NAME = 'Olympiad Evaluator';
const VERSION = '1.0.0';
const VARIANT = 'Web';
const IS_WIN = process.platform === 'win32';
const OS_NAME = IS_WIN ? 'windows' : (process.platform === 'darwin' ? 'darwin' : 'linux');
const HERE = __dirname;

/* === GENERAL UTILITIES === */
function asciiLower(s) { return String(s).replace(/[A-Z]/g, c => String.fromCharCode(c.charCodeAt(0) + 32)); }

function natcmp(a, b) {
  a = String(a); b = String(b);
  let i = 0, j = 0;
  const la = a.length, lb = b.length;
  const dig = c => c >= '0' && c <= '9';
  while (i < la && j < lb) {
    const ca = a[i], cb = b[j];
    if (dig(ca) && dig(cb)) {
      const si = i; while (i < la && dig(a[i])) i++;
      const sj = j; while (j < lb && dig(b[j])) j++;
      const ra = a.slice(si, i).replace(/^0+/, ''), rb = b.slice(sj, j).replace(/^0+/, '');
      if (ra.length !== rb.length) return ra.length < rb.length ? -1 : 1;
      if (ra !== rb) return ra < rb ? -1 : 1;
    } else {
      const xa = asciiLower(ca), xb = asciiLower(cb);
      if (xa !== xb) return xa < xb ? -1 : 1;
      i++; j++;
    }
  }
  const ra = la - i, rb = lb - j;
  if (ra !== rb) return ra < rb ? -1 : 1;
  if (a === b) return 0;
  return a < b ? -1 : 1;
}

function globMatch(pat, name) {
  const p = asciiLower(pat), n = asciiLower(name);
  let pi = 0, ni = 0, star = -1, mark = 0;
  while (ni < n.length) {
    if (pi < p.length && p[pi] === '*') { star = pi; mark = ni; pi++; }
    else if (pi < p.length && (p[pi] === '?' || p[pi] === n[ni])) { pi++; ni++; }
    else if (star !== -1) { pi = star + 1; mark++; ni = mark; }
    else return false;
  }
  while (pi < p.length && p[pi] === '*') pi++;
  return pi === p.length;
}

function pathMatch(pat, rel) {
  const pc = String(pat).replace(/\\/g, '/').split('/').filter(Boolean);
  const rc = rel.split('/');
  if (!pc.length || pc.length > rc.length) return -1;
  const k = rc.length - pc.length;
  for (let x = 0; x < pc.length; x++) if (!globMatch(pc[x], rc[k + x])) return -1;
  return k;
}

function globFull(pat, s) {
  const pc = String(pat).replace(/\\/g, '/').split('/').filter(Boolean);
  const sc = String(s).replace(/\\/g, '/').split('/').filter(Boolean);
  if (pc.length !== sc.length) return false;
  return pc.every((p, i) => globMatch(p, sc[i]));
}

function splitCmd(s) {
  const out = [];
  let cur = '', inq = false, has = false;
  for (const ch of String(s || '')) {
    if (ch === '"') { inq = !inq; has = true; }
    else if (' \t\r\n'.includes(ch) && !inq) { if (has) { out.push(cur); cur = ''; has = false; } }
    else { cur += ch; has = true; }
  }
  if (has) out.push(cur);
  return out;
}

function subst(t, kv) {
  let s = String(t || '');
  for (const k of Object.keys(kv)) if (kv[k] !== undefined && kv[k] !== null) s = s.split('{' + k + '}').join(String(kv[k]));
  return s;
}

function resolveP(base, p) {
  p = String(p || '').trim();
  if (!p) return path.resolve(base);
  p = IS_WIN ? p.replace(/\//g, '\\') : p.replace(/\\/g, '/');
  return path.resolve(base, p);
}
function joinRel(base, rel) { return path.resolve(base, ...String(rel).split('/').filter(Boolean)); }
function extOf(name) {
  const b = String(name).replace(/\\/g, '/').split('/').pop();
  const i = b.lastIndexOf('.');
  return i > 0 ? asciiLower(b.slice(i)) : '';
}
function fmtInt(x) { return String(Math.floor(Number(x) + 0.5)); }
function round2(x) { return Math.floor(x * 100 + 0.5) / 100; }
function num(v, d = 0) {
  if (v === null || v === undefined || v === '') return d;
  if (typeof v === 'boolean') return v ? 1 : 0;
  const x = Number(v);
  return Number.isFinite(x) ? x : d;
}
function pad2(n) { return String(n).padStart(2, '0'); }
function nowStr() { const d = new Date(); return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`; }
function stampStr() { const d = new Date(); return `${d.getFullYear()}${pad2(d.getMonth() + 1)}${pad2(d.getDate())}-${pad2(d.getHours())}${pad2(d.getMinutes())}${pad2(d.getSeconds())}`; }
function clock() { const d = new Date(); return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`; }
function isFile(p) { try { return fs.statSync(p).isFile(); } catch (e) { return false; } }
function isDir(p) { try { return fs.statSync(p).isDirectory(); } catch (e) { return false; } }
function exists(p) { try { fs.statSync(p); return true; } catch (e) { return false; } }
function isExec(p) { try { fs.accessSync(p, fs.constants.X_OK); return true; } catch (e) { return false; } }
function mkdirp(d) { fs.mkdirSync(d, { recursive: true }); }
function readBytes(p, limit) {
  if (limit === undefined) return fs.readFileSync(p);
  const fd = fs.openSync(p, 'r');
  try {
    const buf = Buffer.alloc(limit);
    let n = 0;
    while (n < limit) { const k = fs.readSync(fd, buf, n, limit - n, null); if (k <= 0) break; n += k; }
    return buf.subarray(0, n);
  } finally { fs.closeSync(fd); }
}
function readText(p, limit) { return readBytes(p, limit).toString('utf8'); }
function writeText(p, text) { mkdirp(path.dirname(p)); fs.writeFileSync(p, Buffer.from(String(text), 'utf8')); }
function fileSize(p) { try { return fs.statSync(p).size; } catch (e) { return 0; } }
function clearDir(d) {
  if (!isDir(d)) { mkdirp(d); return; }
  for (const name of fs.readdirSync(d)) {
    const p = path.join(d, name);
    try {
      const st = fs.lstatSync(p);
      if (st.isDirectory()) fs.rmSync(p, { recursive: true, force: true }); else fs.unlinkSync(p);
    } catch (e) { /* IGNORE */ }
  }
}
function removeFile(p) { try { const st = fs.lstatSync(p); if (!st.isDirectory()) fs.unlinkSync(p); } catch (e) { /* MISSING */ } }
function copyFile(a, b) { fs.copyFileSync(a, b); }

/* BREADTH-FIRST WALK: [[REL WITH /, ABS, IS_DIR, DEPTH]] */
function walkBfs(top, maxDepth, ignore) {
  const out = [];
  const queue = [[top, '', 0]];
  for (let qi = 0; qi < queue.length; qi++) {
    const [d, rel, depth] = queue[qi];
    let names;
    try { names = fs.readdirSync(d).sort(natcmp); } catch (e) { continue; }
    const sub = [];
    for (const name of names) {
      const ap = path.join(d, name);
      const rp = rel ? rel + '/' + name : name;
      if (isDir(ap)) {
        if (ignore.some(gl => globMatch(gl, name))) continue;
        out.push([rp, ap, true, depth]);
        if (depth < maxDepth) sub.push([ap, rp, depth + 1]);
      } else out.push([rp, ap, false, depth]);
    }
    queue.push(...sub);
  }
  return out;
}

/* === DEFAULTS (SAME AS THE GUI) === */
function defaultLanguages() {
  const py = IS_WIN ? 'python' : 'python3';
  return [
    { id: 'c', name: 'C', ext: '.c', compile: 'gcc -O2 -Wall -std=c11 {sources} -o {exe} {libs}', run: '{exe}', probe: true },
    { id: 'cpp', name: 'C++', ext: '.cpp .cc .cxx', compile: 'g++ -O2 -Wall -std=c++17 {sources} -o {exe} {libs}', run: '{exe}', probe: true },
    { id: 'py', name: 'Python', ext: '.py', compile: '', run: py + ' {main}', probe: true }
  ];
}
const DEFAULT_AUTOLIBS = [
  ['Math', 'math.h complex.h tgmath.h fenv.h', '-lm'], ['Threads', 'pthread.h threads.h', '-pthread'],
  ['Ncurses', 'ncurses.h curses.h', '-lncurses'], ['Readline', 'readline/readline.h', '-lreadline'],
  ['SDL2', 'SDL2/SDL.h', '-lSDL2'], ['SDL2_image', 'SDL2/SDL_image.h', '-lSDL2_image'], ['SDL2_ttf', 'SDL2/SDL_ttf.h', '-lSDL2_ttf'],
  ['SDL2_mixer', 'SDL2/SDL_mixer.h', '-lSDL2_mixer'], ['Raylib', 'raylib.h', '-lraylib -lGL -lm -lpthread -ldl -lrt -lX11'],
  ['GLFW', 'GLFW/glfw3.h', '-lglfw -lGL'], ['GLEW', 'GL/glew.h', '-lGLEW'], ['OpenGL', 'GL/*', '-lGL -lGLU -lglut'],
  ['wxWidgets', 'wx/*', '$(wx-config --cxxflags --libs)'],
  ['X11', 'X11/Xlib.h', '-lX11'], ['Zlib', 'zlib.h', '-lz'], ['SQLite', 'sqlite3.h', '-lsqlite3'], ['Curl', 'curl/curl.h', '-lcurl'],
  ['OpenSSL', 'openssl/*.h', '-lssl -lcrypto'], ['GMP', 'gmp.h', '-lgmp'], ['Dynamic loading', 'dlfcn.h', '-ldl'],
  ['Realtime', 'mqueue.h aio.h', '-lrt'], ['Crypt', 'crypt.h', '-lcrypt']
].map(([name, headers, flags]) => ({ name, headers, flags }));
const DEFAULT_IGNORE = '.* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__';
const DEFAULT_ROOT_RULES = ['CMakeLists.txt', '*.cbp', 'bash-scripts/compile.sh', 'bash-scripts/build.sh', 'compile.sh', 'build.sh',
  'main.cpp', 'main.c', '*.cpp', '*.c', '*.py'].map(pattern => ({ pattern, up: 0 }));

function g(obj, key, def) {
  if (obj && typeof obj === 'object' && !Array.isArray(obj)) {
    const v = obj[key];
    if (v !== undefined && v !== null) return v;
  }
  return def;
}
const truthy = v => !!v;

class Cfg {
  constructor(ws, prob) {
    this.ws = ws; this.prob = prob;
    this.root = path.resolve(g(ws, 'root', '') || '.');
    this.name = String(g(prob, 'name', ''));
    this.nr = String(Math.trunc(num(g(prob, 'nr', 1), 1)));
    this.layout = g(ws, 'layout', 'problem-student');
    const langs = g(ws, 'languages', null);
    this.languages = (Array.isArray(langs) && langs.length) ? langs : defaultLanguages();
    const al = g(ws, 'autoLibs', {});
    this.autolibsOn = truthy(g(al, 'enabled', true));
    this.autolibs = g(al, 'rules', null);
    if (this.autolibs === null) this.autolibs = DEFAULT_AUTOLIBS;
    this.ignore = String(g(ws, 'ignoreDirs', DEFAULT_IGNORE) || '').split(/\s+/).filter(Boolean);
    const opt = g(ws, 'options', {});
    this.keepOutputs = truthy(g(opt, 'keepOutputs', true));
    this.winStack = truthy(g(opt, 'windowsStackFlag', true));
    this.compileTimeout = num(g(opt, 'compileTimeoutSec', 60), 60);
    this.nonzeroError = truthy(g(opt, 'nonzeroExitIsError', true));
    this.useProbe = truthy(g(opt, 'useProbe', true));
    const io = g(prob, 'io', {});
    this.ioInput = g(io, 'input', 'file');
    this.ioOutput = g(io, 'output', 'file');
    this.ioInpath = g(io, 'inputPath', '{problem}.in');
    this.ioOutpath = g(io, 'outputPath', '{problem}.out');
    this.ioCwd = g(io, 'cwd', '');
    this.runIn = g(io, 'runIn', 'sandbox');
    const pj = g(prob, 'project', {});
    const rr = g(pj, 'rootRules', null);
    this.rootRules = (Array.isArray(rr) && rr.length) ? rr : DEFAULT_ROOT_RULES;
    this.maxDepth = Math.trunc(num(g(pj, 'maxDepth', 6), 6));
    this.mainPatterns = String(g(pj, 'mainPatterns', 'main.cpp main.c {problem}.cpp {problem}.c *.cpp *.c *.py') || '').split(/\s+/).filter(Boolean);
    this.compileScope = g(pj, 'compileScope', 'main');
    this.buildMode = g(pj, 'buildMode', 'command');
    this.buildCmd = String(g(pj, 'buildCommand', 'auto') || 'auto');
    this.buildExe = String(g(pj, 'buildExe', 'release/program') || 'release/program');
    this.sizeScope = g(pj, 'sizeScope', 'main');
    this.sizeExt = String(g(pj, 'sizeExt', '.c .h .cpp .hpp .cc .cxx .hh .py') || '').split(/\s+/).filter(Boolean).map(asciiLower);
    this.compileExtra = g(prob, 'compileExtra', '') || '';
    const lim = g(prob, 'limits', {});
    this.timeMs = num(g(lim, 'timeMs', 1000), 1000);
    this.timeMode = g(lim, 'timeMode', 'cpu');
    this.hardTime = Math.max(1, num(g(lim, 'hardTimeFactor', 2), 2));
    this.memMb = num(g(lim, 'memMb', 64), 64);
    this.hardMem = Math.max(1, num(g(lim, 'hardMemFactor', 2), 2));
    this.stackMb = num(g(lim, 'stackMb', 8), 8);
    this.sourceKb = num(g(lim, 'sourceKb', 0), 0);
    this.outputMb = num(g(lim, 'outputMb', 64), 64);
    this.calibrate = truthy(g(lim, 'calibrate', true));
    const cmp = g(prob, 'compare', {});
    this.cmpMode = g(cmp, 'mode', 'lines');
    this.ignoreCase = truthy(g(cmp, 'ignoreCase', false));
    this.epsilon = num(g(cmp, 'epsilon', 1e-6), 1e-6);
    this.checker = g(cmp, 'checker', '') || '';
    this.protocol = g(cmp, 'protocol', 'oji');
    this.checkerArgs = g(cmp, 'args', '') || '';
    this.v = { root: this.root, problem: this.name, nr: this.nr };
    this.testsDir = resolveP(this.root, subst(g(prob, 'testsDir', '') || g(ws, 'testsDir', '{root}/{problem}/_tests'), this.v));
    this.workDir = resolveP(this.root, subst(g(ws, 'workDir', '{root}/_work'), this.v));
    this.resultsDir = resolveP(this.root, subst(g(ws, 'resultsDir', '{root}/_results'), this.v));
    this.exeSuffix = IS_WIN ? '.exe' : '';
  }
  langExts(L) { return String(g(L, 'ext', '')).split(/\s+/).filter(Boolean).map(asciiLower); }
  langFor(filename) {
    const e = extOf(filename);
    if (!e) return null;
    for (const L of this.languages) if (this.langExts(L).includes(e)) return L;
    return null;
  }
  allExts() { const out = []; for (const L of this.languages) out.push(...this.langExts(L)); return out; }
  /* PROBE KIND: C = LINKED, PY = SITECUSTOMIZE, PRELOAD = LD_PRELOAD (SCRIPTS, LINUX), EMPTY = NONE */
  probeMode(L) {
    if (!this.useProbe || !truthy(g(L, 'probe', false))) return '';
    if (String(g(L, 'compile', '') || '').trim()) {
      if (this.buildMode === 'scripts') return OS_NAME === 'linux' ? 'preload' : '';
      return 'c';
    }
    if (this.langExts(L).includes('.py')) return 'py';
    return '';
  }
  probeEnv(L, probeFile, job) {
    const env = ['OLIEVAL_PROBE=' + probeFile];
    const mode = this.probeMode(L);
    if (mode === 'py' && job.pyProbeDir) {
      const old = process.env.PYTHONPATH || '';
      env.push('PYTHONPATH=' + job.pyProbeDir + (old ? path.delimiter + old : ''));
    }
    if (mode === 'preload' && job.preloadSo) {
      const old = process.env.LD_PRELOAD || '';
      env.push('LD_PRELOAD=' + job.preloadSo + (old ? ' ' + old : ''));
    }
    return env.concat(job.envPathAdd);
  }
  probeActive(L, job) { const m = this.probeMode(L); return !!m && (m !== 'preload' || !!job.preloadSo); }
}

function findProblem(ws, name) {
  for (const p of g(ws, 'problems', []) || []) if (g(p, 'name', '') === name) return p;
  return null;
}

/* === PROGRAM LOOKUP === */
const WIN_EXTRA_DIRS = [
  'C:\\OJI\\Codeblocks\\MinGW\\bin', 'D:\\OJI\\Codeblocks\\MinGW\\bin', 'C:\\OJI\\CodeBlocks\\MinGW\\bin',
  'C:\\Program Files\\CodeBlocks\\MinGW\\bin', 'C:\\Program Files (x86)\\CodeBlocks\\MinGW\\bin',
  'C:\\MinGW\\bin', 'C:\\mingw64\\bin', 'C:\\msys64\\ucrt64\\bin', 'C:\\msys64\\mingw64\\bin',
  'C:\\TDM-GCC-64\\bin', 'C:\\TDM-GCC-32\\bin', 'C:\\Program Files\\Dev-Cpp\\MinGW64\\bin', 'C:\\Program Files (x86)\\Dev-Cpp\\MinGW64\\bin'
];
function winCands(d, n) {
  if (path.basename(n).includes('.')) return [path.join(d, n)];
  return ['.exe', '.com', '.bat', '.cmd'].map(e => path.join(d, n + e)).concat([path.join(d, n)]);
}
function findPythonInstall() {
  const bases = [];
  if (process.env.LOCALAPPDATA) bases.push(path.join(process.env.LOCALAPPDATA, 'Programs', 'Python'));
  bases.push('C:\\', 'C:\\Program Files');
  for (const b of bases) {
    let names = [];
    try { names = fs.readdirSync(b).filter(n => n.toLowerCase().startsWith('python3') && isDir(path.join(b, n))); } catch (e) { continue; }
    for (const n of names.sort().reverse()) { const c = path.join(b, n, 'python.exe'); if (isFile(c)) return c; }
  }
  return null;
}
const programCache = new Map();
function findProgram(name) {
  if (!name) return null;
  if (programCache.has(name)) { const c = programCache.get(name); if (c && isFile(c)) return c; }
  const r = findProgramNoCache(name);
  if (!(name.includes('/') || name.includes('\\'))) programCache.set(name, r);
  return r;
}
function findProgramNoCache(name) {
  if (name.includes('/') || name.includes('\\') || (IS_WIN && name.length > 1 && name[1] === ':')) {
    const cands = IS_WIN ? winCands(path.dirname(name) || '.', path.basename(name)) : [name];
    for (const c of cands) if (isFile(c) && (IS_WIN || isExec(c))) return path.resolve(c);
    return null;
  }
  if (!IS_WIN) {
    for (const d of (process.env.PATH || '/usr/local/bin:/usr/bin:/bin').split(':')) {
      const c = path.join(d || '.', name);
      if (isFile(c) && isExec(c)) return path.resolve(c);
    }
    return null;
  }
  const isPy = ['python3', 'python', 'python3.exe', 'python.exe'].includes(name.toLowerCase());
  let store = null;
  const search = n => {
    for (let d of (process.env.PATH || '').split(path.delimiter)) {
      d = d.trim().replace(/^"|"$/g, '');
      if (!d) continue;
      for (const c of winCands(d, n)) {
        if (isFile(c)) {
          if (isPy && c.toLowerCase().includes('\\windowsapps\\')) { if (!store) store = c; continue; }
          return c;
        }
      }
    }
    return null;
  };
  let found = search(name);
  if (!found && isPy) found = search('python') || search('py') || findPythonInstall();
  if (!found) {
    outer: for (const d of WIN_EXTRA_DIRS) for (const c of winCands(d, name)) if (isFile(c)) { found = c; break outer; }
  }
  return found || store;
}

/* === RUN A PROCESS: VIA OLIEVAL-RUN (LIMITS + MEASUREMENTS) OR DIRECTLY === */
const SIG_TEXT = {
  SIGSEGV: 'SIGSEGV - invalid memory access (array out of bounds, bad pointer or stack overflow)',
  SIGFPE: 'SIGFPE - arithmetic error (e.g. division by zero)',
  SIGABRT: 'SIGABRT - program aborted (failed assert or uncaught exception)',
  SIGKILL: 'SIGKILL - program killed',
  SIGXCPU: 'SIGXCPU - CPU time limit exceeded',
  SIGXFSZ: 'SIGXFSZ - output file too large',
  SIGBUS: 'SIGBUS - memory access error',
  SIGILL: 'SIGILL - illegal instruction',
  SIGPIPE: 'SIGPIPE - write to a closed pipe',
  SIGTERM: 'SIGTERM - program terminated'
};
const SIGNAME_BY_NUM = {};
for (const [k, v] of Object.entries(os.constants.signals || {})) if (SIGNAME_BY_NUM[v] === undefined) SIGNAME_BY_NUM[v] = k;
const NTSTATUS_NAMES = {
  0xC0000005: 'ACCESS_VIOLATION - invalid memory access (array out of bounds or bad pointer)',
  0xC00000FD: 'STACK_OVERFLOW - stack overflow (recursion too deep or large local arrays)',
  0xC0000094: 'INTEGER_DIVIDE_BY_ZERO - division by zero',
  0xC000008E: 'FLOAT_DIVIDE_BY_ZERO - division by zero (floating point)',
  0xC0000095: 'INTEGER_OVERFLOW - arithmetic overflow',
  0xC0000409: 'STACK_BUFFER_OVERRUN - program aborted (abort or buffer overrun)',
  0xC0000017: 'NO_MEMORY - not enough memory',
  0xC000012D: 'COMMITMENT_LIMIT - memory limit reached',
  0xC000001D: 'ILLEGAL_INSTRUCTION - illegal instruction',
  0xC0000374: 'HEAP_CORRUPTION - heap corruption',
  0xC0000135: 'DLL_NOT_FOUND - a DLL is missing (e.g. libstdc++-6.dll)',
  0xC0000139: 'ENTRYPOINT_NOT_FOUND - incompatible DLL',
  0xC0000142: 'DLL_INIT_FAILED - DLL initialization failed',
  0xC000013A: 'CONTROL_C_EXIT - program interrupted',
  0xC0000420: 'ASSERTION_FAILURE - failed assert'
};
function statusText(r) {
  if (!r.started) return r.error;
  if (r.timedOut) return 'stopped at the time limit';
  if (r.cancelled) return 'stopped by the user';
  if (r.signal) { const n = SIGNAME_BY_NUM[r.signal]; return (n && SIG_TEXT[n]) || ('signal ' + r.signal); }
  if (IS_WIN && r.winStatus >= 0xC0000000) return NTSTATUS_NAMES[r.winStatus] || ('runtime error (code 0x' + r.winStatus.toString(16).toUpperCase().padStart(8, '0') + ')');
  return 'exit code ' + r.exit;
}
function newResult() {
  return { started: false, error: '', exited: false, exit: 0, winStatus: 0, signal: 0, timedOut: false, cancelled: false,
    wallMs: 0, cpuMs: 0, peakKb: 0, peakCommitKb: 0, memLimitHit: false, cpuKnown: true };
}

const HELPER = { path: null, checked: false, note: 'fallback' };
function helperName() { return IS_WIN ? 'olieval-run.exe' : 'olieval-run'; }
function helperCandidates() {
  const plat = OS_NAME + '-' + process.arch;
  return [path.join(HERE, 'bin', plat, helperName()), path.join(os.homedir(), '.olympiad-evaluator', 'bin', plat, helperName())];
}
function testHelper(p) {
  try { if (!IS_WIN) fs.chmodSync(p, 0o755); } catch (e) { /* MAY BE READ-ONLY */ }
  const r = spawnSync(p, ['--wall', '10000', '--', process.execPath, '-e', '0'], { encoding: 'utf8', timeout: 20000, windowsHide: true });
  if (r.error || r.status !== 0 || !r.stdout) return false;
  try { return JSON.parse(r.stdout.trim().split('\n').pop()).started === true; } catch (e) { return false; }
}
function ensureHelper() {
  if (HELPER.checked) return HELPER.path;
  HELPER.checked = true;
  for (const c of helperCandidates()) if (isFile(c) && testHelper(c)) { HELPER.path = c; HELPER.note = 'olieval-run'; return c; }
  const src = [path.join(HERE, 'helper', 'runner.c'), path.join(HERE, 'helper', 'olieval_run.c')];
  if (src.every(isFile)) {
    const cc = findProgram('gcc') || findProgram('cc') || findProgram('clang');
    if (cc) {
      const out = helperCandidates()[1];
      try { mkdirp(path.dirname(out)); } catch (e) { /* IGNORE */ }
      const args = ['-O2', '-o', out].concat(src);
      if (IS_WIN) args.push('-static');
      const r = spawnSync(cc, args, { encoding: 'utf8', timeout: 180000, windowsHide: true });
      if (r.status === 0 && isFile(out) && testHelper(out)) { HELPER.path = out; HELPER.note = 'olieval-run (compiled now)'; return out; }
      console.log('Could not compile olieval-run: ' + String(r.stderr || r.error || '').slice(0, 400));
    }
  }
  HELPER.note = 'fallback (no memory limit; CPU time only through the probe)';
  return null;
}

function runProcess(argv, o = {}) {
  const r = newResult();
  if (!argv || !argv.length) { r.error = 'Empty command'; return Promise.resolve(r); }
  const prog = findProgram(argv[0]);
  if (!prog) { r.error = 'Program not found: ' + argv[0]; return Promise.resolve(r); }
  const args = [prog].concat(argv.slice(1));
  return HELPER.path ? runViaHelper(r, args, o) : runFallback(r, args, o);
}

function runViaHelper(r, args, o) {
  return new Promise(resolve => {
    const ha = [];
    if (o.cwd) ha.push('--cwd', o.cwd);
    if (o.stdin) ha.push('--stdin', o.stdin);
    if (o.stdout) ha.push('--stdout', o.stdout);
    if (o.stderr) ha.push('--stderr', o.stderr);
    if (o.wall) ha.push('--wall', String(Math.round(o.wall)));
    if (o.cpu) ha.push('--cpu', String(Math.round(o.cpu)));
    if (o.mem) ha.push('--mem', String(Math.round(o.mem)));
    if (o.stack) ha.push('--stack', String(Math.round(o.stack)));
    if (o.fsize) ha.push('--fsize', String(Math.round(o.fsize)));
    if (o.single) ha.push('--single');
    for (const e of o.env || []) ha.push('--env', e);
    ha.push('--');
    let child;
    try { child = spawn(HELPER.path, ha.concat(args), { stdio: ['ignore', 'pipe', 'ignore'], windowsHide: true }); }
    catch (e) { r.error = 'Cannot start the helper: ' + e.message; resolve(r); return; }
    let out = '', finished = false, asked = false;
    const tick = o.cancel ? setInterval(() => {
      if (!asked && o.cancel()) { asked = true; try { child.kill(IS_WIN ? undefined : 'SIGTERM'); } catch (e) { /* ALREADY EXITED */ } }
    }, 40) : null;
    child.stdout.on('data', d => { out += d; });
    const finish = () => {
      if (finished) return;
      finished = true;
      if (tick) clearInterval(tick);
      try {
        const j = JSON.parse(out.trim().split('\n').pop());
        for (const k of ['started', 'error', 'cancelled', 'exited', 'exit', 'winStatus', 'signal', 'timedOut', 'wallMs', 'cpuMs', 'peakKb', 'peakCommitKb', 'memLimitHit']) if (j[k] !== undefined) r[k] = j[k];
      } catch (e) {
        if (asked) { r.started = true; r.cancelled = true; } else r.error = 'The olieval-run helper returned no result';
      }
      resolve(r);
    };
    child.on('error', e => { r.error = 'Cannot start the helper: ' + e.message; finished = true; if (tick) clearInterval(tick); resolve(r); });
    child.on('close', finish);
  });
}

function runFallback(r, args, o) {
  r.cpuKnown = false;   // NO HELPER: CHILD CPU TIME CANNOT BE MEASURED FROM OUTSIDE
  return new Promise(resolve => {
    const nul = IS_WIN ? '\\\\.\\NUL' : '/dev/null';
    let fin, fout, ferr;
    try {
      fin = fs.openSync(o.stdin || nul, 'r');
      fout = fs.openSync(o.stdout || nul, 'w');
      ferr = (o.stderr && o.stdout && o.stderr === o.stdout) ? fout : fs.openSync(o.stderr || nul, 'w');
    } catch (e) { r.error = 'Cannot open the input/output files: ' + e.message; resolve(r); return; }
    const closeAll = () => { for (const fd of new Set([fin, fout, ferr])) { try { fs.closeSync(fd); } catch (e) { /* IGNORE */ } } };
    let cmd = args[0], cargs = args.slice(1);
    if (!IS_WIN && (o.mem || o.stack || o.cpu)) {
      const lim = [];
      if (o.cpu) lim.push('ulimit -t ' + Math.round(o.cpu));
      if (o.mem) lim.push('ulimit -v ' + Math.round(o.mem));
      if (o.stack) lim.push('ulimit -s ' + Math.round(o.stack));
      cargs = ['-c', lim.map(x => x + ' 2>/dev/null').join('; ') + '; exec "$0" "$@"', cmd].concat(cargs);
      cmd = '/bin/sh';
    }
    const env = Object.assign({}, process.env);
    for (const kv of o.env || []) { const i = kv.indexOf('='); env[kv.slice(0, i)] = kv.slice(i + 1); }
    const t0 = process.hrtime.bigint();
    let child, killed = false, finished = false;
    try { child = spawn(cmd, cargs, { cwd: o.cwd || undefined, env, stdio: [fin, fout, ferr], detached: !IS_WIN, windowsHide: true }); }
    catch (e) { closeAll(); r.error = 'Cannot start the program: ' + e.message; resolve(r); return; }
    const kill = () => { try { if (!IS_WIN) process.kill(-child.pid, 'SIGKILL'); else child.kill(); } catch (e) { try { child.kill('SIGKILL'); } catch (e2) { /* ALREADY EXITED */ } } };
    const tick = setInterval(() => {
      const el = Number(process.hrtime.bigint() - t0) / 1e6;
      if (!killed && ((o.cancel && o.cancel()) || (o.wall && el > o.wall))) {
        if (o.cancel && o.cancel()) r.cancelled = true; else r.timedOut = true;
        killed = true; kill();
      }
    }, 5);
    child.on('error', e => {
      if (finished) return;
      finished = true; clearInterval(tick); closeAll();
      r.error = (o.cwd && !isDir(o.cwd)) ? 'Cannot enter the working folder: ' + e.message : 'Cannot start the program: ' + e.message;
      resolve(r);
    });
    child.on('exit', (code, sig) => {
      if (finished) return;
      finished = true; clearInterval(tick); closeAll();
      r.started = true;
      r.wallMs = Number(process.hrtime.bigint() - t0) / 1e6;
      if (sig) { r.signal = (os.constants.signals || {})[sig] || 9; if (sig === 'SIGXCPU') r.timedOut = true; }
      else {
        const c = code >>> 0;
        r.exited = !killed && (!IS_WIN || c < 0xC0000000);
        r.exit = IS_WIN ? (c >= 0x80000000 ? c - 0x100000000 : c) : code;
        r.winStatus = IS_WIN ? c : 0;
      }
      if (!IS_WIN) { try { process.kill(-child.pid, 'SIGKILL'); } catch (e) { /* EMPTY GROUP */ } }
      resolve(r);
    });
  });
}

function readProbe(p) {
  const out = {};
  try {
    for (const line of readText(p, 4096).split(/\r?\n/)) {
      const parts = line.trim().split(/\s+/);
      if (parts.length === 2) out[parts[0]] = num(parts[1], 0);
    }
  } catch (e) { /* MISSING */ }
  return out;
}

/* === DETECTION: TESTS, STUDENTS, PROJECTS === */
function listTestsFiles(d) { return walkBfs(d, 1, []).filter(e => !e[2]).map(e => e[0]); }
function matchN(pattern, rel) {
  const i = pattern.indexOf('{n}');
  if (i < 0) return globFull(pattern, rel) ? '1' : null;
  const pre = pattern.slice(0, i), suf = pattern.slice(i + 3);
  if (rel.length <= pre.length + suf.length) return null;
  if (asciiLower(rel.slice(0, pre.length)) !== asciiLower(pre)) return null;
  if (suf && asciiLower(rel.slice(rel.length - suf.length)) !== asciiLower(suf)) return null;
  const mid = rel.slice(pre.length, rel.length - suf.length);
  if (!mid || mid.includes('/')) return null;
  return mid;
}
const CHECKER_NAMES = ['verif.cpp', 'verif.c', 'verif.py', 'verif.exe', 'verif'];
function scanTests(C) {
  const res = { testsDir: C.testsDir, testsDirExists: isDir(C.testsDir), tests: [], checker: '' };
  if (!res.testsDirExists) return res;
  const ip = subst(g(C.prob, 'inputPattern', '{n}-{problem}.in'), { problem: C.name, nr: C.nr });
  const op = subst(g(C.prob, 'okPattern', '{n}-{problem}.ok'), { problem: C.name, nr: C.nr });
  const found = new Map();
  for (const rel of listTestsFiles(C.testsDir)) {
    const n = matchN(ip, rel);
    if (n === null || found.has(n)) continue;
    const ok = op.split('{n}').join(n);
    found.set(n, { id: n, input: rel, ok, inputSize: fileSize(joinRel(C.testsDir, rel)), okExists: isFile(joinRel(C.testsDir, ok)), okSize: fileSize(joinRel(C.testsDir, ok)) });
  }
  res.tests = Array.from(found.keys()).sort(natcmp).map(k => found.get(k));
  for (const c of CHECKER_NAMES) if (isFile(path.join(C.testsDir, c))) { res.checker = c; break; }
  return res;
}
function matchFlat(pattern, fname) {
  const toks = [];
  let i = 0;
  while (i < pattern.length) {
    if (pattern.startsWith('{student}', i)) { toks.push(['S']); i += 9; }
    else if (pattern.startsWith('{ext}', i)) { toks.push(['E']); i += 5; }
    else {
      let j = i;
      while (j < pattern.length && !pattern.startsWith('{student}', j) && !pattern.startsWith('{ext}', j)) j++;
      toks.push(['L', asciiLower(pattern.slice(i, j))]);
      i = j;
    }
  }
  const low = asciiLower(fname), caps = {};
  const rec = (ti, pos) => {
    if (ti === toks.length) return pos === fname.length;
    const [kind, lit] = toks[ti];
    if (kind === 'L') return low.startsWith(lit, pos) ? rec(ti + 1, pos + lit.length) : false;
    for (let end = pos + 1; end <= fname.length; end++) {
      const piece = fname.slice(pos, end);
      if (kind === 'E' && piece.includes('.')) break;
      caps[kind] = piece;
      if (rec(ti + 1, end)) return true;
    }
    return false;
  };
  if (rec(0, 0) && caps.S) return [caps.S, caps.E || ''];
  return null;
}
function normcase(p) { return IS_WIN ? p.toLowerCase() : p; }
function discoverStudents(C) {
  const reg = new Map(), order = [];
  for (const s of g(C.ws, 'students', []) || []) {
    const sid = String(g(s, 'id', '')).trim();
    if (sid && !reg.has(sid)) { reg.set(sid, s); order.push(sid); }
  }
  const out = new Map();
  if (C.layout === 'flat') {
    const fdir = resolveP(C.root, subst(g(C.ws, 'flatDir', '{root}/{problem}/sources'), C.v));
    const pat = subst(g(C.ws, 'flatPattern', '{student}{nr}.{ext}'), { problem: C.name, nr: C.nr });
    const exts = C.allExts();
    let names = [];
    try { names = fs.readdirSync(fdir).sort(natcmp); } catch (e) { names = []; }
    for (const name of names) {
      const fp = path.join(fdir, name);
      if (!isFile(fp)) continue;
      const m = matchFlat(pat, name);
      if (!m) continue;
      const [sid, ext] = m;
      if (ext && !exts.includes('.' + asciiLower(ext))) continue;
      if (out.has(sid)) { out.get(sid).warnings.push('Several sources found; using ' + path.basename(out.get(sid).file)); continue; }
      out.set(sid, { id: sid, dir: fdir, exists: true, file: fp, warnings: [] });
    }
    for (const sid of order) if (!out.has(sid)) out.set(sid, { id: sid, dir: fdir, exists: false, file: '', warnings: [] });
  } else {
    let t = subst(g(C.ws, 'studentDir', '{root}/{problem}/{student}'), { root: C.root, problem: C.name, nr: C.nr }).replace(/\\/g, '/');
    const k = t.indexOf('{student}');
    if (k < 0) throw new Error('The student folder template must contain {student}');
    const base = resolveP(C.root, t.slice(0, k) || '.');
    const suffix = t.slice(k + 9);
    const special = new Set([C.testsDir, C.workDir, C.resultsDir].map(normcase));
    let names = [];
    try { names = fs.readdirSync(base).sort(natcmp); } catch (e) { names = []; }
    for (const name of names) {
      if (name.startsWith('.') || name.startsWith('_')) continue;
      const p = path.join(base, name);
      if (!isDir(p) || special.has(normcase(path.resolve(p)))) continue;
      const d = resolveP(base, name + suffix);
      out.set(name, { id: name, dir: d, exists: isDir(d), file: '', warnings: [] });
    }
    for (const sid of order) if (!out.has(sid)) { const d = resolveP(base, sid + suffix); out.set(sid, { id: sid, dir: d, exists: isDir(d), file: '', warnings: [] }); }
  }
  return Array.from(out.keys()).sort(natcmp).map(sid => {
    const s = out.get(sid);
    s.registered = reg.has(sid);
    s.name = String(g(reg.get(sid) || {}, 'name', '') || '');
    return s;
  });
}
function hasMainFunc(p) {
  let t;
  try { t = readBytes(p, 2 * 1024 * 1024); } catch (e) { return false; }
  const isAlnum = c => (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c >= 128;
  let i = 0;
  for (;;) {
    i = t.indexOf('main', i);
    if (i < 0) return false;
    const before = i > 0 ? t[i - 1] : 32;
    let j = i + 4;
    while (j < t.length && (t[j] === 32 || t[j] === 9 || t[j] === 13 || t[j] === 10)) j++;
    if (!(isAlnum(before) || before === 95) && t[j] === 40) return true;
    i += 4;
  }
}
const OBJ_EXTS = ['.c', '.cpp', '.cc', '.cxx', '.c++'];
function detectProject(C, st) {
  const info = { root: '', main: '', mainRel: '', lang: '', langObj: null, sources: [], sizeMain: 0, sizeAll: 0, sizeJudged: 0, warnings: (st.warnings || []).slice() };
  if (C.layout === 'flat') {
    const f = st.file || '';
    info.root = st.dir;
    if (!f) return info;
    info.main = f; info.mainRel = path.basename(f);
    const L = C.langFor(f);
    if (L) { info.lang = g(L, 'id', ''); info.langObj = L; }
    info.sources = [path.basename(f)];
    info.sizeMain = info.sizeAll = info.sizeJudged = fileSize(f);
    return info;
  }
  const sdir = st.dir;
  const entries = walkBfs(sdir, C.maxDepth, C.ignore);
  let rootComps = [];
  for (const rule of C.rootRules) {
    const pat = subst(String(g(rule, 'pattern', '')), { problem: C.name, nr: C.nr }).trim();
    if (!pat) continue;
    const up = Math.trunc(num(g(rule, 'up', 0), 0));
    let hit = null;
    for (const [rp] of entries) { const k = pathMatch(pat, rp); if (k >= 0) { hit = [rp, k]; break; } }
    if (hit) {
      let comps = hit[0].split('/').slice(0, hit[1]);
      if (up > 0) comps = comps.slice(0, Math.max(0, comps.length - up));
      rootComps = comps;
      break;
    }
  }
  const root = rootComps.length ? joinRel(sdir, rootComps.join('/')) : path.resolve(sdir);
  info.root = root;
  const files = walkBfs(root, C.maxDepth, C.ignore).filter(e => !e[2]).map(e => [e[0], e[1]]);
  let main = null;
  for (let pat of C.mainPatterns) {
    pat = subst(pat, { problem: C.name, nr: C.nr });
    const m = files.filter(([rp]) => pathMatch(pat, rp) >= 0);
    if (!m.length) continue;
    main = m.find(([rp, ap]) => extOf(rp) === '.py' || hasMainFunc(ap)) || m[0];
    if (m.length > 1) info.warnings.push(`Several possible main files (${m.length}); chosen: ${main[0]}`);
    break;
  }
  if (!main) { info.warnings.push('Main file not found in ' + root); return info; }
  info.main = main[1]; info.mainRel = main[0];
  const L = C.langFor(main[0]);
  if (!L) { info.warnings.push('Unknown language for ' + main[0]); return info; }
  info.lang = g(L, 'id', ''); info.langObj = L;
  const lexts = C.langExts(L);
  const strSort = (a, b) => (a < b ? -1 : (a > b ? 1 : 0));
  if (C.buildMode === 'objects') info.sources = files.filter(([rp]) => OBJ_EXTS.includes(extOf(rp)) && C.langFor(rp) !== null).map(x => x[0]).sort(strSort);
  else if (C.buildMode === 'scripts') info.sources = files.filter(([rp]) => OBJ_EXTS.includes(extOf(rp))).map(x => x[0]).sort(strSort);
  else if (C.compileScope === 'all') info.sources = files.filter(([rp]) => lexts.includes(extOf(rp))).map(x => x[0]).sort(strSort);
  else info.sources = [main[0]];
  info.sizeMain = fileSize(main[1]);
  info.sizeAll = files.filter(([rp]) => C.sizeExt.includes(extOf(rp))).reduce((a, [, ap]) => a + fileSize(ap), 0);
  info.sizeJudged = C.sizeScope === 'all' ? info.sizeAll : info.sizeMain;
  return info;
}
function publicStudent(C, st, info) {
  return { id: st.id, name: st.name || '', dir: st.dir, exists: st.exists, registered: !!st.registered, root: info.root, main: info.mainRel,
    lang: info.lang, sources: info.sources, sizeMain: info.sizeMain, sizeAll: info.sizeAll, sizeJudged: info.sizeJudged, warnings: info.warnings };
}

/* === COMPILATION === */
function collectIncludes(paths) {
  const names = new Set();
  for (const p of paths) {
    let text;
    try { text = readText(p, 4 * 1024 * 1024); } catch (e) { continue; }
    for (const line of text.split(/\r\n|\r|\n/)) {
      let t = line.replace(/^\s+/, '');
      if (!t.startsWith('#')) continue;
      t = t.slice(1).replace(/^\s+/, '');
      if (!t.startsWith('include')) continue;
      t = t.slice(7).replace(/^\s+/, '');
      if (t.startsWith('<')) { const e = t.indexOf('>'); if (e > 1) names.add(t.slice(1, e).trim()); }
    }
  }
  return names;
}
async function expandFlags(text, job) {
  const out = [];
  text = String(text || '');
  let i = 0;
  while (i < text.length) {
    if (text.startsWith('$(', i) || text[i] === '`') {
      const dollar = text[i] === '$';
      const end = text.indexOf(dollar ? ')' : '`', i + (dollar ? 2 : 1));
      if (end < 0) break;
      const cmd = text.slice(i + (dollar ? 2 : 1), end);
      if (!job.cmdCache.has(cmd)) {
        mkdirp(job.runDirAny);
        const tmp = path.join(job.runDirAny, '_cmd.txt');
        const r = await runProcess(splitCmd(cmd), { stdout: tmp, wall: 20000 });
        const val = (r.started && r.exited && r.exit === 0) ? readText(tmp, 100000).trim() : null;
        job.cmdCache.set(cmd, val);
        if (val === null) job.log(`  Warning: command \"${cmd}\" failed (${statusText(r)}); its options are not added`);
      }
      const v = job.cmdCache.get(cmd);
      if (v) out.push(...splitCmd(v));
      i = end + 1;
    } else {
      let j = i;
      while (j < text.length && !text.startsWith('$(', j) && text[j] !== '`') j++;
      out.push(...splitCmd(text.slice(i, j)));
      i = j;
    }
  }
  return out;
}
function compileOnlyFlags(flags) { return flags.filter(f => !(f.startsWith('-l') || f.startsWith('-L') || f.startsWith('-Wl,'))); }
async function autoLibFlags(C, root, sourcePaths, job) {
  if (!C.autolibsOn) return [];
  const hdrs = walkBfs(root, C.maxDepth, C.ignore).filter(e => !e[2] && ['.h', '.hpp', '.hh', '.hxx'].includes(extOf(e[0]))).map(e => e[1]);
  const names = collectIncludes(sourcePaths.concat(hdrs));
  const flags = [];
  for (const rule of C.autolibs) {
    const pats = String(g(rule, 'headers', '')).split(/\s+/).filter(Boolean);
    let hit = false;
    for (const p of pats) { for (const n of names) if (globFull(p, n)) { hit = true; break; } if (hit) break; }
    if (hit) for (const f of await expandFlags(String(g(rule, 'flags', '')), job)) if (!flags.includes(f)) flags.push(f);
  }
  return flags;
}
function buildArgv(template, sources, libs, exe, main, stackBytes, problem) {
  const out = [];
  for (const tok of splitCmd(template)) {
    if (tok === '{sources}') out.push(...sources);
    else if (tok === '{libs}') out.push(...libs);
    else out.push(tok.split('{exe}').join(exe).split('{main}').join(main).split('{stack}').join(String(stackBytes)).split('{problem}').join(problem));
  }
  return out;
}
function cmdString(argv) { return argv.map(a => (a.includes(' ') || !a) ? `"${a}"` : a).join(' '); }
function freshExe(outDir, C) {
  let exe = path.join(outDir, 'prog' + C.exeSuffix);
  if (exists(exe)) { try { fs.unlinkSync(exe); } catch (e) { exe = path.join(outDir, `prog-${Date.now() % 100000}${C.exeSuffix}`); } }
  return exe;
}
async function compileProgram(C, L, sourcesAbs, mainAbs, outDir, cwd, includeProbe, job, extraLibs) {
  mkdirp(outDir);
  const tmpl = String(g(L, 'compile', '') || '');
  if (!tmpl.trim()) return [true, '', '', 0, ''];
  const exe = freshExe(outDir, C);
  const srcs = sourcesAbs.slice();
  if (includeProbe && C.probeMode(L) === 'c') srcs.push(job.currentProbe);
  const libs = (extraLibs || []).slice();
  const stackBytes = Math.trunc(C.stackMb * 1024 * 1024);
  if (IS_WIN && C.winStack && C.stackMb > 0) libs.push('-Wl,--stack=' + stackBytes);
  libs.push(...splitCmd(C.compileExtra));
  const argv = buildArgv(tmpl, srcs, libs, exe, mainAbs, stackBytes, C.name);
  const logPath = path.join(outDir, 'compile.txt');
  const r = await runProcess(argv, { cwd, stdout: logPath, stderr: logPath, env: job.envPathAdd, wall: C.compileTimeout * 1000, cancel: job.cancelled });
  let log = '';
  try { log = readText(logPath, 20000); } catch (e) { /* MISSING */ }
  const ok = r.started && r.exited && r.exit === 0 && isFile(exe);
  if (!r.started) log = (r.error + '\n' + log).trim();
  else if (r.timedOut) log = (`Compilation exceeded ${Math.trunc(C.compileTimeout)} s\n` + log).trim();
  else if (!ok && !log) log = 'The compiler ended with: ' + statusText(r);
  return [ok, exe, log, r.wallMs, cmdString(argv)];
}
function langCompiler(L, C) {
  const toks = splitCmd(String(g(L, 'compile', '')));
  if (!toks.length) return [null, []];
  const flags = [];
  let skip = false;
  for (const t of toks.slice(1)) {
    if (skip) { skip = false; continue; }
    if (t === '-o') { skip = true; continue; }
    if (t === '{sources}' || t === '{libs}' || t.includes('{exe}') || t.includes('{main}')) continue;
    flags.push(t.split('{stack}').join(String(Math.trunc(C.stackMb * 1024 * 1024))).split('{problem}').join(C.name));
  }
  return [toks[0], flags];
}
async function compileObjects(C, Lmain, root, outDir, job, libs) {
  mkdirp(outDir);
  const exe = freshExe(outDir, C);
  const objDir = path.join(outDir, 'obj');
  clearDir(objDir);
  const files = walkBfs(root, C.maxDepth, C.ignore).filter(e => !e[2]).map(e => e[0]).sort((a, b) => (a < b ? -1 : (a > b ? 1 : 0)));
  const srcs = [];
  for (const rp of files) {
    if (!OBJ_EXTS.includes(extOf(rp))) continue;
    const L = C.langFor(rp);
    if (L && String(g(L, 'compile', '') || '').trim()) srcs.push([rp, L]);
  }
  const log = [];
  let totalMs = 0, ok = true, linkerLang = null;
  const objs = [];
  const cflagsExtra = compileOnlyFlags(libs);
  const logPath = path.join(outDir, 'compile.txt');
  for (const [rp, L] of srcs) {
    const [comp, flags] = langCompiler(L, C);
    const obj = joinRel(objDir, rp.replace(/\.[^./]*$/, '') + '.o');
    mkdirp(path.dirname(obj));
    const argv = [comp].concat(flags, cflagsExtra, ['-c', joinRel(root, rp), '-o', obj]);
    const r = await runProcess(argv, { cwd: root, stdout: logPath, stderr: logPath, env: job.envPathAdd, wall: C.compileTimeout * 1000, cancel: job.cancelled });
    totalMs += r.wallMs;
    const out = isFile(logPath) ? readText(logPath, 20000) : '';
    log.push(`Compiled: ${rp} -> ${path.relative(objDir, obj).split(path.sep).join('/')}`);
    if (out.trim()) log.push(out.replace(/\s+$/, ''));
    if (!(r.started && r.exited && r.exit === 0 && isFile(obj))) { ok = false; if (!r.started) log.push(r.error); }
    objs.push(obj);
    if (extOf(rp) !== '.c') linkerLang = L;
  }
  if (!srcs.length) return [false, '', 'No .c or .cpp files found in ' + root, 0, ''];
  if (ok && C.probeMode(Lmain) === 'c') {
    const Lc = C.langFor('x.c') || Lmain;
    const [comp, flags] = langCompiler(Lc, C);
    const pobj = path.join(outDir, 'olieval_probe.o');
    const r = await runProcess([comp].concat(flags, ['-c', job.currentProbe, '-o', pobj]), { cwd: outDir, stdout: logPath, stderr: logPath, env: job.envPathAdd, wall: C.compileTimeout * 1000 });
    if (r.started && r.exited && r.exit === 0 && isFile(pobj)) objs.push(pobj);
  }
  let cmd = '';
  if (ok) {
    const lk = linkerLang || C.langFor('x.c') || Lmain;
    const linker = langCompiler(lk, C)[0];
    const argv = [linker].concat(objs, ['-o', exe], libs);
    cmd = cmdString(argv);
    const r = await runProcess(argv, { cwd: root, stdout: logPath, stderr: logPath, env: job.envPathAdd, wall: C.compileTimeout * 1000, cancel: job.cancelled });
    totalMs += r.wallMs;
    const out = isFile(logPath) ? readText(logPath, 20000) : '';
    if (out.trim()) log.push(out.replace(/\s+$/, ''));
    ok = r.started && r.exited && r.exit === 0 && isFile(exe);
    log.push(ok ? 'Build completed. Executable created at ' + exe : 'Linking (build) failed.');
  } else log.push('Compilation failed; linking skipped.');
  return [ok, exe, log.join('\n').slice(0, 20000), totalMs, 'compile.sh + build.sh: ' + (cmd || `${srcs.length} sources`)];
}
function findScripts(root, buildCmd) {
  if (buildCmd.trim() && buildCmd.trim() !== 'auto') return buildCmd.trim();
  for (const d of ['bash-scripts/', 'scripts/', '']) {
    if (isFile(joinRel(root, d + 'compile.sh')) && isFile(joinRel(root, d + 'build.sh'))) return `bash ${d}compile.sh && bash ${d}build.sh`;
  }
  for (const d of ['bash-scripts/', 'scripts/', '']) if (isFile(joinRel(root, d + 'build.sh'))) return `bash ${d}build.sh`;
  return null;
}
async function compileScripts(C, root, job) {
  const cmd = findScripts(root, C.buildCmd);
  if (cmd === null) return [false, '', 'Scripts compile.sh / build.sh not found (in bash-scripts/, scripts/ or the project root)', 0, ''];
  const exeRel = subst(C.buildExe, { problem: C.name, nr: C.nr });
  const exe = joinRel(root, exeRel);
  removeFile(exe); removeFile(exe + '.exe');
  const outDir = path.join(C.workDir, 'build', C.name, safeName(job.studentNow));
  mkdirp(outDir);
  const logPath = path.join(outDir, 'compile.txt');
  const argv = IS_WIN ? ['cmd', '/c', cmd] : ['sh', '-c', cmd];
  const r = await runProcess(argv, { cwd: root, stdout: logPath, stderr: logPath, env: job.envPathAdd, wall: C.compileTimeout * 1000, cancel: job.cancelled });
  let log = isFile(logPath) ? readText(logPath, 20000) : '';
  const found = isFile(exe) ? exe : (isFile(exe + '.exe') ? exe + '.exe' : '');
  const ok = r.started && !r.timedOut && !!found;
  if (!r.started) log = (r.error + '\n' + log).trim();
  else if (r.timedOut) log = (`The scripts exceeded ${Math.trunc(C.compileTimeout)} s\n` + log).trim();
  else if (!found) log = (log.replace(/\s+$/, '') + '\nThe scripts did not produce the executable ' + exeRel).trim();
  return [ok, found, log, r.wallMs, cmd];
}
async function buildStudent(C, L, info, bdir, job, libs) {
  if (!String(g(L, 'compile', '') || '').trim()) return compileProgram(C, L, [], info.main, bdir, info.root, false, job);
  if (C.buildMode === 'objects') return compileObjects(C, L, info.root, bdir, job, libs);
  if (C.buildMode === 'scripts') return compileScripts(C, info.root, job);
  const srcs = info.sources.map(s => joinRel(info.root, s));
  return compileProgram(C, L, srcs, info.main, bdir, info.root, true, job, libs);
}
const CALIB_C = '#include <stdio.h>\nint main(void)\n{\n    int x = 0;\n    FILE *f = fopen("calibration.in", "r");\n    if (f != NULL) {\n        if (fscanf(f, "%d", &x) != 1) x = 0;\n        fclose(f);\n    }\n    printf("%d\\n", x * 0);\n    return 0;\n}\n';
const CALIB_CPP = '#include <iostream>\n#include <fstream>\nint main()\n{\n    std::ifstream f("calibration.in");\n    int x = 0;\n    f >> x;\n    std::cout << x * 0 << std::endl;\n    return 0;\n}\n';
const CALIB_SOURCES = { '.c': ['calibration.c', CALIB_C], '.cpp': ['calibration.cpp', CALIB_CPP], '.cc': ['calibration.cc', CALIB_CPP], '.cxx': ['calibration.cxx', CALIB_CPP], '.py': ['calibration.py', 'x = 0\nprint(x)\n'] };
async function calibrate(C, L, job) {
  const lid = String(g(L, 'id', ''));
  let src = null;
  for (const e of C.langExts(L)) if (CALIB_SOURCES[e]) { src = CALIB_SOURCES[e]; break; }
  if (!src) return 0;
  const d = path.join(C.workDir, 'calibration', lid);
  mkdirp(d);
  const sp = path.join(d, src[0]);
  writeText(sp, src[1]);
  const [ok, exe] = await compileProgram(C, L, [sp], sp, d, d, C.buildMode !== 'scripts', job);
  if (!ok) { job.log(`  Calibration ${lid}: compilation failed; memory will not be calibrated.`); return 0; }
  const argv = buildArgv(String(g(L, 'run', '{exe}')), [sp], [], exe, sp, 0, C.name);
  let best = null;
  const probe = path.join(d, 'probe.txt');
  for (let k = 0; k < 3; k++) {
    removeFile(probe);
    const r = await runProcess(argv, { cwd: d, stdout: path.join(d, 'out.txt'), env: C.probeEnv(L, probe, job), wall: 10000 });
    const pr = readProbe(probe);
    const m = pr.rss_kb !== undefined ? Math.trunc(pr.rss_kb) : (C.probeActive(L, job) ? 0 : Math.trunc(r.peakKb));
    if (r.started && r.exited && m > 0) best = best === null ? m : Math.min(best, m);
  }
  job.log(`  Calibration ${lid}: empty program = ${best !== null ? best : '?'} KB`);
  return best || 0;
}

/* === COMPARISON AND CHECKER (ON BYTES, LIKE THE OTHER VERSIONS) === */
const isWs = c => c === 32 || c === 9 || c === 10 || c === 13 || c === 11 || c === 12;
function stripCR(b) { return b.includes(13) ? Buffer.from(b.filter(c => c !== 13)) : b; }
function lowerBytes(b) { const o = Buffer.from(b); for (let i = 0; i < o.length; i++) if (o[i] >= 65 && o[i] <= 90) o[i] += 32; return o; }
function splitWs(b) {
  const out = [];
  let i = 0;
  while (i < b.length) {
    while (i < b.length && isWs(b[i])) i++;
    const s = i;
    while (i < b.length && !isWs(b[i])) i++;
    if (i > s) out.push(b.subarray(s, i));
  }
  return out;
}
function splitLines(b) {
  const out = [];
  let s = 0;
  for (let i = 0; i <= b.length; i++) if (i === b.length || b[i] === 10) { out.push(b.subarray(s, i)); s = i + 1; }
  return out;
}
function rstripSpTab(b) { let e = b.length; while (e > 0 && (b[e - 1] === 32 || b[e - 1] === 9)) e--; return b.subarray(0, e); }
function short(b, n = 60) { const s = Buffer.from(b).toString('utf8'); const cp = Array.from(s); return cp.length <= n ? s : cp.slice(0, n).join('') + '…'; }
function isNumber(t) {
  let i = 0;
  const n = t.length, dg = c => c >= 48 && c <= 57;
  if (i < n && (t[i] === 43 || t[i] === 45)) i++;
  let digits = 0;
  while (i < n && dg(t[i])) { i++; digits++; }
  if (i < n && t[i] === 46) { i++; while (i < n && dg(t[i])) { i++; digits++; } }
  if (!digits) return false;
  if (i < n && (t[i] === 101 || t[i] === 69)) {
    i++;
    if (i < n && (t[i] === 43 || t[i] === 45)) i++;
    let ed = 0;
    while (i < n && dg(t[i])) { i++; ed++; }
    if (!ed) return false;
  }
  return i === n;
}
function compareOutput(C, outBytes, okBytes) {
  let a = stripCR(outBytes), b = stripCR(okBytes);
  if (C.ignoreCase) { a = lowerBytes(a); b = lowerBytes(b); }
  const mode = C.cmpMode;
  if (mode === 'exact') {
    if (a.equals(b)) return [1, ''];
    const n = Math.min(a.length, b.length);
    let i = 0;
    while (i < n && a[i] === b[i]) i++;
    let line = 1;
    for (let k = 0; k < i; k++) if (b[k] === 10) line++;
    return [0, `Difference at line ${line} (byte ${i + 1})`];
  }
  if (mode === 'tokens' || mode === 'real') {
    const ta = splitWs(a), tb = splitWs(b);
    for (let k = 0; k < Math.min(ta.length, tb.length); k++) {
      const x = ta[k], y = tb[k];
      if (x.equals(y)) continue;
      if (mode === 'real' && isNumber(x) && isNumber(y)) {
        const fx = parseFloat(x.toString('latin1')), fy = parseFloat(y.toString('latin1'));
        if (Math.abs(fx - fy) <= C.epsilon * Math.max(1, Math.abs(fy))) continue;
      }
      return [0, `Token ${k + 1}: expected \"${short(y)}\", got \"${short(x)}\"`];
    }
    if (ta.length !== tb.length) return [0, `Different number of values: expected ${tb.length}, got ${ta.length}`];
    return [1, ''];
  }
  const la = splitLines(a).map(rstripSpTab), lb = splitLines(b).map(rstripSpTab);
  while (la.length && la[la.length - 1].length === 0) la.pop();
  while (lb.length && lb[lb.length - 1].length === 0) lb.pop();
  for (let k = 0; k < Math.min(la.length, lb.length); k++) {
    if (!la[k].equals(lb[k])) return [0, `Line ${k + 1}: expected \"${short(lb[k])}\", got \"${short(la[k])}\"`];
  }
  if (la.length !== lb.length) return [0, `Different number of lines: expected ${lb.length}, got ${la.length}`];
  return [1, ''];
}
async function prepareChecker(C, job) {
  let spec = C.checker.trim();
  if (!spec) for (const c of CHECKER_NAMES) if (isFile(path.join(C.testsDir, c))) { spec = c; break; }
  if (!spec) return [null, 'No checker is set'];
  let p = resolveP(C.testsDir, subst(spec, C.v));
  if (!isFile(p)) {
    const found = (!spec.includes('/') && !spec.includes('\\')) ? findProgram(spec) : null;
    if (!found) return [null, 'Checker not found: ' + p];
    p = found;
  }
  const e = extOf(p);
  if (['.c', '.cpp', '.cc', '.cxx'].includes(e)) {
    const L = C.langFor(p);
    if (!L) return [null, 'Unknown language for the checker ' + p];
    const d = path.join(C.workDir, 'checker', C.name);
    const [ok, exe, log] = await compileProgram(C, L, [p], p, d, path.dirname(p), false, job);
    if (!ok) return [null, 'The checker does not compile:\n' + log];
    return [[exe], ''];
  }
  if (e === '.py') {
    const L = C.langFor(p);
    const runt = L ? String(g(L, 'run', 'python3 {main}')) : (IS_WIN ? 'python {main}' : 'python3 {main}');
    return [buildArgv(runt, [p], [], '', p, 0, C.name), ''];
  }
  return [[p], ''];
}
async function runChecker(C, job, checkerArgv, inPath, okPath, outPath) {
  const cdir = path.join(job.runDir(C), 'check');
  clearDir(cdir);
  const names = { in: C.name + '.in', ok: C.name + '.ok', out: C.name + '.out' };
  try {
    copyFile(inPath, path.join(cdir, names.in));
    copyFile(okPath, path.join(cdir, names.ok));
    if (outPath && isFile(outPath)) copyFile(outPath, path.join(cdir, names.out));
  } catch (e) { return [0, null, '', 'cannot prepare the files: ' + e.message]; }
  const proto = C.protocol;
  let argsT = C.checkerArgs.trim();
  if (!argsT) argsT = proto === 'oji' ? '' : (proto === 'fraction' ? '{input} {ok} {output}' : '{input} {output} {ok}');
  const extra = splitCmd(argsT).map(a => a.split('{input}').join(path.join(cdir, names.in)).split('{ok}').join(path.join(cdir, names.ok))
    .split('{output}').join(path.join(cdir, names.out)).split('{problem}').join(C.name));
  const so = path.join(cdir, '_stdout.txt'), se = path.join(cdir, '_stderr.txt');
  const r = await runProcess(checkerArgv.concat(extra), { cwd: cdir, stdout: so, stderr: se, env: job.envPathAdd, wall: 10000, cancel: job.cancelled });
  const out = isFile(so) ? readText(so, 10000).trim() : '';
  const err = isFile(se) ? readText(se, 10000).trim() : '';
  if (!r.started) return [0, null, '', r.error];
  if (r.timedOut) return [0, null, '', 'the checker exceeded 10 s'];
  if (proto === 'exitcode') return [(r.exited && r.exit === 0) ? 1 : 0, null, (out + (err ? ' ' + err : '')).trim().slice(0, 300), ''];
  const m = out.match(/^(\S+)(?:\s+([\s\S]*))?$/);
  if (!m) return [0, null, '', 'the checker printed nothing' + (err ? ': ' + err.slice(0, 200) : '')];
  const valS = m[1].replace(',', '.');
  const val = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(valS) || /^[+-]?(inf|infinity|nan)$/i.test(valS) ? Number(valS.replace(/inf(inity)?/i, 'Infinity').replace(/nan/i, 'NaN')) : NaN;
  if (Number.isNaN(val) && !/nan/i.test(valS)) return [0, null, '', "the checker output is not in the 'score message' format: " + out.slice(0, 200)];
  let msg = (m[2] || '').trim();
  if (proto === 'fraction') { if (err) msg = (msg + ' ' + err).trim(); return [Math.max(0, Math.min(1, val)), null, msg.slice(0, 300), '']; }
  return [0, val, msg.slice(0, 300), ''];
}

/* === SCORING === */
const FATAL = ['RE', 'TLE', 'MLE', 'NOOUT', 'CHKERR', 'SKIP'];
function policyFactor(pol, v, L, H) {
  const mode = g(pol, 'mode', 'zero');
  if (mode === 'ignore') return 1;
  if (mode === 'percent') return Math.max(0, Math.min(100, num(g(pol, 'percent', 50), 50))) / 100;
  if (mode === 'linear') return H > L ? Math.max(0, Math.min(1, 1 - (v - L) / (H - L))) : 0;
  return 0;
}
function policyNote(pol, factor) {
  const mode = g(pol, 'mode', 'zero');
  if (mode === 'ignore') return ' (ignored)';
  if (mode === 'zero' || factor <= 0) return ' (0 points)';
  return ` (score reduced to ${fmtInt(factor * 100)}%)`;
}
function fmtG(x) { return String(Number(x)); }
function scoreTest(C, t, tr) {
  const P = num(g(t, 'points', 0)), TP = num(g(t, 'timePoints', 0)), MP = num(g(t, 'memPoints', 0));
  tr.max = round2(P + TP + MP);
  const raw = tr.raw !== undefined ? tr.raw : 'SKIP';
  const msg0 = tr.msg0 || '';
  if (FATAL.includes(raw)) { tr.status = raw; tr.score = 0; tr.msg = msg0; return 0; }
  const pts = (tr.pts === undefined) ? null : tr.pts;
  let base, f;
  if (pts !== null) {
    base = P > 0 ? Math.max(0, Math.min(pts, P)) : Math.max(0, pts);
    f = P > 0 ? base / P : (pts > 0 ? 1 : 0);
  } else {
    f = Math.max(0, Math.min(1, num(tr.f, 0)));
    base = f * P;
  }
  const Lt = num(g(t, 'timeMs', 0)) || C.timeMs;
  const Lm = num(g(t, 'memMb', 0)) || C.memMb;
  const tval = C.timeMode === 'wall' ? num(tr.wallMs) : num(tr.cpuMs);
  const mval = num(tr.memKb);
  const overT = Lt > 0 && tval > Lt;
  const overM = Lm > 0 && tr.memKb !== null && tr.memKb !== undefined && mval > Lm * 1024;
  const pol = g(C.prob, 'policy', {});
  const ft = overT ? policyFactor(g(pol, 'time', {}), tval, Lt, Lt * C.hardTime) : 1;
  const fm = overM ? policyFactor(g(pol, 'memory', {}), mval, Lm * 1024, Lm * 1024 * C.hardMem) : 1;
  let score = base * ft * fm + (!overT ? f * TP : 0) + (!overM ? f * MP : 0);
  score = round2(score);
  let status = raw, msg = msg0;
  if (overT && g(g(pol, 'time', {}), 'mode', 'zero') !== 'ignore') {
    status = 'TLE';
    msg = `Time limit exceeded: ${fmtInt(tval)} ms > ${fmtInt(Lt)} ms${policyNote(g(pol, 'time', {}), ft)}`;
    if (raw !== 'OK') msg += '; ' + msg0;
  } else if (overM && g(g(pol, 'memory', {}), 'mode', 'zero') !== 'ignore') {
    status = 'MLE';
    msg = `Memory limit exceeded: ${fmtInt(mval)} KB > ${fmtInt(Lm * 1024)} KB${policyNote(g(pol, 'memory', {}), fm)}`;
    if (raw !== 'OK') msg += '; ' + msg0;
  } else {
    const notes = [];
    if (overT) notes.push('time limit exceeded (ignored)');
    if (overM) notes.push('memory limit exceeded (ignored)');
    if (notes.length) msg = msg0 + '; ' + notes.join(', ');
  }
  tr.status = status; tr.score = score; tr.msg = msg;
  return score;
}
function rescore(ws, prob, res) {
  const C = new Cfg(ws, prob);
  const tests = g(prob, 'tests', []) || [];
  const pts = g(prob, 'points', {});
  const pCompile = num(g(pts, 'compile', 0)), pSize = num(g(pts, 'size', 0));
  let maxScore = 0;
  for (const t of tests) maxScore += num(g(t, 'points', 0)) + num(g(t, 'timePoints', 0)) + num(g(t, 'memPoints', 0));
  maxScore = round2(maxScore + pCompile + pSize);
  res.maxScore = maxScore;
  res.tests = tests.map(t => ({ id: String(g(t, 'id', '')), points: num(g(t, 'points', 0)), timePoints: num(g(t, 'timePoints', 0)), memPoints: num(g(t, 'memPoints', 0)) }));
  res.limits = { timeMs: C.timeMs, memMb: C.memMb, stackMb: C.stackMb, sourceKb: C.sourceKb, timeMode: C.timeMode };
  const polSize = g(g(prob, 'policy', {}), 'size', {});
  for (const sr of res.students || []) {
    sr.max = maxScore;
    const notes = (sr.notes || []).filter(n => !String(n).startsWith('Source exceeds'));
    if (['CE', 'NOSRC', 'ERR'].includes(sr.status)) { sr.score = 0; sr.notes = notes; continue; }
    const byId = new Map((sr.tests || []).map(tr => [String(tr.id), tr]));
    const newTests = [];
    let total = 0;
    for (const t of tests) {
      const tid = String(g(t, 'id', ''));
      let tr = byId.get(tid);
      if (!tr) tr = { id: tid, raw: 'SKIP', msg0: 'Not evaluated', f: 0, pts: null };
      total += scoreTest(C, t, tr);
      newTests.push(tr);
    }
    sr.tests = newTests;
    total += pCompile;
    if ('sizeMain' in sr) sr.sizeJudged = C.sizeScope === 'all' ? (sr.sizeAll || 0) : sr.sizeMain; // FOLLOW THE CURRENT SIZE CHOICE
    const sizeOk = C.sourceKb <= 0 || num(sr.sizeJudged) <= C.sourceKb * 1024;
    sr.sizeOk = sizeOk;
    if (sizeOk) total += pSize;
    else {
      const mode = g(polSize, 'mode', 'zero');
      let lim = `Source exceeds the limit: ${(num(sr.sizeJudged) / 1024).toFixed(2)} KB > ${fmtG(C.sourceKb)} KB`;
      if (mode === 'zero') { total = 0; lim += ' (0 points)'; }
      else if (mode === 'percent') { const p = Math.max(0, Math.min(100, num(g(polSize, 'percent', 50), 50))); total = total * p / 100; lim += ` (score reduced to ${fmtInt(p)}%)`; }
      else if (mode === 'penalty') { const pen = num(g(polSize, 'penalty', 10), 10); total = Math.max(0, total - pen); lim += ` (penalty ${fmtG(pen)} points)`; }
      else lim += ' (ignored)';
      notes.push(lim);
    }
    sr.notes = notes;
    sr.score = round2(total);
  }
  return res;
}

/* === EVALUATION (BACKGROUND JOB) === */
const JOB = {
  running: false, stopping: false, phase: '', problem: '', student: '', test: '', done: 0, total: 0,
  logs: [], rv: 0, results: {}, error: '', envPathAdd: [], currentProbe: '', pyProbeDir: '', cmdCache: new Map(),
  preloadSo: '', runDirAny: '', studentNow: '', checkerArgv: null, checkerError: '',
  log(msg) { this.logs.push(clock() + ' ' + msg); if (this.logs.length > 20000) this.logs = this.logs.slice(-10000); },
  cancelled: () => JOB.stopping,
  publish(pname, res) { this.results[pname] = JSON.parse(JSON.stringify(res)); this.rv++; },
  runDir(C) { return path.join(C.workDir, 'run', C.name); }
};
function safeName(s) { return Array.from(String(s)).map(c => (/[\p{L}\p{N}_.-]/u.test(c) ? c : '_')).join('') || '_'; }
function loadResults(C) {
  const p = path.join(C.resultsDir, C.name, 'latest.json');
  try {
    const data = JSON.parse(readText(p).replace(/^\uFEFF/, ''));
    if (data && typeof data === 'object' && Array.isArray(data.students)) return data;
  } catch (e) { /* MISSING */ }
  return null;
}
function saveResults(C, res, snapshot) {
  const d = path.join(C.resultsDir, C.name);
  mkdirp(d);
  const text = JSON.stringify(res, null, 1);
  writeText(path.join(d, 'latest.json'), text);
  if (snapshot) writeText(path.join(d, `results-${stampStr()}.json`), text);
}
const r3 = x => Math.round(x * 1000) / 1000;
async function runTest(C, job, st, info, L, exe, t, baseline) {
  const tid = String(g(t, 'id', ''));
  const tr = { id: tid, raw: 'RE', msg0: '', detail: '', f: 0, pts: null, cpuMs: 0, wallMs: 0, timeMs: 0, memKb: 0, memRawKb: 0,
    stackKb: null, exit: 0, signal: 0, timedOut: false, stderr: '', out: '', err: '' };
  const inSrc = resolveP(C.testsDir, String(g(t, 'input', '')));
  const okSrc = resolveP(C.testsDir, String(g(t, 'ok', '')));
  if (!isFile(inSrc)) { tr.raw = 'CHKERR'; tr.msg0 = 'Test input file missing: ' + inSrc; return tr; }
  const rdir = job.runDir(C);
  mkdirp(rdir);
  let sroot;
  if (C.runIn === 'project') sroot = info.root;
  else { sroot = path.join(rdir, 'sandbox'); clearDir(sroot); }
  const cwd = resolveP(sroot, subst(C.ioCwd, { problem: C.name, nr: C.nr, n: tid }));
  mkdirp(cwd);
  let stdinPath = null;
  if (C.ioInput === 'stdin') stdinPath = inSrc;
  else {
    const dst = resolveP(sroot, subst(C.ioInpath, { problem: C.name, nr: C.nr, n: tid }));
    mkdirp(path.dirname(dst));
    copyFile(inSrc, dst);
  }
  const so = path.join(rdir, 'stdout.txt'), se = path.join(rdir, 'stderr.txt'), probe = path.join(rdir, 'probe.txt');
  removeFile(probe);
  let outPath;
  if (C.ioOutput === 'stdout') outPath = so;
  else { outPath = resolveP(sroot, subst(C.ioOutpath, { problem: C.name, nr: C.nr, n: tid })); removeFile(outPath); }
  const Lt = num(g(t, 'timeMs', 0)) || C.timeMs;
  const Lm = num(g(t, 'memMb', 0)) || C.memMb;
  const hardWall = Lt > 0 ? Lt * C.hardTime + 500 : 60000;
  const cpuS = Math.ceil(hardWall / 1000) + 1;
  const memHard = Lm > 0 ? Math.trunc(Lm * 1024 * C.hardMem + 65536) : 0;
  const stackKb = C.stackMb > 0 ? Math.trunc(C.stackMb * 1024) : 0;
  const fsizeKb = C.outputMb > 0 ? Math.trunc(C.outputMb * 1024) : 0;
  const argv = buildArgv(String(g(L, 'run', '{exe}')), [], [], exe, info.main, 0, C.name);
  const r = await runProcess(argv, { cwd, stdin: stdinPath, stdout: so, stderr: se, env: C.probeEnv(L, probe, job), wall: Math.trunc(hardWall),
    cpu: cpuS, mem: memHard, stack: stackKb, fsize: fsizeKb, single: true, cancel: job.cancelled });
  const pr = readProbe(probe);
  let memRaw;
  if (pr.rss_kb !== undefined) memRaw = Math.trunc(pr.rss_kb);
  else if (C.probeActive(L, job) && !IS_WIN) memRaw = null;
  else memRaw = Math.trunc(r.peakKb);
  const base = C.calibrate ? baseline : 0;
  tr.cpuMs = r3(r.cpuMs > 0 ? r.cpuMs : num(pr.cpu_ms));
  tr.wallMs = r3(r.wallMs);
  tr.timeMs = C.timeMode === 'wall' ? tr.wallMs : tr.cpuMs;
  tr.memRawKb = memRaw;
  tr.memKb = memRaw !== null ? Math.max(0, memRaw - base) : null;
  tr.stackKb = pr.stack_kb !== undefined ? Math.trunc(pr.stack_kb) : null;
  tr.exit = r.exit; tr.signal = r.signal; tr.timedOut = r.timedOut;
  try { tr.stderr = isFile(se) ? readText(se, 2000) : ''; } catch (e) { tr.stderr = ''; }
  const lowErr = tr.stderr.toLowerCase();
  if (C.keepOutputs) {
    const kd = path.join(C.workDir, 'outputs', C.name, st.id);
    mkdirp(kd);
    const ko = path.join(kd, safeName(tid) + '.out'), ke = path.join(kd, safeName(tid) + '.err');
    removeFile(ko); removeFile(ke);
    try {
      if (isFile(outPath)) { copyFile(outPath, ko); tr.out = ko; }
      if (fileSize(se) > 0) { copyFile(se, ke); tr.err = ke; }
    } catch (e) { /* IGNORE */ }
  }
  if (r.cancelled) { tr.raw = 'SKIP'; tr.msg0 = 'Stopped by the user'; return tr; }
  if (!r.started) { tr.raw = 'RE'; tr.msg0 = 'The program could not be started: ' + r.error; return tr; }
  if (r.timedOut) {
    tr.raw = 'TLE';
    tr.msg0 = `Time limit exceeded: stopped after ${fmtInt(r.wallMs)} ms`;
    if (r.cpuKnown && r.cpuMs < 0.3 * r.wallMs) tr.msg0 += ' (the program seems to be waiting, e.g. reading from the keyboard)';
    return tr;
  }
  const crashed = !r.exited || (r.exit !== 0 && C.nonzeroError);
  if (crashed) {
    const memFail = lowErr.includes('bad_alloc') || lowErr.includes('memoryerror') || lowErr.includes('out of memory') || r.memLimitHit ||
      r.winStatus === 0xC0000017 || r.winStatus === 0xC000012D || (memHard > 0 && r.peakKb >= 0.95 * memHard);
    if (memFail) { tr.raw = 'MLE'; tr.msg0 = 'Memory limit exceeded (allocation failed)'; }
    else { tr.raw = 'RE'; tr.msg0 = 'Runtime error: ' + statusText(r); }
    return tr;
  }
  if (!isFile(outPath)) { tr.raw = 'NOOUT'; tr.msg0 = 'Output file missing: ' + path.basename(outPath); return tr; }
  if (C.outputMb > 0 && fileSize(outPath) > C.outputMb * 1024 * 1024) { tr.raw = 'WA'; tr.msg0 = `Output too large (over ${fmtG(C.outputMb)} MB)`; return tr; }
  if (C.cmpMode === 'checker') {
    if (job.checkerArgv === null) { tr.raw = 'CHKERR'; tr.msg0 = 'Checker error: ' + job.checkerError; return tr; }
    let [f, pts, msg, err] = await runChecker(C, job, job.checkerArgv, inSrc, okSrc, outPath);
    if (err) { tr.raw = 'CHKERR'; tr.msg0 = 'Checker error: ' + err; return tr; }
    tr.pts = pts;
    if (pts !== null) { const P = num(g(t, 'points', 0)); f = P > 0 ? Math.max(0, Math.min(pts, P)) / P : (pts > 0 ? 1 : 0); }
    tr.f = f;
    tr.raw = f >= 1 ? 'OK' : (f > 0 ? 'PARTIAL' : 'WA');
    tr.msg0 = (f >= 1 ? 'Correct' : (f > 0 ? 'Partial' : 'Incorrect')) + (msg ? ': ' + msg : '');
    return tr;
  }
  if (!isFile(okSrc)) { tr.raw = 'CHKERR'; tr.msg0 = 'Test answer file missing: ' + okSrc; return tr; }
  let f, detail;
  try { [f, detail] = compareOutput(C, readBytes(outPath), readBytes(okSrc)); }
  catch (e) { tr.raw = 'CHKERR'; tr.msg0 = 'Cannot read the output: ' + e.message; return tr; }
  tr.f = f; tr.raw = f >= 1 ? 'OK' : 'WA'; tr.msg0 = f >= 1 ? 'Correct' : 'Incorrect'; tr.detail = detail;
  return tr;
}

async function evaluateProblem(job, ws, prob, onlyStudents, onlyTests) {
  const C = new Cfg(ws, prob);
  Object.assign(job, { problem: C.name, phase: 'preparing', student: '', test: '' });
  const tests = (g(prob, 'tests', []) || []).filter(t => !onlyTests || onlyTests.includes(String(g(t, 'id', ''))));
  job.log(`=== Problem ${C.name} ===`);
  if (!tests.length) { job.log('The scoring scheme has no tests. Use \"Detect tests\" in the Tests and scoring tab.'); return; }
  let students = discoverStudents(C);
  if (onlyStudents) students = students.filter(s => onlyStudents.includes(s.id));
  job.total += students.length * (tests.length + 1);
  const res = loadResults(C) || {};
  Object.assign(res, { format: 'olympiad-evaluator/results', version: 1, problem: C.name, variant: VARIANT, os: OS_NAME, updated: nowStr() });
  if (!Array.isArray(res.students)) res.students = [];
  res.baseline = {};
  for (const d of [C.workDir, job.runDir(C)]) mkdirp(d);
  job.currentProbe = path.join(C.workDir, 'olieval_probe.c');
  job.runDirAny = job.runDir(C);
  job.cmdCache = new Map();
  job.pyProbeDir = path.join(C.workDir, 'probe-py');
  try {
    writeText(job.currentProbe, PROBE_SOURCE);
    writeText(path.join(job.pyProbeDir, 'sitecustomize.py'), PY_PROBE_SOURCE);
  } catch (e) { job.log('Cannot write the probe: ' + e.message); }
  job.preloadSo = '';
  if (C.buildMode === 'scripts' && OS_NAME === 'linux' && C.useProbe) {
    const so = path.join(C.workDir, 'probe', 'olieval_probe.so');
    mkdirp(path.dirname(so));
    const Lc = C.langFor('x.c');
    const cc = (Lc ? langCompiler(Lc, C)[0] : null) || 'gcc';
    const lp = path.join(job.runDir(C), '_probe.txt');
    const r = await runProcess([cc, '-O2', '-shared', '-fPIC', '-o', so, job.currentProbe], { stdout: lp, stderr: lp, wall: 60000 });
    if (r.started && r.exited && r.exit === 0 && isFile(so)) job.preloadSo = so;
    else job.log('The LD_PRELOAD probe could not be compiled; memory is measured from outside.');
  }
  job.envPathAdd = [];
  if (IS_WIN) {
    const dirs = [];
    for (const L of C.languages) {
      const toks = splitCmd(String(g(L, 'compile', '')));
      if (toks.length) { const fp = findProgram(toks[0]); if (fp && !dirs.includes(path.dirname(fp))) dirs.push(path.dirname(fp)); }
    }
    if (dirs.length) job.envPathAdd = ['PATH=' + dirs.join(path.delimiter) + path.delimiter + (process.env.PATH || '')];
  }
  job.checkerArgv = null; job.checkerError = '';
  if (C.cmpMode === 'checker') {
    [job.checkerArgv, job.checkerError] = await prepareChecker(C, job);
    job.log(job.checkerArgv === null ? 'Checker: ' + job.checkerError : 'Checker: ' + cmdString(job.checkerArgv));
  }
  rescore(ws, prob, res);
  job.publish(C.name, res);
  for (const st of students) {
    if (job.stopping) break;
    Object.assign(job, { student: st.id, test: '', phase: 'compiling' });
    job.log('Student ' + st.id);
    const startDone = job.done;
    const sr = { id: st.id, name: st.name || '', dir: st.dir, root: '', main: '', lang: '', sources: [], sizeMain: 0, sizeAll: 0, sizeJudged: 0,
      sizeOk: true, compile: { ok: false, ms: 0, log: '', cmd: '' }, status: '', score: 0, max: 0, notes: [], evaluatedAt: nowStr(), tests: [] };
    try {
      if (!st.exists) { sr.status = 'NOSRC'; sr.notes.push('No source (the student folder or file does not exist)'); job.log('  no source'); }
      else {
        const info = detectProject(C, st);
        Object.assign(sr, { root: info.root, main: info.mainRel, lang: info.lang, sources: info.sources, sizeMain: info.sizeMain, sizeAll: info.sizeAll, sizeJudged: info.sizeJudged });
        sr.notes.push(...info.warnings);
        const L = info.langObj;
        if (!info.main || !L) { sr.status = 'NOSRC'; job.log('  source not found'); }
        else {
          const srcs = C.buildMode === 'objects'
            ? walkBfs(info.root, C.maxDepth, C.ignore).filter(e => !e[2] && OBJ_EXTS.includes(extOf(e[0]))).map(e => e[1])
            : info.sources.map(s => joinRel(info.root, s));
          const libs = await autoLibFlags(C, info.root, srcs, job);
          const bdir = path.join(C.workDir, 'build', C.name, safeName(st.id));
          job.studentNow = st.id;
          const [ok, exe, log, ms, cmd] = await buildStudent(C, L, info, bdir, job, libs);
          sr.compile = { ok, ms: Math.round(ms * 10) / 10, log, cmd };
          job.done++;
          if (!ok) { sr.status = 'CE'; job.log('  compilation error'); }
          else {
            const lid = String(g(L, 'id', ''));
            if (C.calibrate && res.baseline[lid] === undefined) res.baseline[lid] = await calibrate(C, L, job);
            const baseline = C.calibrate ? Math.trunc(res.baseline[lid] || 0) : 0;
            job.phase = 'running';
            for (const t of tests) {
              if (job.stopping) break;
              job.test = String(g(t, 'id', ''));
              const tr = await runTest(C, job, st, info, L, exe, t, baseline);
              sr.tests.push(tr);
              job.done++;
              job.log(`  test ${tr.id}: ${tr.raw} ${tr.msg0}`);
            }
            sr.status = 'DONE';
          }
        }
      }
    } catch (e) {
      sr.status = 'ERR';
      sr.notes.push('Internal error: ' + e.message);
      job.log('  ERROR: ' + (e.stack || e));
    }
    job.done = Math.min(job.total, startDone + tests.length + 1);
    const prev = res.students.findIndex(x => x.id === sr.id);
    if (prev >= 0 && onlyTests && sr.status === 'DONE' && res.students[prev].status === 'DONE') {
      const old = new Map((res.students[prev].tests || []).map(x => [String(x.id), x]));
      for (const tr of sr.tests) old.set(tr.id, tr);
      sr.tests = Array.from(old.values());
    }
    if (prev < 0) res.students.push(sr); else res.students[prev] = sr;
    res.students.sort((a, b) => natcmp(String(a.id || ''), String(b.id || '')));
    res.updated = nowStr();
    rescore(ws, prob, res);
    try { saveResults(C, res, false); } catch (e) { job.log('Cannot save the results: ' + e.message); }
    job.publish(C.name, res);
    await new Promise(r => setImmediate(r));
  }
  try { saveResults(C, res, true); } catch (e) { job.log('Cannot save the results: ' + e.message); }
  job.publish(C.name, res);
}
async function jobMain(ws, probs, onlyStudents, onlyTests) {
  try {
    for (const pname of probs) {
      if (JOB.stopping) break;
      const prob = findProblem(ws, pname);
      if (!prob) { JOB.log(`Problem ${pname} does not exist`); continue; }
      await evaluateProblem(JOB, ws, prob, onlyStudents, onlyTests);
    }
    JOB.log(JOB.stopping ? 'Evaluation stopped.' : 'Evaluation finished.');
  } catch (e) {
    JOB.log('ERROR: ' + (e.stack || e));
    JOB.error = 'Internal error';
  } finally {
    Object.assign(JOB, { running: false, phase: JOB.stopping ? 'stopped' : 'done', student: '', test: '' });
    JOB.done = JOB.total;
    JOB.rv++;
  }
}

/* === FOLDER STRUCTURE === */
function structure(ws, create) {
  const root = path.resolve(g(ws, 'root', '') || '.');
  const items = [];
  const add = (p, kind) => { if (!items.some(x => x.path === p)) items.push({ path: p, kind, exists: isDir(p) }); };
  const probs = g(ws, 'problems', []) || [];
  const C0 = new Cfg(ws, probs[0] || { name: '_' });
  add(C0.workDir, 'work');
  add(C0.resultsDir, 'results');
  const studs = (g(ws, 'students', []) || []).map(s => String(g(s, 'id', '')).trim()).filter(Boolean);
  for (const p of probs) {
    const C = new Cfg(ws, p);
    if (C.layout === 'flat') add(resolveP(root, subst(g(ws, 'flatDir', '{root}/{problem}/sources'), C.v)), 'sources');
    else for (const s of studs) add(resolveP(root, subst(g(ws, 'studentDir', '{root}/{problem}/{student}'), Object.assign({ student: s }, C.v))), 'student');
    add(C.testsDir, 'tests');
  }
  let created = 0;
  const errors = [];
  if (create) for (const it of items) if (!it.exists) { try { mkdirp(it.path); it.exists = true; created++; } catch (e) { errors.push(`${it.path}: ${e.message}`); } }
  return { items, created, errors };
}

/* === HTTP SERVER === */
const TOKEN = crypto.randomBytes(16).toString('hex');
let PORT = 8765;
function loadFileNear(name, sub) {
  for (const p of [path.join(HERE, name), path.join(HERE, 'public', name), path.join(HERE, '..', '..', 'common', sub, name)]) {
    if (isFile(p)) return readText(p);
  }
  return null;
}
const GUI_HTML = loadFileNear('index.html', 'gui') || '<h1>index.html is missing</h1>';
const PROBE_SOURCE = loadFileNear('olieval_probe.c', 'probe') || '';
const PY_PROBE_SOURCE = loadFileNear('sitecustomize.py', 'probe') || '';
function settingsPath() { return path.join(os.homedir(), '.olympiad-evaluator', 'settings.json'); }
function findExample() {
  let d = path.resolve(HERE);
  for (let k = 0; k < 5; k++) {
    const p = path.join(d, 'tutorial', 'examples', 'contest');
    if (isFile(path.join(p, 'evaluator.json'))) return p;
    d = path.dirname(d);
  }
  return '';
}
function apiTranslations() {
  const d = path.join(HERE, 'translations'), languages = {}, errors = [];
  let names = [];
  try { names = fs.readdirSync(d).filter(n => n.toLowerCase().endsWith('.json')).sort(); } catch (e) { names = []; }
  for (const n of names) {
    try { languages[n.slice(0, -5)] = JSON.parse(readText(path.join(d, n)).replace(/^\ufeff/, '')); } catch (e) { errors.push(n + ': ' + e.message); }
  }
  return { dir: d, languages, errors };
}
function apiInfo() {
  const py = IS_WIN ? 'python' : 'python3';
  return { app: APP_NAME, version: VERSION, variant: VARIANT, os: OS_NAME, sep: path.sep, home: os.homedir(), settings: settingsPath(), cwd: process.cwd(),
    python: py, pid: process.pid, compilers: { gcc: findProgram('gcc') || '', 'g++': findProgram('g++') || '', python: findProgram(py) || '' },
    features: { runner: HELPER.note, probe: !!PROBE_SOURCE, memLimit: !!HELPER.path || !IS_WIN, cpuTime: !!HELPER.path }, example: findExample() };
}
function winRoots() { const out = []; for (const c of 'ABCDEFGHIJKLMNOPQRSTUVWXYZ') { const d = c + ':\\'; if (exists(d)) out.push(d); } return out; }
function apiFsList(body) {
  let p = String(body.path || '') || os.homedir();
  if (p.startsWith('~')) p = path.join(os.homedir(), p.slice(1));
  p = path.resolve(p);
  if (!isDir(p)) throw new UserError('Folder not found: ' + p);
  const entries = [];
  for (const name of fs.readdirSync(p)) {
    const fp = path.join(p, name);
    try { const st = fs.statSync(fp); entries.push({ name, dir: st.isDirectory(), size: st.isDirectory() ? 0 : st.size, mtime: Math.trunc(st.mtimeMs / 1000) }); } catch (e) { /* UNREADABLE */ }
  }
  entries.sort((a, b) => (a.dir === b.dir ? natcmp(a.name, b.name) : (a.dir ? -1 : 1)));
  const parent = path.dirname(p);
  return { path: p, parent: parent !== p ? parent : '', sep: path.sep, entries, roots: IS_WIN ? winRoots() : ['/'] };
}
function apiFsRead(body) {
  const p = String(body.path || '');
  const mx = Math.trunc(num(body.max, 2000000));
  if (!isFile(p)) throw new UserError('File not found: ' + p);
  const size = fs.statSync(p).size;
  return { path: path.resolve(p), size, text: readBytes(p, Math.min(mx, size)).toString('utf8'), truncated: size > mx };
}
function apiFsWrite(body) { const p = String(body.path || ''); if (!p) throw new UserError('Missing path'); writeText(path.resolve(p), String(body.text || '')); return { ok: true }; }
function apiFsStat(body) {
  try { const st = fs.statSync(String(body.path || '')); return { exists: true, dir: st.isDirectory(), size: st.size, mtime: Math.trunc(st.mtimeMs / 1000) }; }
  catch (e) { return { exists: false, dir: false, size: 0, mtime: 0 }; }
}
function apiFsMkdir(body) { const p = String(body.path || ''); if (!p) throw new UserError('Missing path'); mkdirp(p); return { ok: true }; }
function apiOpen(body) {
  const p = String(body.path || '');
  if (!exists(p)) throw new UserError('Not found: ' + p);
  const [cmd, args] = IS_WIN ? ['explorer.exe', [p]] : (process.platform === 'darwin' ? ['open', [p]] : ['xdg-open', [p]]);
  try { spawn(cmd, args, { detached: true, stdio: 'ignore', windowsHide: false }).on('error', () => {}).unref(); } catch (e) { /* IGNORE */ }
  return { ok: true };
}
function apiScan(body) {
  const ws = body.workspace || {};
  const prob = findProblem(ws, body.problem || '');
  if (!prob) throw new UserError('Problem not found: ' + (body.problem || ''));
  const C = new Cfg(ws, prob);
  const res = scanTests(C);
  res.students = discoverStudents(C).map(st => {
    const info = st.exists ? detectProject(C, st) : { root: '', mainRel: '', lang: '', sources: [], sizeMain: 0, sizeAll: 0, sizeJudged: 0, warnings: ['The student folder/file does not exist'] };
    return publicStudent(C, st, info);
  });
  return res;
}
function apiEvalStart(body) {
  if (JOB.running) throw new UserError('An evaluation is already running');
  const ws = body.workspace || {};
  let probs = body.problems || [];
  if (typeof probs === 'string') probs = [probs];
  Object.assign(JOB, { running: true, stopping: false, phase: 'starting', done: 0, total: 0, logs: [], results: {}, error: '' });
  JOB.rv++;
  const onlyS = Array.isArray(body.students) && body.students.length ? body.students.map(String) : null;
  const onlyT = Array.isArray(body.tests) && body.tests.length ? body.tests.map(String) : null;
  setImmediate(() => jobMain(ws, probs.map(String), onlyS, onlyT));
  return { ok: true };
}
function apiEvalStatus(q) {
  const since = Math.trunc(num(q.get('log'), 0)), rv = Math.trunc(num(q.get('rv'), -1));
  const out = { running: JOB.running, stopping: JOB.stopping, phase: JOB.phase, problem: JOB.problem, student: JOB.student, test: JOB.test,
    done: JOB.done, total: JOB.total, log: JOB.logs.slice(since), logNext: JOB.logs.length, rv: JOB.rv, error: JOB.error };
  if (rv !== JOB.rv) out.results = JOB.results;
  return out;
}
function apiRescore(body) {
  const ws = body.workspace || {};
  const prob = findProblem(ws, body.problem || '');
  if (!prob) throw new UserError('Problem not found');
  return { results: rescore(ws, prob, body.results || {}) };
}
class UserError extends Error {}

function send(res, code, body, ctype) {
  const buf = Buffer.isBuffer(body) ? body : Buffer.from(String(body), 'utf8');
  res.writeHead(code, { 'Content-Type': ctype || 'application/json; charset=utf-8', 'Content-Length': buf.length, 'Cache-Control': 'no-store', Connection: 'close' });
  res.end(buf);
}
const sendJson = (res, code, obj) => send(res, code, JSON.stringify(obj));
function hostOk(req) { const h = String(req.headers.host || '').trim(); return h === `127.0.0.1:${PORT}` || h === `localhost:${PORT}`; }
function authOk(req) { return hostOk(req) && req.headers['x-eval-token'] === TOKEN; }
const ROUTES = {
  '/api/fs/list': apiFsList, '/api/fs/read': apiFsRead, '/api/fs/write': apiFsWrite, '/api/fs/stat': apiFsStat, '/api/fs/mkdir': apiFsMkdir,
  '/api/open': apiOpen, '/api/structure': b => structure(b.workspace || {}, !!b.create), '/api/scan': apiScan,
  '/api/eval/start': apiEvalStart, '/api/rescore': apiRescore
};
function handle(req, res) {
  const u = new URL(req.url, 'http://127.0.0.1');
  if (!hostOk(req)) return send(res, 403, 'Access denied', 'text/plain; charset=utf-8');
  if (req.method === 'GET') {
    if (u.pathname === '/' || u.pathname === '/index.html') return send(res, 200, GUI_HTML.split('__EVAL_TOKEN__').join(TOKEN), 'text/html; charset=utf-8');
    if (u.pathname === '/api/info' && authOk(req)) return sendJson(res, 200, apiInfo());
    if (u.pathname === '/api/eval/status' && authOk(req)) return sendJson(res, 200, apiEvalStatus(u.searchParams));
    if (u.pathname === '/api/translations' && authOk(req)) return sendJson(res, 200, apiTranslations());
    if (u.pathname.startsWith('/api/')) return sendJson(res, 403, { error: 'Access denied' });
    return send(res, 404, 'Not found', 'text/plain; charset=utf-8');
  }
  if (req.method !== 'POST') return send(res, 405, 'Method not allowed', 'text/plain; charset=utf-8');
  if (!authOk(req)) return sendJson(res, 403, { error: 'Access denied' });
  const chunks = [];
  let size = 0;
  req.on('data', c => { size += c.length; if (size > 200 * 1024 * 1024) req.destroy(); else chunks.push(c); });
  req.on('end', () => {
    let body;
    try { body = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); if (!body || typeof body !== 'object' || Array.isArray(body)) body = {}; }
    catch (e) { return sendJson(res, 400, { error: 'Invalid JSON' }); }
    if (u.pathname === '/api/eval/stop') { JOB.stopping = true; JOB.log('Stopping the evaluation...'); return sendJson(res, 200, { ok: true }); }
    if (u.pathname === '/api/quit') { sendJson(res, 200, { ok: true }); setTimeout(() => process.exit(0), 300); return; }
    const fn = ROUTES[u.pathname];
    if (!fn) return sendJson(res, 404, { error: 'Unknown: ' + u.pathname });
    try { return sendJson(res, 200, fn(body)); }
    catch (e) {
      if (e instanceof UserError || (e && e.code)) return sendJson(res, 400, { error: e.message });
      console.error(e);
      return sendJson(res, 500, { error: 'Internal error: ' + e.message });
    }
  });
}

function openBrowser(url) {
  const cands = [];
  if (IS_WIN) {
    for (const b of [process.env.ProgramFiles, process.env['ProgramFiles(x86)'], process.env.LOCALAPPDATA]) {
      if (!b) continue;
      cands.push(path.join(b, 'Microsoft', 'Edge', 'Application', 'msedge.exe'), path.join(b, 'Google', 'Chrome', 'Application', 'chrome.exe'));
    }
  } else if (process.platform === 'darwin') {
    cands.push('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge');
  } else {
    for (const n of ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser', 'microsoft-edge']) { const p = findProgram(n); if (p) cands.push(p); }
  }
  for (const c of cands) {
    if (c && isFile(c)) {
      try { spawn(c, ['--app=' + url, '--window-size=1400,900'], { detached: true, stdio: 'ignore' }).on('error', () => {}).unref(); return; } catch (e) { /* TRY NEXT */ }
    }
  }
  const [cmd, args] = IS_WIN ? ['cmd', ['/c', 'start', '', url]] : (process.platform === 'darwin' ? ['open', [url]] : ['xdg-open', [url]]);
  try { spawn(cmd, args, { detached: true, stdio: 'ignore' }).on('error', () => {}).unref(); } catch (e) { /* IGNORE */ }
}

function main() {
  const args = process.argv.slice(2);
  let port = 8765, browser = true;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--port' && i + 1 < args.length) port = Number(args[++i]);
    else if (args[i] === '--no-browser') browser = false;
    else if (args[i] === '--no-helper') { HELPER.checked = true; HELPER.note = 'fallback (no memory limit; CPU time only through the probe)'; }
  }
  ensureHelper();
  const server = http.createServer(handle);
  let tries = 0;
  const tryListen = p => {
    server.once('error', e => {
      if (e.code === 'EADDRINUSE' && tries++ < 30) tryListen(p + 1);
      else { console.log('No free port found: ' + e.message); process.exit(1); }
    });
    server.listen(p, '127.0.0.1', () => {
      PORT = p;
      const url = `http://127.0.0.1:${PORT}/`;
      console.log(`${APP_NAME} ${VERSION} — variant ${VARIANT} (Node.js ${process.version})`);
      console.log(`Program runner: ${HELPER.note}${HELPER.path ? ' — ' + HELPER.path : ''}`);
      console.log(`Interface: ${url}`);
      console.log('Close this window (or press Ctrl+C) to stop the application.');
      if (browser) setTimeout(() => openBrowser(url), 300);
    });
  };
  tryListen(port);
}
main();

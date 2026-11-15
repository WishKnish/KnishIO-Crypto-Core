// wasm_check.mjs <kcore.wasm> [<differential.json>]
//
// Instantiates the wasm32-wasip1 reactor the way a JS binding does and checks: the export set (the
// eleven kcore_* functions plus malloc/free), kcore_abi_version() == 1, SHAKE256("") -> 32 bytes
// against the canonical vector, and ML-KEM-1024 and ML-KEM-768 keypair/encaps/decaps round trips.
// With the selftest vectors (tests/gen_vectors.py output) it also runs every canonical.mlkem case
// (the public key from the case's seed must equal the byte-frozen cross-SDK key) and every acvp case
// (NIST ACVP sample vectors), with the same expectations as tests/kcore_selftest.c.
// Prints "WASMCHECK ok" (exit 0) or "WASMCHECK FAIL <step>" (exit 1).
import { readFileSync } from 'node:fs'
import { WASI } from 'node:wasi'

const NAMES = [
  'kcore_abi_version', 'kcore_chains_hex', 'kcore_mlkem1024_decaps', 'kcore_mlkem1024_encaps',
  'kcore_mlkem1024_keypair', 'kcore_mlkem768_decaps', 'kcore_mlkem768_encaps', 'kcore_mlkem768_keypair',
  'kcore_shake256', 'kcore_wots_address', 'kcore_wots_address_v2', 'malloc', 'free'
]
// Entry points and key/ciphertext lengths per parameter set; a case without "set" is ML-KEM-1024.
const SETS = {
  1024: { pk: 1568, sk: 3168, ct: 1568, keypair: 'kcore_mlkem1024_keypair', encaps: 'kcore_mlkem1024_encaps', decaps: 'kcore_mlkem1024_decaps' },
  768: { pk: 1184, sk: 2400, ct: 1088, keypair: 'kcore_mlkem768_keypair', encaps: 'kcore_mlkem768_encaps', decaps: 'kcore_mlkem768_decaps' }
}
const EMPTY_32 = '46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f'

function fail (step) {
  console.log(`WASMCHECK FAIL ${step}`)
  process.exit(1)
}

if (process.argv.length !== 3 && process.argv.length !== 4) {
  console.error('usage: node wasm_check.mjs <kcore.wasm> [<differential.json>]')
  process.exit(2)
}

let instance
try {
  const wasi = new WASI({ version: 'preview1' })
  const module = await WebAssembly.compile(readFileSync(process.argv[2]))
  instance = await WebAssembly.instantiate(module, wasi.getImportObject())
  wasi.initialize(instance)
} catch (e) {
  console.error(e)
  fail('instantiate')
}
const ex = instance.exports

for (const name of NAMES) {
  if (typeof ex[name] !== 'function') fail(`export ${name}`)
}
if (ex.kcore_abi_version() !== 1) fail('abi')

const alloc = (n) => {
  const p = ex.malloc(Math.max(n, 1))
  if (!p) fail('malloc')
  return p
}
const read = (p, n) => Buffer.from(ex.memory.buffer.slice(p, p + n))

const out = alloc(32)
if (ex.kcore_shake256(0, 0, out, 32) !== 0) fail('shake256')
if (read(out, 32).toString('hex') !== EMPTY_32) fail('shake256-kat')

const seed = alloc(64)
const coins = alloc(32)
new Uint8Array(ex.memory.buffer, seed, 64).fill(0)
new Uint8Array(ex.memory.buffer, coins, 32).fill(0)
const pk = alloc(1568)
const sk = alloc(3168)
const ct = alloc(1568)
const ss = alloc(32)
const dss = alloc(32)
// The 1024 buffers are large enough for ML-KEM-768 too.
for (const [name, s] of Object.entries(SETS)) {
  if (ex[s.keypair](seed, pk, sk) !== 0) fail(`keypair${name}`)
  if (ex[s.encaps](pk, coins, ct, ss) !== 0) fail(`encaps${name}`)
  if (ex[s.decaps](ct, sk, dss) !== 0) fail(`decaps${name}`)
  if (!read(ss, 32).equals(read(dss, 32))) fail(`roundtrip${name}`)
}

const write = (p, hex) => new Uint8Array(ex.memory.buffer, p, hex.length / 2).set(Buffer.from(hex, 'hex'))
const fill = (p, n, byte) => new Uint8Array(ex.memory.buffer, p, n).fill(byte)
const hexAt = (p, n) => read(p, n).toString('hex')

// Key-check cases: 0 for a valid key; for an invalid key -1 with the 0xA5-filled outputs untouched.
function refusal (c, rc, outputs) {
  if (c.valid === 1) return rc === 0
  if (rc !== -1) return false
  return outputs.every(([p, n]) => read(p, 1)[0] === 0xa5 && read(p + n - 1, 1)[0] === 0xa5)
}

if (process.argv[3]) {
  const doc = JSON.parse(readFileSync(process.argv[3], 'utf8'))
  const cases = doc.canonical.mlkem
  if (!cases || cases.length === 0) fail('canonical.mlkem missing')
  for (const c of cases) {
    const s = SETS[c.set || 1024]
    if (!s) fail(`${c.id} set`)
    write(seed, c.seed)
    if (ex[s.keypair](seed, pk, sk) !== 0) fail(`${c.id} keypair`)
    if (hexAt(pk, s.pk) !== c.expect) fail(`${c.id} pubkey`)
  }

  const acvp = doc.acvp
  if (!acvp) fail('acvp missing')
  for (const c of acvp.keygen) {
    const s = SETS[c.set || 1024]
    write(seed, c.seed)
    if (ex[s.keypair](seed, pk, sk) !== 0) fail(`${c.id} keypair`)
    if (hexAt(pk, s.pk) !== c.expect) fail(`${c.id} pubkey`)
    if (hexAt(sk, s.sk) !== c.expect_sk) fail(`${c.id} seckey`)
  }
  for (const c of acvp.encaps) {
    const s = SETS[c.set || 1024]
    write(pk, c.ek)
    write(coins, c.coins)
    if (ex[s.encaps](pk, coins, ct, ss) !== 0) fail(`${c.id} encaps`)
    if (hexAt(ct, s.ct) !== c.ct || hexAt(ss, 32) !== c.ss) fail(`${c.id} output`)
  }
  for (const c of acvp.decaps) {
    const s = SETS[c.set || 1024]
    write(sk, c.dk)
    write(ct, c.ct)
    if (ex[s.decaps](ct, sk, ss) !== 0) fail(`${c.id} decaps`)
    if (hexAt(ss, 32) !== c.ss) fail(`${c.id} output`)
  }
  for (const c of acvp.ekcheck) {
    const s = SETS[c.set || 1024]
    write(pk, c.ek)
    fill(coins, 32, 0)
    fill(ct, s.ct, 0xa5)
    fill(ss, 32, 0xa5)
    if (!refusal(c, ex[s.encaps](pk, coins, ct, ss), [[ct, s.ct], [ss, 32]])) fail(`${c.id} check`)
  }
  for (const c of acvp.dkcheck) {
    const s = SETS[c.set || 1024]
    write(sk, c.dk)
    fill(ct, s.ct, 0)
    fill(ss, 32, 0xa5)
    if (!refusal(c, ex[s.decaps](ct, sk, ss), [[ss, 32]])) fail(`${c.id} check`)
  }
  for (const c of acvp.shake) {
    const n = c.in.length / 2
    const pin = alloc(n)
    const pout = alloc(c.outlen)
    write(pin, c.in)
    if (ex.kcore_shake256(n === 0 ? 0 : pin, n, pout, c.outlen) !== 0) fail(`${c.id} shake256`)
    if (hexAt(pout, c.outlen) !== c.expect) fail(`${c.id} output`)
    ex.free(pin)
    ex.free(pout)
  }
}
for (const p of [out, seed, coins, pk, sk, ct, ss, dss]) ex.free(p)

console.log('WASMCHECK ok')

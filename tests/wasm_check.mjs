// wasm_check.mjs <kcore.wasm> [<differential.json>]
//
// Instantiates the wasm32-wasip1 reactor the way a JS binding does and checks: the export set (the
// ten kcore_* functions plus malloc/free), kcore_abi_version() == 1, SHAKE256("") -> 32 bytes
// against the canonical vector, and ML-KEM-1024 and ML-KEM-768 keypair/encaps/decaps round trips.
// With the selftest vectors (tests/gen_vectors.py output) it also runs every canonical.mlkem case:
// the public key from the case's seed must equal the byte-frozen cross-SDK key.
// Prints "WASMCHECK ok" (exit 0) or "WASMCHECK FAIL <step>" (exit 1).
import { readFileSync } from 'node:fs'
import { WASI } from 'node:wasi'

const NAMES = [
  'kcore_abi_version', 'kcore_chains_hex', 'kcore_mlkem1024_decaps', 'kcore_mlkem1024_encaps',
  'kcore_mlkem1024_keypair', 'kcore_mlkem768_decaps', 'kcore_mlkem768_encaps', 'kcore_mlkem768_keypair',
  'kcore_shake256', 'kcore_wots_address', 'malloc', 'free'
]
// Entry points and public-key length per parameter set; a canonical case without "set" is ML-KEM-1024.
const SETS = {
  1024: { pk: 1568, keypair: 'kcore_mlkem1024_keypair', encaps: 'kcore_mlkem1024_encaps', decaps: 'kcore_mlkem1024_decaps' },
  768: { pk: 1184, keypair: 'kcore_mlkem768_keypair', encaps: 'kcore_mlkem768_encaps', decaps: 'kcore_mlkem768_decaps' }
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

if (process.argv[3]) {
  const cases = JSON.parse(readFileSync(process.argv[3], 'utf8')).canonical.mlkem
  if (!cases || cases.length === 0) fail('canonical.mlkem missing')
  for (const c of cases) {
    const s = SETS[c.set || 1024]
    if (!s) fail(`${c.id} set`)
    new Uint8Array(ex.memory.buffer, seed, 64).set(Buffer.from(c.seed, 'hex'))
    if (ex[s.keypair](seed, pk, sk) !== 0) fail(`${c.id} keypair`)
    if (read(pk, s.pk).toString('hex') !== c.expect) fail(`${c.id} pubkey`)
  }
}
for (const p of [out, seed, coins, pk, sk, ct, ss, dss]) ex.free(p)

console.log('WASMCHECK ok')

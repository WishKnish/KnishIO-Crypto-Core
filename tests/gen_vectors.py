#!/usr/bin/env python3
"""kcore selftest vectors (stdlib only).

Writes <out-dir>/differential.json: seeded differential cases plus a `canonical` section built
from the cross-platform-test-vectors.json fixture, one case per line so that
tests/kcore_selftest.c can read it without a JSON library. Each case carries `expect`, computed here
with hashlib, for the selftest.

Usage: gen_vectors.py <cross-platform-test-vectors.json> <out-dir>
"""
import base64
import hashlib
import json
import os
import random
import sys

W3_COUNTS = [16, 0, 15, 1, 14, 2, 13, 3, 12, 4, 11, 5, 10, 6, 9, 7]
OUTLENS = [1, 32, 64, 136, 137, 1024]
FIXED_INLENS = [0, 1, 135, 136, 137, 271, 272, 273]


def shake_hex(data: bytes, n: int) -> str:
    return hashlib.shake_256(data).hexdigest(n)


def chains(chunks: str, counts):
    parts = [chunks[i * 128:(i + 1) * 128] for i in range(len(counts))]
    for i, c in enumerate(counts):
        for _ in range(c):
            parts[i] = shake_hex(parts[i].encode("ascii"), 64)
    return "".join(parts)


def address(key: str) -> str:
    walked = chains(key, [16] * 16)
    digest = shake_hex(walked.encode("ascii"), 1024)
    return shake_hex(digest.encode("ascii"), 32)


# Port of sdks/KnishIO-Client-JS/src/Molecule.js:212-290 (static enumerate, static normalize).
ENUM_MAP = {c: v for c, v in zip("0123456789abcdefg", range(-8, 9))}


def enumerate_hash(h: str):
    return [ENUM_MAP[s] for s in h.lower() if s in ENUM_MAP]


def normalize(mapped):
    mapped = list(mapped)
    total = sum(mapped)
    total_condition = total < 0
    while total < 0 or total > 0:
        for index in range(len(mapped)):
            condition = mapped[index] < 8 if total_condition else mapped[index] > -8
            if condition:
                if total_condition:
                    mapped[index] += 1
                    total += 1
                else:
                    mapped[index] -= 1
                    total -= 1
                if total == 0:
                    break
    return mapped


def as_list(value):
    if isinstance(value, dict):
        return [value[str(i)] for i in range(len(value))]
    return list(value)


def is_hex(s: str) -> bool:
    return len(s) > 0 and all(c in "0123456789abcdefABCDEF" for c in s)


# Port of sdks/KnishIO-Client-JS/src/Wallet.js:220-256 (static generateKey). The vector field
# `expectedPrivateKey` is not this key: generateAddress(expectedPrivateKey) != expectedAddress in
# the JS SDK as well, so the canonical W4 case derives the key the way every SDK's Wallet does.
def generate_key(secret: str, token: str, position: str) -> str:
    secret_hex = secret if is_hex(secret) else shake_hex(secret.encode("utf-8"), 128)
    position_hex = position if is_hex(position) else shake_hex(position.encode("utf-8"), 32)
    indexed = int(secret_hex, 16) + int(position_hex, 16)
    intermediate = shake_hex((format(indexed, "x") + (token or "")).encode("utf-8"), 1024)
    return shake_hex(intermediate.encode("ascii"), 1024)


def rand_hex(rng, nbytes):
    return bytes(rng.randrange(256) for _ in range(nbytes)).hex()


def main():
    if len(sys.argv) != 3:
        print("usage: gen_vectors.py <cross-platform-test-vectors.json> <out-dir>", file=sys.stderr)
        return 2
    vectors = json.load(open(sys.argv[1], encoding="utf-8"))["vectors"]
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    for t in vectors["hash_normalization"]["tests"]:
        enum = enumerate_hash(t["hash"])
        norm = normalize(enum)
        if enum != as_list(t["expectedEnumerated"]) or norm != as_list(t["expectedNormalized"]):
            print("FAIL hash_normalization %s: port of enumerate/normalize disagrees" % t["name"], file=sys.stderr)
            return 2

    rng = random.Random(20260926)
    shake_cases = []
    for i in range(2000):
        data = bytes(rng.randrange(256) for _ in range(rng.randint(0, 300)))
        n = OUTLENS[i % len(OUTLENS)]
        shake_cases.append({"id": "s%d" % i, "in": data.hex(), "outlen": n, "expect": shake_hex(data, n)})
    for j, length in enumerate(FIXED_INLENS):
        data = bytes(rng.randrange(256) for _ in range(length))
        shake_cases.append({"id": "s%d" % (2000 + j), "in": data.hex(), "outlen": 64, "expect": shake_hex(data, 64)})

    chain_cases = []
    count_sets = [[rng.randint(0, 16) for _ in range(16)] for _ in range(300)]
    count_sets += [[0] * 16, [16] * 16, list(W3_COUNTS)]
    for i, counts in enumerate(count_sets):
        chunks = "".join(rand_hex(rng, 64) for _ in range(16))
        chain_cases.append({"id": "c%d" % i, "chunks": chunks, "counts": counts, "expect": chains(chunks, counts)})

    address_cases = []
    for i in range(100):
        key = rand_hex(rng, 1024)
        address_cases.append({"id": "a%d" % i, "key": key, "expect": address(key)})

    mlkem_cases = [{"id": "k%d" % i, "seed": rand_hex(rng, 64), "coins": rand_hex(rng, 32)} for i in range(200)]
    # Drawn after every earlier case, so adding them left the existing seeded cases unchanged.
    mlkem_cases += [{"id": "k768_%d" % i, "set": 768, "seed": rand_hex(rng, 64), "coins": rand_hex(rng, 32)}
                    for i in range(100)]

    canon_shake = []
    for i, t in enumerate(vectors["shake256"]["tests"]):
        data = t["input"].encode("utf-8")
        n = int(t["outputLength"])
        canon_shake.append({"id": "vs%d" % i, "name": t["name"], "in": data.hex(), "outlen": n, "expect": t["expected"]})
    canon_address = []
    for i, t in enumerate(vectors["wallet_generation"]["tests"]):
        key = generate_key(t["secret"], t["token"], t["position"])
        if address(key) != t["expectedAddress"]:
            print("FAIL wallet_generation %s: port of generateKey/generateAddress disagrees" % t["name"], file=sys.stderr)
            return 2
        canon_address.append({"id": "va%d" % i, "name": t["name"], "key": key, "expect": t["expectedAddress"]})
    # In the vector file, the SDK signing loop (Molecule.js:1120, counts 8 - normalized[i]) yields
    # `expectedCompressedSignature`; `expectedSignatureFragments` is the 8 + normalized[i] walk
    # (the verifier's direction). Both are canonical chain cases.
    canon_chains = []
    for i, t in enumerate(vectors["wots_signature"]["tests"]):
        norm = normalize(enumerate_hash(t["molecularHash"]))
        assert len(t["privateKey"]) == 2048, t["name"]
        canon_chains.append({"id": "vws%d" % i, "name": t["name"] + ".sign", "chunks": t["privateKey"],
                             "counts": [8 - norm[k] for k in range(16)], "expect": t["expectedCompressedSignature"]})
        canon_chains.append({"id": "vwf%d" % i, "name": t["name"] + ".fragments", "chunks": t["privateKey"],
                             "counts": [8 + norm[k] for k in range(16)], "expect": t["expectedSignatureFragments"]})
    # ML-KEM keygen KATs (vectors.mlkem1024.keygen and vectors.mlkem768.keygen), derived as
    # sdks/KnishIO-Client-JS src/Wallet.js _deriveMlKemKeypair: seed = generateSecret(wallet.key,
    # 128), i.e. SHAKE256 of the wallet key's text, 64 bytes (d || z); expect = the byte-frozen
    # public key. Coins are fixed so the selftest's encaps/decaps round trip also runs on each key.
    canon_mlkem = []
    for vid, (block_name, pk_len, extra) in enumerate((("mlkem1024", 1568, {}), ("mlkem768", 1184, {"set": 768}))):
        kg = vectors[block_name]["keygen"]
        pk = base64.b64decode(kg["expectedPubkey"], validate=True)
        if len(pk) != pk_len:
            print("FAIL %s.keygen: expectedPubkey is %d bytes, not %d" % (block_name, len(pk), pk_len), file=sys.stderr)
            return 2
        kg_key = generate_key(kg["secret"], kg["token"], kg["position"])
        case = {"id": "vk%d" % vid, "name": block_name + "." + kg["name"]}
        case.update(extra)
        case.update({"seed": shake_hex(kg_key.encode("utf-8"), 64),
                     "coins": shake_hex(("kcore.canonical.%s.coins" % block_name).encode("ascii"), 32),
                     "expect": pk.hex()})
        canon_mlkem.append(case)

    def block(name, cases, indent=""):
        body = ",\n".join(indent + json.dumps(c, separators=(",", ":")) for c in cases)
        return '%s"%s":[\n%s\n%s]' % (indent, name, body, indent)

    doc = "{\n" + ",\n".join([
        block("shake", shake_cases),
        block("chains", chain_cases),
        block("address", address_cases),
        block("mlkem", mlkem_cases),
        '"canonical":{\n' + ",\n".join([
            block("shake", canon_shake),
            block("address", canon_address),
            block("chains", canon_chains),
            block("mlkem", canon_mlkem),
        ]) + "\n}",
    ]) + "\n}\n"
    json.loads(doc)  # the line format must stay valid JSON
    with open(os.path.join(out_dir, "differential.json"), "w", encoding="utf-8") as f:
        f.write(doc)

    print("vectors: shake=%d chains=%d address=%d mlkem=%d canonical=%d/%d/%d/%d" % (
        len(shake_cases), len(chain_cases), len(address_cases), len(mlkem_cases),
        len(canon_shake), len(canon_address), len(canon_chains), len(canon_mlkem)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

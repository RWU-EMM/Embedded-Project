import json

# ==========================
# CONFIG (STATIC)
# ==========================
SECRET = "PICO_SECRET"

# ==========================
# HASH (same as Pico)
# ==========================
def simple_hash(s):
    h = 5381
    for c in s:
        h = ((h << 5) + h) ^ ord(c)
        h &= 0xFFFFFFFF  # 32-bit overflow
    return h


# ==========================
# STEP 1 JSON
# ==========================
def gen_step1(in_key):
    msg = {
        "in_key": in_key
    }

    print("\n=== STEP 1 (/connect) ===")
    print(json.dumps(msg))


# ==========================
# STEP 2 JSON
# ==========================
def gen_step2(in_key, out_key, token):
    combined = in_key + out_key + SECRET
    h = simple_hash(combined)

    msg = {
        "out_key": out_key,
        "reg": str(h),
        "token": token
    }

    print("\n=== STEP 2 (/connect) ===")
    print("STRING USED:", combined)
    print("HASH:", h)
    print(json.dumps(msg))


# ==========================
# MAIN (INTERACTIVE)
# ==========================
def main():
    print("=== MQTT AUTH TOOL ===")

    # STEP 1
    in_key = input("Enter IN_KEY: ").strip()
    gen_step1(in_key)

    # STEP 2
    print("\n--- After receiving OUT_KEY from Pico ---")
    out_key = input("Enter OUT_KEY: ").strip()

    token = input("Enter TOKEN (default=dev1): ").strip()
    if not token:
        token = "dev1"

    gen_step2(in_key, out_key, token)


if __name__ == "__main__":
    main()
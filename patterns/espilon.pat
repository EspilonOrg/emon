# Espilon firmware + ESPM runtime patterns
# Mirror of builtin_espilon[] in src/detector.c — use this for runtime tweaks
# without recompiling. Load with: --patterns espilon.pat
# Format: SEVERITY  NAME  REGEX

# ── Security ────────────────────────────────────────────────────────────────
CRITICAL  MITM_DETECTED    Server verification FAILED
HIGH      AEAD_FAIL        AEAD auth/decrypt failed
HIGH      NO_CHALLENGE     server_verify: no challenge

# ── ESPM module lifecycle faults ────────────────────────────────────────────
CRITICAL  ESPM_PANIC       espm_panic.*module.*faulted
HIGH      ESPM_WATCHDOG    espm_watchdog.*timeout
HIGH      ESPM_LOAD_FAIL   espm.*load.*failed

# ── Transport state ─────────────────────────────────────────────────────────
WARN      PEER_CLOSED      RX: peer closed connection
WARN      WIFI_DISCONNECT  CORE_WIFI: Disconnected
WARN      CONNECT_FAIL     CORE_WIFI: connect\(\) failed

# ── Positive signals (useful for runner assertions) ─────────────────────────
INFO      AUTH_OK          Server identity verified
INFO      HANDSHAKE_OK     CORE_WIFI: Handshake done
INFO      ESPM_LOADED      espm.*module.*loaded
INFO      ESPM_UNLOADED    espm.*module.*unloaded
INFO      C2_CMD           DISPATCH: C2 CMD:
INFO      READY            ESPILON: espilon ready

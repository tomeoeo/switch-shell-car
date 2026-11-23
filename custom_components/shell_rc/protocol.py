"""Shell RC (QCAR / Marvel 2026) protocol, autopilot and steering assist.

Pure Python, no Home Assistant imports (unit-tested in tests-ha/).
Mirrors core/qcar.c, core/autopilot.c and core/steer.c.

Control frame plaintext (AES-128-ECB, key below), written to ...9600:
  [0]=0 [1..3]='CTL' [4]=forward [5]=backward [6]=left [7]=right
  [8]=lights (0x00 on, 0x01 off) [9]=speed 0..100 [10..15]=0
Battery notify from ...9601: [0]=counter [1..3]='VBT' [4]=percent
"""

from __future__ import annotations

from dataclasses import dataclass, replace

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

KEY = bytes.fromhex("34522a5b7a6e492c08090a9d8d2a23f8")

SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
CONTROL_UUID = "d44bc439-abfd-45a2-b575-925416129600"
BATTERY_UUID = "d44bc439-abfd-45a2-b575-925416129601"
# Advert: manufacturer data bytes 54 52 00 3c -> company id 0x5254 = 21076.
MANUFACTURER_ID = 0x5254

SPEED_NORMAL = 0x50
SPEED_MAX = 100


@dataclass(frozen=True)
class DriveCmd:
    throttle: int = 0  # 1 forward, -1 reverse, 0 none
    steer: int = 0  # -1 left, 1 right, 0 straight
    speed: int = SPEED_NORMAL  # used only while throttle != 0
    lights: bool = False


NEUTRAL = DriveCmd()


def neutral(lights: bool = False) -> DriveCmd:
    return DriveCmd(lights=lights)


def plaintext(cmd: DriveCmd) -> bytes:
    p = bytearray(16)
    p[1:4] = b"CTL"
    p[4] = 1 if cmd.throttle > 0 else 0
    p[5] = 1 if cmd.throttle < 0 else 0
    p[6] = 1 if cmd.steer < 0 else 0
    p[7] = 1 if cmd.steer > 0 else 0
    p[8] = 0x00 if cmd.lights else 0x01
    speed = max(0, min(SPEED_MAX, int(cmd.speed)))
    p[9] = speed if cmd.throttle != 0 else SPEED_NORMAL
    return bytes(p)


class Codec:
    """AES-128-ECB with the published key. Cheap; one block per frame."""

    def __init__(self, key: bytes = KEY) -> None:
        self._cipher = Cipher(algorithms.AES(key), modes.ECB())
        self._cache: dict[DriveCmd, bytes] = {}

    def encrypt(self, block: bytes) -> bytes:
        e = self._cipher.encryptor()
        return e.update(block) + e.finalize()

    def decrypt(self, block: bytes) -> bytes:
        d = self._cipher.decryptor()
        return d.update(block) + d.finalize()

    def encode(self, cmd: DriveCmd) -> bytes:
        cmd = replace(cmd, speed=SPEED_NORMAL) if cmd.throttle == 0 else cmd
        frame = self._cache.get(cmd)
        if frame is None:
            frame = self.encrypt(plaintext(cmd))
            if len(self._cache) < 512:
                self._cache[cmd] = frame
        return frame

    def decode_battery(self, data: bytes) -> tuple[int, int] | None:
        """(percent, counter) or None if this is not a battery frame."""
        if len(data) != 16:
            return None
        p = self.decrypt(bytes(data))
        if p[1:4] != b"VBT" or p[4] > 100:
            return None
        return p[4], p[0]


class SteerAssist:
    """Speed kick when a turn starts + speed floor while turning (core/steer.c).

    The speed byte powers both motors, so slow turns were too weak.
    """

    def __init__(self, kick_ms: int = 150, kick_speed: int = 60, floor: int = 30) -> None:
        self.kick_ms = kick_ms
        self.kick_speed = min(kick_speed, SPEED_MAX)
        self.floor = min(floor, SPEED_MAX)
        self._prev = 0
        self._kick_until = 0

    def apply(self, cmd: DriveCmd, now_ms: int) -> DriveCmd:
        if cmd.steer != 0 and cmd.steer != self._prev:
            self._kick_until = now_ms + self.kick_ms
        self._prev = cmd.steer
        if cmd.throttle == 0 or cmd.steer == 0:
            return cmd
        speed = max(cmd.speed, self.floor)
        if self.kick_ms and now_ms < self._kick_until:
            speed = max(speed, self.kick_speed)
        return replace(cmd, speed=speed)


class Autopilot:
    """Cat mode: slow random wandering in short segments (core/autopilot.c)."""

    def __init__(self, seed: int, now_ms: int, duration_ms: int, lo: int = 20, hi: int = 60) -> None:
        self._rng = (seed & 0xFFFFFFFF) or 0x9E3779B9
        hi = min(hi, SPEED_MAX)
        lo = min(lo, hi)
        self.lo, self.hi = lo, hi
        self.end_ms = now_ms + duration_ms
        self._seg_end = now_ms
        self._seg = NEUTRAL
        self.done = duration_ms <= 0
        self.segments = 0

    def _rand(self) -> int:
        x = self._rng
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self._rng = x
        return x

    def _range(self, lo: int, hi: int) -> int:
        return lo if hi <= lo else lo + self._rand() % (hi - lo + 1)

    def _new_segment(self, now_ms: int) -> None:
        roll = self._range(0, 99)
        turn = 1 if self._rand() & 1 else -1
        if roll < 35:  # cruise forward
            seg = DriveCmd(1, 0, self._range(self.lo, self.hi))
            dur = self._range(500, 2000)
        elif roll < 60:  # forward turn
            seg = DriveCmd(1, turn, self._range(self.lo, self.hi))
            dur = self._range(400, 1500)
        elif roll < 70:  # back off, maybe turning
            steer = turn if self._rand() % 3 == 0 else 0
            seg = DriveCmd(-1, steer, self._range(self.lo, (self.lo + self.hi) // 2))
            dur = self._range(300, 1000)
        elif roll < 85:  # sudden dart
            seg = DriveCmd(-1 if self._rand() % 4 == 0 else 1, 0, self.hi)
            dur = self._range(200, 500)
        else:  # pause
            seg = NEUTRAL
            dur = self._range(500, 3000)
        self._seg = seg
        self._seg_end = now_ms + dur
        self.segments += 1

    def update(self, now_ms: int, lights: bool) -> DriveCmd:
        if not self.done and now_ms >= self.end_ms:
            self.done = True
        if self.done:
            return neutral(lights)
        if now_ms >= self._seg_end:
            self._new_segment(now_ms)
        return replace(self._seg, lights=lights)

    def remaining_s(self, now_ms: int) -> int:
        if self.done or self.end_ms <= now_ms:
            return 0
        return (self.end_ms - now_ms + 999) // 1000

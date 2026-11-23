"""Host tests for custom_components/shell_rc/protocol.py (no Home Assistant needed).

Run: python3 -m unittest discover -s tests-ha
"""

import importlib.util
import pathlib
import sys
import unittest

_PATH = pathlib.Path(__file__).resolve().parent.parent / "custom_components/shell_rc/protocol.py"
_spec = importlib.util.spec_from_file_location("shell_rc_protocol", _PATH)
P = importlib.util.module_from_spec(_spec)
sys.modules["shell_rc_protocol"] = P  # dataclasses need the module registered
_spec.loader.exec_module(P)

D = P.DriveCmd

# jenkings/BLE-RC-Car-ESP32 frames + gist "keep steady" (= left, lights on).
VECTORS = [
    (D(0, 0, 0x50), "025e695a48ff2a438ca680f83e04e45d"),
    (D(1, 0, 0x50), "29609c664852cff1b0f0cbb98014bd2c"),
    (D(-1, 0, 0x50), "03209909ba9da1c8b986163c6d484655"),
    (D(0, -1, 0x50), "51382112135cccdb46cf8921b705499a"),
    (D(0, 1, 0x50), "1b5769cdf13e8ab627080ff3cefc3bc0"),
    (D(1, 0, 0x64), "e65567da8e6c560d09d3733a7f47ff06"),
    (D(-1, 0, 0x64), "cec2ff1d7acc163cd13b7e6153ad5c45"),
    (D(1, -1, 0x50), "9928e590dfe821485f414fbb633d5c4e"),
    (D(1, 1, 0x50), "0f2ce56662d4fd9d32a44f102bf20aa7"),
    (D(-1, -1, 0x50), "98ce981d58d115afe11960bf4613925c"),
    (D(-1, 1, 0x50), "f2520fba3144fb11468fe080c6c2c23c"),
    (D(1, -1, 0x64), "592381c943a417ca1bc3b59400e0fc12"),
    (D(1, 1, 0x64), "fb976fba04af87022226ec50ae82f8c4"),
    (D(-1, -1, 0x64), "d54ad55857d327745f141dd00d671595"),
    (D(-1, 1, 0x64), "80dfb2165f3260f1d9837750f43a43da"),
    (D(0, -1, 0x50, True), "e1055f54d880f49c2ce547267f930bf2"),
]


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.c = P.Codec()

    def test_vectors(self):
        for cmd, cipher in VECTORS:
            self.assertEqual(self.c.encode(cmd).hex(), cipher, cmd)

    def test_neutral_ignores_speed(self):
        self.assertEqual(self.c.encode(D(0, 0, 37)).hex(), VECTORS[0][1])
        self.assertEqual(P.plaintext(D(1, 0, 250))[9], 100)
        p = P.plaintext(D(1, 0, 35, True))
        self.assertEqual((p[4], p[5], p[8], p[9]), (1, 0, 0, 35))

    def test_battery(self):
        self.assertEqual(self.c.decode_battery(bytes.fromhex("83230ab55f4318b94eaff4696bff1ab1")), (72, 2))
        self.assertIsNone(self.c.decode_battery(self.c.encode(P.NEUTRAL)))
        self.assertIsNone(self.c.decode_battery(b"\x00" * 5))

    def test_manufacturer_id_matches_advert(self):
        # HA advert dump 2026-11-22: "manufacturer_data": {"21076": "003c"}, raw ...ff5452003c...
        self.assertEqual(P.MANUFACTURER_ID, 21076)


class SteerAssistTest(unittest.TestCase):
    def test_kick_and_floor(self):
        s = P.SteerAssist(150, 60, 30)
        self.assertEqual(s.apply(D(1, 0, 20), 0).speed, 20)
        self.assertEqual(s.apply(D(1, -1, 20), 100).speed, 60)
        self.assertEqual(s.apply(D(1, -1, 20), 200).speed, 60)
        self.assertEqual(s.apply(D(1, -1, 20), 251).speed, 30)
        self.assertEqual(s.apply(D(1, -1, 45), 300).speed, 45)
        self.assertEqual(s.apply(D(1, 1, 20), 400).speed, 60)
        self.assertEqual(s.apply(D(0, 1, 0x50), 500), D(0, 1, 0x50))
        self.assertEqual(P.SteerAssist(0, 0, 0).apply(D(1, 1, 20), 10).speed, 20)


class AutopilotTest(unittest.TestCase):
    def test_two_minutes(self):
        a = P.Autopilot(12345, 1000, 120000, 20, 60)
        speeds, moving, paused, turning, reverse = [], 0, 0, 0, 0
        for t in range(1000, 121000, 10):
            c = a.update(t, True)
            p = P.plaintext(c)
            self.assertFalse(p[4] and p[5])
            self.assertFalse(p[6] and p[7])
            self.assertTrue(c.lights)
            if c.throttle:
                moving += 1
                speeds.append(c.speed)
                reverse += c.throttle < 0
            else:
                paused += 1
            turning += c.steer != 0
        self.assertGreaterEqual(min(speeds), 20)
        self.assertLessEqual(max(speeds), 60)
        self.assertTrue(moving > paused > 0 and turning > 0 and reverse > 0)
        self.assertGreater(a.segments, 50)
        self.assertEqual(a.remaining_s(120000), 1)
        end = a.update(121000, False)
        self.assertTrue(a.done)
        self.assertEqual((end.throttle, end.steer), (0, 0))

    def test_deterministic_and_edges(self):
        a, b = P.Autopilot(7, 0, 10000), P.Autopilot(7, 0, 10000)
        for t in range(0, 10000, 10):
            self.assertEqual(a.update(t, False), b.update(t, False))
        self.assertTrue(P.Autopilot(1, 0, 0).done)
        z = P.Autopilot(1, 0, 1000, 90, 200)
        self.assertEqual((z.lo, z.hi), (90, 100))


if __name__ == "__main__":
    unittest.main()

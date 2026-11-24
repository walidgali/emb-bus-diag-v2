"""End-to-end: run the real CLI against a pseudo-terminal acting as a sensor."""
import os
import pty
import subprocess
import threading
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "emb-bus-diag-v2")


class UartCliTest(unittest.TestCase):
    def test_uart_poll_reads_12v(self):
        master, slave = pty.openpty()
        requests = []

        def device():
            try:
                for _ in range(3):
                    req = os.read(master, 1)
                    requests.append(req)
                    os.write(master, bytes([0x5D, 0xC0]))   # INA219 raw 24000 -> 12.0 V
            except OSError:
                pass

        t = threading.Thread(target=device, daemon=True)
        t.start()
        env = dict(os.environ, EMBDIAG_UART_DEV=os.ttyname(slave))
        report = os.path.join(ROOT, "build", "uart_cli_report.json")
        os.makedirs(os.path.dirname(report), exist_ok=True)
        r = subprocess.run(
            [CLI, "--bus", "uart", "-r", "2", "-n", "3", "-i", "10", "-o", report],
            capture_output=True, text=True, env=env, timeout=20)
        t.join(timeout=5)
        os.close(master)
        os.close(slave)

        self.assertEqual(r.returncode, 0, r.stderr)
        lines = [l for l in r.stdout.splitlines() if l.startswith("[")]
        self.assertEqual(len(lines), 3)
        for l in lines:
            self.assertIn("Val=12.0000 V", l)
            self.assertIn("| OK |", l)
        self.assertEqual(requests, [b"\x02"] * 3)           # -r 2 reaches the device

    def test_uart_missing_device_exits_1(self):
        env = dict(os.environ, EMBDIAG_UART_DEV="/dev/does-not-exist")
        r = subprocess.run([CLI, "--bus", "uart"], capture_output=True, text=True, env=env, timeout=20)
        self.assertEqual(r.returncode, 1)


if __name__ == "__main__":
    unittest.main()

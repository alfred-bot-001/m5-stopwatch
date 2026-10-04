"""Host logger regression tests; all USB, clocks, and signal handlers are mocked."""
import importlib.util
import json
from pathlib import Path
import signal
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import Mock, patch


# Load without requiring pyserial, and make accidental real USB access impossible.
serial_stub = ModuleType('serial')
serial_stub.SerialException = OSError
serial_stub.Serial = Mock()
ports_stub = ModuleType('serial.tools.list_ports')
ports_stub.comports = Mock()
spec = importlib.util.spec_from_file_location(
    'stopwatch_diagnose', Path(__file__).resolve().parents[1] / 'tools/diagnose.py')
diagnose = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {'serial': serial_stub, 'serial.tools': ModuleType('serial.tools'),
                             'serial.tools.list_ports': ports_stub}):
    spec.loader.exec_module(diagnose)


class Clock:
    def __init__(self):
        self.wall, self.elapsed = 1000.0, 0.0
        self.after_sleep = None

    def time(self):
        return self.wall

    def monotonic(self):
        return self.elapsed

    def sleep(self, seconds):
        self.wall += seconds
        self.elapsed += seconds
        if self.after_sleep:
            self.after_sleep()


APP = SimpleNamespace(vid=0xcafe, pid=0x4020,
                      serial_number='288485439560-VIBE1', device='mock-app')
ROM = SimpleNamespace(vid=0x303a, pid=0x1001,
                      serial_number='28:84:85:43:95:60', device='mock-rom')


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.clock, self.handlers = Clock(), {}
        self.run_number = 0
        self.port_factory = Mock(side_effect=AssertionError('Unexpected serial open'))

    def register_signal(self, number, handler):
        old = self.handlers.get(number)
        self.handlers[number] = handler
        return old

    def invoke(self, ports=(), seconds=65, deadline=None, enumerate_error=None):
        self.run_number += 1
        path = Path(self.directory.name) / str(self.run_number)
        args = ['--output', str(path), '--seconds', str(seconds)]
        if deadline is not None:
            args += ['--deadline-unix', str(deadline)]
        signals = SimpleNamespace(SIGINT=signal.SIGINT, SIGTERM=signal.SIGTERM,
                                  Signals=signal.Signals, signal=self.register_signal)
        enumeration = Mock(return_value=ports, side_effect=enumerate_error)
        with patch.object(diagnose, 'time', self.clock), \
             patch.object(diagnose, 'signal', signals), \
             patch.object(diagnose, 'comports', enumeration), \
             patch.object(diagnose.serial, 'Serial', self.port_factory):
            result = diagnose.main(args)
        return result, [json.loads(line) for line in path.read_text().splitlines()], enumeration

    def use_application(self, lines=None):
        connection = Mock()

        def read():
            self.clock.sleep(1)
            return next(lines) if lines is not None else b''

        connection.readline.side_effect = read
        self.port_factory = Mock(return_value=connection)
        return connection

    def test_shared_deadline_expires_across_restart(self):
        self.invoke(seconds=10, deadline=1020)
        _, second, _ = self.invoke(seconds=100, deadline=1020)
        self.assertEqual(second[0]['duration_seconds'], 10)
        self.assertEqual(self.clock.wall, 1020)
        code, expired, enumeration = self.invoke(seconds=100, deadline=1020)
        self.assertEqual(code, 0)
        self.assertEqual([row['kind'] for row in expired], ['start', 'stop'])
        self.assertEqual(expired[0]['duration_seconds'], 0)
        enumeration.assert_not_called()

    def test_rom_and_another_application_are_never_opened(self):
        other = SimpleNamespace(**{**vars(APP), 'serial_number': 'another-device'})
        code, rows, _ = self.invoke([ROM, other], seconds=1)
        self.assertEqual(code, 0)
        self.assertEqual(rows[1]['mode'], 'rom')
        self.port_factory.assert_not_called()

    def test_heartbeat_while_absent_or_application_silent(self):
        for available in (False, True):
            with self.subTest(application=available):
                self.clock = Clock()
                connection = self.use_application() if available else None
                code, rows, _ = self.invoke([APP] if available else [])
                self.assertEqual(code, 0)
                beats = [row for row in rows if row['kind'] == 'heartbeat']
                self.assertEqual([row['wall_gap_seconds'] for row in beats], [30, 30])
                self.assertEqual(beats[-1]['mode'], 'app' if available else 'absent')
                self.assertIsNone(beats[-1]['last_status_unix'])
                if connection:
                    self.port_factory.assert_called_once_with(port=None, baudrate=115200, timeout=1)
                    self.assertEqual(connection.port, APP.device)
                    self.assertTrue(connection.dtr)
                    self.assertFalse(connection.rts)
                    connection.close.assert_called_once()

    def test_cumulative_event_bypasses_five_second_sampling(self):
        self.use_application(iter([b'VIBE uptime=1000 pm_events=0\n',
                                   b'VIBE uptime=2000 pm_events=0\n',
                                   b'VIBE uptime=3000 pm_events=1\n']))
        code, rows, _ = self.invoke([APP], seconds=3)
        self.assertEqual(code, 0)
        statuses = [row['value'] for row in rows if row['kind'] == 'status']
        self.assertEqual(len(statuses), 2)
        self.assertIn('pm_events=1', statuses[-1])

    def test_signal_exit_reason(self):
        for number in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=number):
                self.clock = Clock()
                self.clock.after_sleep = lambda: self.handlers[number](number, None)
                code, rows, _ = self.invoke()
                self.assertEqual(code, 128 + number)
                self.assertEqual(rows[-2]['kind'], 'signal')
                self.assertEqual(rows[-1]['reason'], signal.Signals(number).name)

    def test_enumeration_exception_logs_stop_reason(self):
        code, rows, _ = self.invoke(enumerate_error=RuntimeError('enumeration failed'))
        self.assertEqual(code, 1)
        self.assertEqual(rows[-2]['kind'], 'outer_error')
        self.assertEqual(rows[-2]['error_type'], 'RuntimeError')
        self.assertEqual(rows[-1]['reason'], 'exception')

    def test_host_resume_gap_obeys_absolute_deadline(self):
        def resume():
            self.clock.wall += 100
            self.clock.after_sleep = None

        self.clock.after_sleep = resume
        code, rows, _ = self.invoke(seconds=60)
        self.assertEqual(code, 0)
        self.assertEqual(rows[-2]['kind'], 'heartbeat')
        self.assertEqual(rows[-2]['wall_gap_seconds'], 101)
        self.assertEqual(rows[-2]['monotonic_gap_seconds'], 1)
        self.assertEqual(rows[-1]['reason'], 'deadline')


if __name__ == '__main__':
    unittest.main()

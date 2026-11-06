"""Regression tests for the headless server's isolated port allocation."""
import socket
import unittest

from headless_server_smoke import PORT_KEYS, reserve_ports


class HeadlessPortAllocatorTests(unittest.TestCase):
    def test_allocates_one_unique_port_per_service(self):
        for _ in range(10):
            ports = reserve_ports()
            self.assertEqual(len(ports), len(PORT_KEYS))
            self.assertEqual(len(ports), len(set(ports)))
            self.assertTrue(all(1024 <= port <= 65535 for port in ports))

    def test_allocated_ports_can_be_rebound_on_loopback(self):
        ports = reserve_ports()
        listeners = []
        try:
            for port in ports:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                listeners.append(sock)
                sock.bind(("127.0.0.1", port))
                sock.listen(1)
        finally:
            for sock in listeners:
                sock.close()


if __name__ == "__main__":
    unittest.main()

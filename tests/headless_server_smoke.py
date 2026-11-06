#!/usr/bin/env python3
"""Headless startup/shutdown smoke test; no proprietary Darkspore files needed.

Requires the built Linux recap_server binary and the repository's res/data/.
Runs exclusively on ephemeral loopback ports in a temporary directory.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

PORT_KEYS = (
    "SERVER_REDIRECTOR_PORT", "SERVER_BLAZE_PORT", "SERVER_PSS_PORT",
    "SERVER_TICK_PORT", "SERVER_TELEMETRY_PORT", "SERVER_QOS_PORT",
    "SERVER_HTTP_PORT", "SERVER_HTTP_TELEMETRY_PORT", "SERVER_HTTP_QOS_PORT",
)


def reserve_ports():
    # Keep ports reserved while selecting all nine to avoid duplicates.
    sockets = []
    try:
        for _ in PORT_KEYS:
            sock = socket.socket()
            sock.bind(("127.0.0.1", 0))
            sockets.append(sock)
        return [sock.getsockname()[1] for sock in sockets]
    finally:
        for sock in sockets:
            sock.close()


def smoke(server: Path, data: Path) -> None:
    server, data = server.resolve(), data.resolve()
    if not server.is_file() or not data.is_dir():
        raise RuntimeError("Missing compiled server or bundled data directory")
    with tempfile.TemporaryDirectory(prefix="recap-headless-") as temp:
        cwd = Path(temp)
        (cwd / "data").symlink_to(data, target_is_directory=True)
        ports = dict(zip(PORT_KEYS, reserve_ports()))
        root = ET.Element("configs")
        for key, port in ports.items():
            ET.SubElement(root, "config", key=key, value=str(port))
        ET.ElementTree(root).write(cwd / "config.xml", encoding="utf-8", xml_declaration=True)

        log_path = cwd / "server.log"
        with log_path.open("w+", encoding="utf-8") as log:
            proc = subprocess.Popen(
                [str(server), "--darkspore-path", str(cwd / "missing-game-client")],
                cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 20
                ready = False
                while time.monotonic() < deadline:
                    if proc.poll() is not None:
                        break
                    try:
                        with socket.create_connection(("127.0.0.1", ports["SERVER_HTTP_PORT"]), timeout=0.3):
                            ready = True
                            break
                    except OSError:
                        time.sleep(0.1)
                if not ready:
                    raise RuntimeError("Headless server did not bind an HTTP listener")
                proc.terminate()
                code = proc.wait(timeout=20)
                if code != 0:
                    raise RuntimeError(f"Headless server crashed on SIGTERM: exit={code}")
                print("Headless server opened HTTP and shut down cleanly")
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                log.flush()
                if proc.returncode != 0:
                    log.seek(0)
                    print(log.read()[-6000:])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    args = parser.parse_args()
    smoke(args.server, args.data)

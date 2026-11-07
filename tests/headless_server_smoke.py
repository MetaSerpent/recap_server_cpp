#!/usr/bin/env python3
"""Headless startup/shutdown smoke test; no proprietary Darkspore files needed.

Requires the built Linux recap_server binary and the repository's res/data/.
Runs exclusively on ephemeral loopback ports in a temporary directory.
"""
from __future__ import annotations

import argparse
import http.client
import json
import os
import shutil
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


def query_server(port: int, resource: str) -> tuple[int, bytes]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        connection.request("GET", resource)
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def verify_http_routes(port: int) -> None:
    # The launcher polls this API before opening the game.
    status, data = query_server(port, "/recap/api?method=api.game.status")
    if status != 200:
        raise RuntimeError(f"Launcher status API returned HTTP {status}")
    payload = json.loads(data)
    if not isinstance(payload.get("enablePlayButton"), bool):
        raise RuntimeError("Launcher API returned an invalid JSON status payload")

    # Exercise a real XML response expected by the unmodified game client.
    status, data = query_server(port, "/game/api?method=api.status.getStatus")
    if status != 200 or b"<status>" not in data:
        raise RuntimeError(f"Game status XML API returned invalid response: {status}")

    # A nonexistent file must not be served as a successful route.
    status, _ = query_server(port, "/__recap_missing_file_for_smoke__")
    if status != 404:
        raise RuntimeError(f"Missing static resource returned HTTP {status}, expected 404")

    # The public HTTP listener must reject traversal attempts.
    status, _ = query_server(port, "/../__recap_path_traversal_probe__")
    if status != 400:
        raise RuntimeError(f"Illegal HTTP path returned HTTP {status}, expected 400")


def verify_registration_roundtrip(port: int, cwd: Path) -> None:
    # This exercises the real registration API and two successive on-disk
    # saves. Accounts exist only in TemporaryDirectory and are discarded.
    base = "/recap/api?method=api.game.registration"
    registration = (
        base + "&name=SmokePlayer&email=smoke@example.invalid"
        + "&pass=ephemeral-password&avatar=0"
    )
    status, body = query_server(port, registration)
    if status != 200 or json.loads(body).get("success") is not True:
        raise RuntimeError(f"Local registration failed: HTTP {status}, {body[:300]!r}")

    saved = cwd / "storage/user/smoke@example.invalid.xml"
    if not saved.is_file():
        raise RuntimeError("Registration was acknowledged but no account XML was persisted")
    document = ET.parse(saved)
    if document.getroot().findtext("name") != "SmokePlayer":
        raise RuntimeError("Saved account XML lost the registration name")
    if not document.getroot().find("account") is not None:
        raise RuntimeError("Registered account has no persisted account state")
    initial_bytes = saved.read_bytes()

    # Creating two players during one smoke run also catches the former
    # time-seeded account-ID collision during rapid registrations.
    second = base + "&name=SecondPlayer&email=second@example.invalid" \
        + "&pass=ephemeral-password&avatar=1"
    status, body = query_server(port, second)
    if status != 200 or json.loads(body).get("success") is not True:
        raise RuntimeError(f"Second account registration failed: HTTP {status}")
    saved_second = cwd / "storage/user/second@example.invalid.xml"
    second_document = ET.parse(saved_second)
    first_id = document.getroot().findtext("account/id")
    second_id = second_document.getroot().findtext("account/id")
    if not first_id or not second_id or first_id == "0" or first_id == second_id:
        raise RuntimeError("New accounts have missing or duplicate persistent IDs")

    # Duplicate registration must be rejected without overwriting a save.
    status, _ = query_server(port, registration)
    if status == 200 or saved.read_bytes() != initial_bytes:
        raise RuntimeError("Duplicate registration accepted or corrupted existing user XML")

    # Invalid avatar input must fail before touching any account file.
    status, _ = query_server(
        port, base + "&name=BadAvatar&email=badavatar@example.invalid"
        + "&pass=ephemeral-password&avatar=not-a-number"
    )
    if status == 200 or (cwd / "storage/user/badavatar@example.invalid.xml").exists():
        raise RuntimeError("Invalid avatar caused a partial account to be registered")



def verify_restarted_server(server: Path, cwd: Path, port: int) -> None:
    """Start a second real process against the previous account files.

    Catches account state that exists only in memory, broken startup
    deserialization, and accidental overwrite of user saves after restart.
    """
    account_paths = [
        cwd / "storage/user/smoke@example.invalid.xml",
        cwd / "storage/user/second@example.invalid.xml",
    ]
    before = {path: path.read_bytes() for path in account_paths}
    with (cwd / "server-restart.log").open("w+", encoding="utf-8") as log:
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
                    status, payload = query_server(
                        port, "/recap/api?method=api.game.status"
                    )
                    if status == 200 and isinstance(
                        json.loads(payload).get("enablePlayButton"), bool
                    ):
                        ready = True
                        break
                except (OSError, ValueError, http.client.HTTPException):
                    time.sleep(0.1)
            if not ready:
                raise RuntimeError("Restarted server did not become HTTP-ready")

            # Existing accounts must remain known across process boundaries.
            for name, email in [
                ("SmokePlayer", "smoke@example.invalid"),
                ("SecondPlayer", "second@example.invalid"),
            ]:
                request = (
                    "/recap/api?method=api.game.registration"
                    f"&name={name}&email={email}&pass=ephemeral-password&avatar=0"
                )
                status, _ = query_server(port, request)
                if status == 200:
                    raise RuntimeError(
                        f"Restarted server accepted duplicate registration: {email}"
                    )
            for path, original in before.items():
                if path.read_bytes() != original:
                    raise RuntimeError(
                        f"Restart changed an existing persisted account: {path.name}"
                    )

            # Account XML should remain parseable and keep stable IDs.
            ids = [
                ET.parse(path).getroot().findtext("account/id")
                for path in account_paths
            ]
            if not all(ids) or ids[0] == ids[1]:
                raise RuntimeError("Persisted account identities changed after restart")
            proc.terminate()
            result = proc.wait(timeout=20)
            if result != 0 and os.name != "nt":
                raise RuntimeError(
                    f"Restarted headless server crashed on SIGTERM: exit={result}"
                )
            print("Second server process reloaded accounts without data loss")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            if proc.returncode != 0 and os.name != "nt":
                log.seek(0)
                print(log.read()[-6000:])

def smoke(server: Path, data: Path) -> None:
    server, data = server.resolve(), data.resolve()
    if not server.is_file() or not data.is_dir():
        raise RuntimeError("Missing compiled server or bundled data directory")
    with tempfile.TemporaryDirectory(prefix="recap-headless-") as temp:
        cwd = Path(temp)
        try:
            (cwd / "data").symlink_to(data, target_is_directory=True)
        except (NotImplementedError, OSError):
            # Windows runners may forbid symlinks; keep the test portable.
            shutil.copytree(data, cwd / "data")
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
                verify_http_routes(ports["SERVER_HTTP_PORT"])
                verify_registration_roundtrip(ports["SERVER_HTTP_PORT"], cwd)
                proc.terminate()
                code = proc.wait(timeout=20)
                if code != 0 and os.name != "nt":
                    raise RuntimeError(f"Headless server crashed on SIGTERM: exit={code}")
                print("Headless server passed HTTP APIs and local account registration/persistence"
                      + (" and clean shutdown" if os.name != "nt" else " (Windows termination)"))
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                log.flush()
                if proc.returncode != 0 and os.name != "nt":
                    log.seek(0)
                    print(log.read()[-6000:])

        # Verify the same account files survive a completely new process.
        verify_restarted_server(server, cwd, ports["SERVER_HTTP_PORT"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    args = parser.parse_args()
    smoke(args.server, args.data)

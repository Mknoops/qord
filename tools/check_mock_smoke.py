"""Bounded offscreen/mock startup smoke; run only inside security-validation CI.

Usage inside its sandbox: python3 /src/tools/check_mock_smoke.py /work/build/qord
This is a connection/startup check, not a visual-layout or interactive UI test.
"""
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import time
import urllib.request


def main() -> None:
    source = Path(__file__).resolve().parent
    # Fail before starting repository-controlled processes outside the verified
    # no-external-network, read-only-source CI sandbox.
    runpy.run_path(str(source / "check_security_sandbox.py"))
    if len(sys.argv) != 2:
        raise SystemExit("expected the built qord executable")
    executable = Path(sys.argv[1]).resolve(strict=True)
    if not executable.is_relative_to(Path("/work")):
        raise SystemExit("the executable must be in sandbox scratch")
    scratch = Path("/work/mock-ui-smoke")
    home = scratch / "home"
    runtime = scratch / "runtime"
    home.mkdir(parents=True, exist_ok=True)
    runtime.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)
    env = dict(os.environ, HOME=str(home), XDG_RUNTIME_DIR=str(runtime),
               QT_QPA_PLATFORM="offscreen", QORD_MOCK_DISCORD="http://127.0.0.1:8765")
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    base = "http://127.0.0.1:8765"
    children = []
    try:
        with (scratch / "server.log").open("wb") as server_log, (scratch / "client.log").open("wb") as client_log:
            server = subprocess.Popen([sys.executable, str(source / "mock_discord.py"), "--port", "8765"],
                                      stdout=server_log, stderr=subprocess.STDOUT, env=env)
            children.append(server)
            deadline = time.monotonic() + 5
            while True:
                if server.poll() is not None:
                    raise RuntimeError("mock server exited; see server.log")
                try:
                    with opener.open(base + "/mock/state", timeout=0.5) as reply:
                        assert reply.status == 200
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("mock server was not ready within 5 seconds")
                    time.sleep(0.1)
            client = subprocess.Popen([str(executable)], stdout=client_log,
                                      stderr=subprocess.STDOUT, env=env)
            children.append(client)
            deadline = time.monotonic() + 20
            while "gateway: identified, READY sent" not in (scratch / "server.log").read_text(errors="replace"):
                if client.poll() is not None:
                    raise RuntimeError("client exited before mock READY; see client.log")
                if time.monotonic() >= deadline:
                    raise RuntimeError("client did not identify within 20 seconds; check curl WebSocket support and client.log")
                time.sleep(0.1)
            request = urllib.request.Request(base + "/mock/say",
                data=json.dumps({"channel": "general", "user": "ava", "content": "Dummy security smoke message"}).encode(),
                headers={"Content-Type": "application/json"}, method="POST")
            with opener.open(request, timeout=2) as reply:
                assert reply.status == 200
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if client.poll() is not None or server.poll() is not None:
                    raise RuntimeError("client or mock crashed after READY/message dispatch")
                time.sleep(0.1)
            print("PASS: offscreen client identified to local mock and stayed alive after dummy message dispatch")
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)


if __name__ == "__main__":
    main()

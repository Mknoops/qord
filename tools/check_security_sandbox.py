"""Fail closed unless the CI process has the intended isolation controls.

This validator runs inside the sandbox before any configure/build/test command.
Only dummy writes and the reserved documentation IP range are used.
"""
import errno
import os
from pathlib import Path
import resource
import socket


allowed = {"PATH", "HOME", "QT_QPA_PLATFORM", "XDG_RUNTIME_DIR",
           "PYTHONDONTWRITEBYTECODE", "TEST_QT_MAJOR", "PWD", "SHLVL", "_", "LC_CTYPE"}
assert set(os.environ) <= allowed, "unexpected inherited environment"
assert os.environ["HOME"] == "/work/home"
assert Path("/sys").exists() is False, "unexpected host sysfs"
interfaces = Path("/proc/net/dev").read_text().splitlines()[2:]
assert all(line.split(":")[0].strip() == "lo" for line in interfaces), "external network interface"
for path in ("/src/.sandbox-write-probe", "/usr/.sandbox-write-probe"):
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except OSError as error:
        assert error.errno in (errno.EROFS, errno.EACCES), "unexpected write denial"
    else:
        os.close(fd)
        os.unlink(path)
        raise AssertionError("source/toolchain is writable")
probe = Path("/work/.sandbox-write-probe")
probe.write_text("dummy")
probe.unlink()
with socket.socket() as client:
    client.settimeout(1)
    try:
        client.connect(("192.0.2.1", 9))
    except OSError as error:
        assert error.errno in (errno.ENETUNREACH, errno.EHOSTUNREACH), "external route exists"
    else:
        raise AssertionError("external network is reachable")
for limit in (resource.RLIMIT_AS, resource.RLIMIT_CPU, resource.RLIMIT_NPROC, resource.RLIMIT_FSIZE):
    assert resource.getrlimit(limit)[1] != resource.RLIM_INFINITY, "missing resource limit"
print("Isolation verified: no external route, allowlisted environment, read-only source/toolchain, bounded resources")

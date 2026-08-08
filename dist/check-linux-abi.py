#!/usr/bin/env python3
"""Guard: verify the Linux release binaries load on the oldest distro we support.

An ELF records, per shared dependency, the *symbol version* it needs
(`.gnu.version_r`). A binary built on Ubuntu 24.04 records GLIBC_2.38 and
GLIBCXX_3.4.31 because the toolchain there resolves e.g. sscanf to
`__isoc23_sscanf@GLIBC_2.38`. The dynamic loader on an older distro cannot
satisfy that and REAPER's dlopen fails with

    version `GLIBCXX_3.4.31' not found (required by reaper_rea-sixty.so)

which is what MX Linux 23 users hit on v0.4.4 (glibc 2.36 / GLIBCXX 3.4.30).
Nothing in the build fails on the build machine — the mismatch only exists on
the user's box, so it needs a check at packaging time.

The floor below is the *build baseline*: CI builds the Linux .so in an
ubuntu:22.04 container (see .github/workflows/build.yml), whose glibc is 2.35.
Every distro at or above that resolves the symbols:

    Ubuntu 22.04  glibc 2.35   <- the baseline itself
    Debian 12     glibc 2.36
    MX Linux 23   glibc 2.36   (Debian 12 based)
    Ubuntu 24.04  glibc 2.39

libstdc++ is linked statically (-static-libstdc++ -static-libgcc), so a
correct build has no GLIBCXX/CXXABI requirement at all; the limits below stay
as a backstop in case that flag ever gets dropped.

Usage:  python3 dist/check-linux-abi.py <elf> [<elf> ...]
Exit 0 = every requirement is within the floor, 1 = would break users.
"""

import collections
import re
import struct
import sys

# Highest symbol version an end user's system libraries are guaranteed to
# provide. Raising these means dropping distros — do it deliberately, and
# change the CI container in the same commit.
MAX_VERSION = {
    "GLIBC": (2, 35),  # Ubuntu 22.04 LTS
    "GLIBCXX": (3, 4, 30),  # GCC 12 runtime (Ubuntu 22.04 / Debian 12)
    "CXXABI": (1, 3, 13),  # ditto
    "GCC": (3, 0),  # libgcc_s baseline, unchanged since forever
    "LIBUDEV": (183,),  # systemd/udev, ancient
}


def read_version_needs(path):
    """{needed_soname: {version_string, ...}} from .gnu.version_r."""
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise ValueError(f"{path}: not a 64-bit ELF")

    (e_shoff,) = struct.unpack_from("<Q", data, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3A)

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, _typ, _flags, _addr, s_off, s_size, link, info, _al, _es = struct.unpack_from(
            "<IIQQQQIIQQ", data, off
        )
        sections.append(dict(name=name, off=s_off, size=s_size, link=link, info=info))

    shstr = sections[e_shstrndx]

    def section_name(sec):
        raw = data[shstr["off"] + sec["name"] :]
        return raw[: raw.index(b"\0")].decode()

    by_name = {section_name(s): s for s in sections}
    verneed = by_name.get(".gnu.version_r")
    if verneed is None:
        return {}

    strtab = sections[verneed["link"]]

    def string_at(off):
        raw = data[strtab["off"] + off :]
        return raw[: raw.index(b"\0")].decode()

    needs = collections.defaultdict(set)
    off = verneed["off"]
    for _ in range(verneed["info"]):
        _version, aux_count, file_off, aux_off, next_off = struct.unpack_from(
            "<HHIII", data, off
        )
        soname = string_at(file_off)
        aux = off + aux_off
        for _ in range(aux_count):
            _hash, _flags, _other, name_off, aux_next = struct.unpack_from(
                "<IHHII", data, aux
            )
            needs[soname].add(string_at(name_off))
            aux += aux_next
        off += next_off
    return needs


def split_version(version_string):
    """'GLIBCXX_3.4.31' -> ('GLIBCXX', (3, 4, 31))"""
    tag, _, rest = version_string.rpartition("_")
    if not tag:
        return version_string, ()
    return tag, tuple(int(n) for n in re.findall(r"\d+", rest))


def main(paths):
    if not paths:
        print("usage: check-linux-abi.py <elf> [<elf> ...]", file=sys.stderr)
        return 2

    failures = []
    for path in paths:
        needs = read_version_needs(path)
        print(f"==> {path}")
        for soname in sorted(needs):
            versions = sorted(needs[soname], key=lambda v: split_version(v)[1])
            print(f"    {soname}: {', '.join(versions)}")
            for version in versions:
                tag, parts = split_version(version)
                limit = MAX_VERSION.get(tag)
                if limit is None:
                    failures.append(f"{path}: unknown version tag {version} ({soname})")
                elif parts > limit:
                    ceiling = ".".join(str(n) for n in limit)
                    failures.append(
                        f"{path}: needs {version} from {soname} "
                        f"— above the {tag}_{ceiling} floor"
                    )

    if failures:
        print("")
        print("ERROR: these binaries will fail to load on our oldest supported distro:")
        for line in failures:
            print(f"  - {line}")
        print("")
        print("  Build the Linux artifacts on the baseline image, not on the host:")
        print("    the CI job 'linux-x86_64' runs inside container ubuntu:22.04.")
        print("  Re-download that run's artifacts instead of building on a newer box.")
        return 1

    print("")
    print("==> ABI floor OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

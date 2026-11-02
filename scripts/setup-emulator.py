#!/usr/bin/env python3
"""Prepare the official x86-64 InkView runtime privately; install no packages."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.error
import urllib.request


PROJECT = Path(__file__).resolve().parents[1]
RUNTIME = PROJECT / ".sdk/emulator-runtime"
WORK = PROJECT / "local/emulator"
ARCHIVE = "https://archive.debian.org/debian/"
# The current SDK omits theme/keyboard data. Fetch these five data files from
# this pinned public SDK mirror; never ship them in the application archive.
RESOURCE_COMMIT = "64e9fa210dd90569f8eabea92da4d40f3d072fc1"
RESOURCE_URL = f"https://raw.githubusercontent.com/blchinezu/pocketbook-sdk/{RESOURCE_COMMIT}/"
RESOURCES = (
    ("system/language/en.txt", "e739fc648fd143f624f95125ab820944e0bf676a190af6bb3b05299c687512f0"),
    ("system/language/keyboard/.entext.kbd", "c81705a1b0b89127cf5e4b6a5fe226d4bc893b6b4e1a90bd732b7215d9f58b91"),
    ("system/language/keyboard/.numeric.kbd", "f67ec8bceadb94ea239db035c667cd9c03bfa176865095087f7168fc0c14933b"),
    ("system/language/keyboard/EN.kbd", "a4ea008c16c7db9d8c1bd3fdb06d1c657cce9b0fbe0ce9fb9ae8237560479dba"),
    ("system/themes/default.pbt", "5d87446d8cbc0c8d27058b8d1c96b03118d48be5d5e35cf644f4c0e5fc9f1e6f"),
)
# Filename and SHA256 fields from the publisher's Stretch amd64 Packages index:
# https://archive.debian.org/debian/dists/stretch/main/binary-amd64/Packages.xz
PACKAGES = (
    ("pool/main/c/curl/libcurl3_7.52.1-5+deb9u10_amd64.deb",
     "e8327c7c91b610141116fd4a5c67f8d13fba8024f18638f0196216d66098e6a7"),
    ("pool/main/i/icu/libicu57_57.1-6+deb9u4_amd64.deb",
     "11152f642bef2c9ee5386a9a4573fc00bafb62cafe1ec1e925974ac1a58d5296"),
    ("pool/main/libj/libjpeg-turbo/libjpeg62-turbo_1.5.1-2_amd64.deb",
     "55b4208bca9e772cd3d6e6a3f6bf3949d170e6da77e53b0ba59abb8f1658bb64"),
    ("pool/main/o/openldap/libldap-2.4-2_2.4.44+dfsg-5+deb9u4_amd64.deb",
     "50ad398d8b882b25042b2bdc8e941573e8e9b161141227f4d58d16ccb11363a9"),
    ("pool/main/o/openssl1.0/libssl1.0.2_1.0.2u-1~deb9u1_amd64.deb",
     "a208d375182830bdcad42c0b92154d55233d7cfe4a7543015d6a99c5086416c0"),
    ("pool/main/t/tiff/libtiff5_4.0.8-2+deb9u5_amd64.deb",
     "71abb8266d744718db819a476a6f469a242c294a446b36724f3745f2ea673b2e"),
)
REQUIRED_LIBS = (
    "libssl.so.1.0.2", "libcrypto.so.1.0.2", "libcurl.so.4",
    "libjpeg.so.62", "libtiff.so.5", "libicuuc.so.57",
    "libicui18n.so.57", "libicuio.so.57", "libicudata.so.57",
    "libldap_r-2.4.so.2", "liblber-2.4.so.2",
)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def check_x86_64(path):
    with path.open("rb") as source:
        header = source.read(20)
    if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        raise RuntimeError(f"Expected an x86-64 ELF library: {path}")


def cached_download(target, url, checksum, offline):
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        if offline:
            raise RuntimeError(f"Not cached: {target.name}; run setup without --offline once")
        temporary = target.with_suffix(target.suffix + ".part")
        print(f"Downloading {target.name}", flush=True)
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                with temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
            if digest(temporary) != checksum:
                raise RuntimeError(f"SHA256 mismatch: {target.name}")
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)
    if digest(target) != checksum:
        raise RuntimeError(f"SHA256 mismatch: {target}; remove the corrupt cache file to retry")
    return target


def get_package(filename, checksum, offline):
    return cached_download(RUNTIME / "downloads" / Path(filename).name,
                           ARCHIVE + filename, checksum, offline)


def prepare_resources(sdk, font):
    system = WORK / "system"
    for folder in (
        "config", "state", "fonts", "themes", "resources", "language/keyboard",
        "tmp/fonts", "var/run", "mnt/ext1", "mnt/ext2", "mnt/secure",
        "profiles/default/config", "profiles/default/state", "profiles/default/favorite",
    ):
        (system / folder).mkdir(parents=True, exist_ok=True)
    config = system / "config/hwconfig"
    if not config.exists():
        shutil.copyfile(sdk / "usr/local/share/config/hwconfig_700", config)
    global_config = system / "config/global.cfg"
    if not global_config.exists():
        # The SDK's own config writer produced this Era 700 display selection.
        global_config.write_text("emu_resmode=15\n\n#bd7c6e65\n\n", encoding="ascii")

    if font is None:
        candidates = (
            Path("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"),
            Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
        )
        font = next((candidate for candidate in candidates if candidate.is_file()), None)
    if font is None or not font.is_file():
        raise RuntimeError("No host Liberation Sans font; supply --font /path/to/a/sans-font.ttf")
    # InkView resolves OpenFont("LiberationSans") by file basename, not fontconfig.
    for name in ("LiberationSans.ttf", "default.ttf"):
        target = system / "fonts" / name
        if not target.exists():
            shutil.copyfile(font, target)
    for name in ("LiberationSans-Bold.ttf", "LiberationSans-Italic.ttf", "LiberationSans-BoldItalic.ttf"):
        source = font.parent / name
        target = system / "fonts" / name
        if source.is_file() and not target.exists():
            shutil.copyfile(source, target)
    reader = WORK / "reader/Go"
    reader.mkdir(parents=True, exist_ok=True)
    sample = reader / "first-steps.sgf"
    if not sample.exists():
        shutil.copyfile(PROJECT / "examples/first-steps.sgf", sample)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path,
                        default=Path(os.environ.get("POCKETBOOK_SDK", PROJECT / ".sdk/SDK-B288")))
    parser.add_argument("--offline", action="store_true", help="Require already cached Debian packages")
    parser.add_argument("--font", type=Path, help="Host sans-serif TTF to use if Liberation Sans is absent")
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "amd64"):
        parser.error("This desktop SDK runtime requires x86-64 Linux")
    sdk = args.sdk_root.resolve()
    library = sdk / "usr/local/lib/libinkview.so"
    if not library.is_file():
        parser.error("B288 SDK is absent; run bash scripts/setup-sdk.sh first")
    for tool in ("dpkg-deb", "ldd"):
        if not shutil.which(tool):
            parser.error(f"Required host tool is absent: {tool}")
    try:
        check_x86_64(library)
        check_x86_64(sdk / "usr/local/lib/libhwconfig.so")
        (RUNTIME / "downloads").mkdir(parents=True, exist_ok=True)
        root = RUNTIME / "root"
        libs = root / "usr/lib/x86_64-linux-gnu"
        manifest = {filename: checksum for filename, checksum in PACKAGES}
        stamp = RUNTIME / "extracted.json"
        extracted = json.loads(stamp.read_text()) if stamp.is_file() else None
        extract = extracted != manifest or not all((libs / name).is_file() for name in REQUIRED_LIBS)
        for filename, checksum in PACKAGES:
            package = get_package(filename, checksum, args.offline)
            if extract:
                subprocess.run(["dpkg-deb", "-x", str(package), str(root)], check=True)
        stamp.write_text(json.dumps(manifest, indent=2) + "\n")
        for soname in REQUIRED_LIBS:
            check_x86_64(libs / soname)
        # In particular, never include SDK/usr/lib: it contains an obsolete C++ runtime.
        environment = dict(os.environ, LD_LIBRARY_PATH=f"{libs}:{sdk / 'usr/local/lib'}")
        linked = subprocess.run(["ldd", str(library)], env=environment,
                                capture_output=True, text=True)
        output = linked.stdout + linked.stderr
        if linked.returncode or "not found" in output:
            raise RuntimeError("Unresolved host runtime dependencies:\n" + output)
        WORK.mkdir(parents=True, exist_ok=True)
        (WORK / "runtime-ldd.txt").write_text(output)
        prepare_resources(sdk, args.font)
        for name, checksum in RESOURCES:
            source = cached_download(RUNTIME / "resources" / name,
                                     RESOURCE_URL + name, checksum, args.offline)
            target = WORK / name
            # Preserve local emulator customisations on repeated setup.
            if not target.exists():
                shutil.copyfile(source, target)
    except (OSError, RuntimeError, subprocess.CalledProcessError, urllib.error.URLError) as error:
        parser.exit(1, f"Emulator setup failed: {error}\n")
    print(f"Official x86-64 InkView runtime ready: {RUNTIME}")
    print(f"Synthetic reader and SDK resources: {WORK}")


if __name__ == "__main__":
    main()

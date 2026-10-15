import hashlib
import os
import re
import shutil
import sys
import tarfile
import urllib.request

PIN_FILE = "tuner-version.txt"
SIBLING_BUNDLE = os.path.join("..", "canshift-tuner", "dist-tuner")
RELEASE_URL = (
    "https://github.com/CANShift/canshift-tuner/releases/download/{tag}/{asset}"
)
ASSET_NAME = "canshift-tuner-dash-{tag}.tar.gz"
CACHE_DIR = os.path.join(".pio", "tuner-cache")
UNPINNED = "none"
TAG_PATTERN = re.compile(r"^v\d+\.\d+\.\d+$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ENTRY_FILE = "index.html.gz"
MOUNT_PREFIX = "/tuner/"
PATH_CAP_HEADER = os.path.join("include", "workshop_mode_rs.h")
PATH_CAP_PATTERN = re.compile(r"constexpr\s+int\s+kWorkshopTunerPathCap\s*=\s*(\d+)")
DOWNLOAD_TIMEOUT_S = 60
HASH_BLOCK_BYTES = 1 << 16


class BundleError(Exception):
    pass


def log(message):
    print(f"[tuner] {message}")


def read_pin(project_dir):
    values = {}
    with open(os.path.join(project_dir, PIN_FILE), "r", encoding="utf-8") as handle:
        for line in handle:
            key, _, value = line.strip().partition("=")
            values[key] = value
    return values.get("tag", UNPINNED), values.get("sha256", UNPINNED)


def validate_pin(tag, sha256):
    if not TAG_PATTERN.match(tag):
        raise BundleError(f"{PIN_FILE}: tag '{tag}' is not vX.Y.Z")
    if not SHA256_PATTERN.match(sha256):
        raise BundleError(
            f"{PIN_FILE}: sha256 for {tag} is not 64 lowercase hex characters"
        )


def name_cap(project_dir):
    with open(
        os.path.join(project_dir, PATH_CAP_HEADER), "r", encoding="utf-8"
    ) as handle:
        match = PATH_CAP_PATTERN.search(handle.read())
    if match is None:
        raise BundleError(f"kWorkshopTunerPathCap not found in {PATH_CAP_HEADER}")
    return int(match.group(1)) - 1 - len(MOUNT_PREFIX)


def check_names(names, project_dir):
    cap = name_cap(project_dir)
    too_long = [name for name in names if len(name) > cap]
    if too_long:
        raise BundleError(
            f"names longer than {cap} characters do not fit SPIFFS: {too_long}"
        )
    if ENTRY_FILE not in names:
        raise BundleError(f"bundle has no {ENTRY_FILE}")


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(HASH_BLOCK_BYTES), b""):
            digest.update(block)
    return digest.hexdigest()


def download(tag, destination):
    asset = ASSET_NAME.format(tag=tag)
    url = RELEASE_URL.format(tag=tag, asset=asset)
    log(f"downloading {url}")
    partial = destination + ".part"
    with (
        urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT_S) as response,
        open(partial, "wb") as out,
    ):
        shutil.copyfileobj(response, out)
    os.replace(partial, destination)


def cached_archive(project_dir, tag, sha256):
    cache = os.path.join(project_dir, CACHE_DIR)
    os.makedirs(cache, exist_ok=True)
    archive = os.path.join(cache, ASSET_NAME.format(tag=tag))
    if not os.path.exists(archive) or file_sha256(archive) != sha256:
        download(tag, archive)
    actual = file_sha256(archive)
    if actual != sha256:
        os.remove(archive)
        raise BundleError(
            f"{ASSET_NAME.format(tag=tag)} sha256 is {actual}, pinned {sha256}"
        )
    return archive


def flat_members(archive):
    members = [member for member in archive.getmembers() if not member.isdir()]
    for member in members:
        name = member.name[2:] if member.name.startswith("./") else member.name
        if not member.isfile() or "/" in name or name.startswith("."):
            raise BundleError(f"archive entry '{member.name}' is not a flat file")
        member.name = name
    return members


def unpack(archive_path, target, project_dir):
    with tarfile.open(archive_path, "r:gz") as archive:
        members = flat_members(archive)
        check_names([member.name for member in members], project_dir)
        reset(target)
        for member in members:
            with (
                archive.extractfile(member) as source,
                open(os.path.join(target, member.name), "wb") as out,
            ):
                shutil.copyfileobj(source, out)
    return len(members)


def reset(target):
    shutil.rmtree(target, ignore_errors=True)
    os.makedirs(target)


def copy_sibling(sibling, target, project_dir):
    names = sorted(os.listdir(sibling))
    check_names(names, project_dir)
    reset(target)
    for name in names:
        shutil.copyfile(os.path.join(sibling, name), os.path.join(target, name))
    return len(names)


def fill(project_dir, target):
    sibling = os.path.normpath(os.path.join(project_dir, SIBLING_BUNDLE))
    if os.path.isfile(os.path.join(sibling, ENTRY_FILE)):
        log(
            f"using the sibling bundle {sibling} ({copy_sibling(sibling, target, project_dir)} files)"
        )
        return
    tag, sha256 = read_pin(project_dir)
    if tag == UNPINNED:
        log(f"no tuner release pinned in {PIN_FILE} — keeping the probe page")
        return
    validate_pin(tag, sha256)
    count = unpack(cached_archive(project_dir, tag, sha256), target, project_dir)
    log(f"unpacked tuner {tag} ({count} files)")


def fill_or_exit(project_dir, target):
    try:
        fill(project_dir, target)
    except (BundleError, OSError, tarfile.TarError) as err:
        sys.stderr.write(f"[tuner] ERROR: {err}\n")
        sys.exit(1)

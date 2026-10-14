import os
import shutil
import sys

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")  # noqa: F821

sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "scripts"))  # noqa: F821

import tuner_bundle  # noqa: E402

WORKSHOP_FLAG = "APP_WORKSHOP_ENABLED=1"
TUNER_DIR = "tuner"
WORKSHOP_ONLY = (TUNER_DIR,)
FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}
STAGE_DIR_NAME = "spiffs_data"


def workshop_enabled():
    flags = env.GetProjectOption("build_flags", "")  # noqa: F821
    text = flags if isinstance(flags, str) else " ".join(flags)
    return WORKSHOP_FLAG in text


def skip_workshop_only(source):
    def ignore(directory, names):
        if os.path.abspath(directory) != source:
            return []
        return [name for name in names if name in WORKSHOP_ONLY]

    return ignore


def stage():
    source = os.path.abspath(env.subst("$PROJECT_DATA_DIR"))  # noqa: F821
    target = os.path.join(env.subst("$BUILD_DIR"), STAGE_DIR_NAME)  # noqa: F821
    shutil.rmtree(target, ignore_errors=True)
    ignore = None if workshop_enabled() else skip_workshop_only(source)
    shutil.copytree(source, target, ignore=ignore)
    if ignore is None:
        tuner_bundle.fill_or_exit(env["PROJECT_DIR"], os.path.join(target, TUNER_DIR))  # noqa: F821
    env.Replace(PROJECT_DATA_DIR=target)  # noqa: F821
    print(f"[fs] staged {source} -> {target} (workshop files {'kept' if ignore is None else 'dropped'})")


if FS_TARGETS & set(COMMAND_LINE_TARGETS):
    stage()

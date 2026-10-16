"""This build proves C3 compatibility, not Maker Band wiring or flash layout."""
Import("env")
from SCons.Script import COMMAND_LINE_TARGETS

if set(COMMAND_LINE_TARGETS) & {"upload", "uploadfs", "uploadfsota", "erase", "program"}:
    raise RuntimeError(
        "hackerband_core is compile-only; establish Maker Band recovery, "
        "pinout and partition layout before enabling hardware uploads."
    )

def refuse_upload(source, target, env):
    raise RuntimeError(
        "hackerband_core is compile-only: Maker Band recovery, pinout and "
        "partition layout are unverified. Do not flash its generic image."
    )

env.AddPreAction("upload", refuse_upload)
env.AddPreAction("uploadfs", refuse_upload)
env.AddPreAction("erase", refuse_upload)

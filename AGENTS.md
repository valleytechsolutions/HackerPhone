# HackerPhone development

This is an independent MIT project for the owner's CircuitMess MakerPhone 2 / MP24
engineering sample. Read README.md, docs/MP24-HARDWARE.md and docs/VALIDATION.md before
changing the hardware adapter. Keep GPL feature references separate from original code.

Use the pinned pioarduino Core and dependencies in requirements-dev.txt. Run
`python tools/test_core.py` for portable service changes and
`python -m platformio run -e mp24_sample` for firmware changes.

The sample's existing partition layout must be preserved. Development writes only the
application at 0x10000 after matching the connected chip and checking the private recovery
backup. Do not flash the generated factory image, generic bootloader or partition table.
Do not erase the device or format its SD card as a routine build step.

Recovery images, extracted vendor firmware, nearby-device captures and local diagnostics
stay in ignored .work/ or private/. Never stage or upload those files. Hardware tests
must distinguish measured results from unfinished features; do not claim complete
Marauder parity or validated calling until those paths are implemented and tested.

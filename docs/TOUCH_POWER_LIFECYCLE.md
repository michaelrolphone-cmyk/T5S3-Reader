# Optional touch power lifecycle

`RiscTouchPowerV1.h` adds a tagged, versioned, size-checked suffix after the
unchanged `input.touch.raw@1` API. A single loaded provider still publishes a
single capability; clients discover the extension with `risc_touch_power`.
Legacy providers return no extension. The prefix header is byte-unchanged.

Both calls use the existing context and owner executor. Budgets range from
1 to 1000 ms; zero only polls established state and does no hardware work.
Prepare rejects live subscribers without disrupting their input. Once admitted,
normal input is fenced. Preparation and recovery retain exact hardware tokens
and stages through timeouts and ordinary platform refusals. A successful resume
restores admission with a new neutral-input gate and no stale queue contents.
Held contacts and Home cannot activate the newly subscribed consumer: a fresh,
valid, acknowledged all-neutral controller report must precede ordinary edges.

A retained error is terminal for this loaded instance: preserve code,
dependencies and tokens, and do not retry I/O or unload. An ordinary refusal
with known resource custody may be retried or recovered. Successful preparation
is not system deep-sleep entry, a permission to release the grant, or RAM
retention. MCU reset takes the ordinary startup path.

The X4 provider separately owns board GPIO sequencing, declared I2C addresses,
finite transfer and sleep bounds, cleanup and electrical qualification. The
suffix adds no raw/native import, second capability, or platform-specific field.

Validation: `bash test/run_touch_power_test.sh` compiles the same contract test
as C11 and C++17 with ASan/UBSan. It checks legacy-prefix rejection, extension
layout, all truncated sizes, wrong versions/tags and missing callbacks.

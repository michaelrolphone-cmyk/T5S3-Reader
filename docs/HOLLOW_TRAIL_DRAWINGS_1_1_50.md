# The child's drawing and the older card

The existing third glasshouse evidence point now has two physical papers on its
actual supporting wall. Player input lifts the child's picture, moves it beside
the older card, takes out the five-legged railway dog, sets it alongside the
pictures, puts it away without identifying an author, and returns the drawing.
The older card remains on the wall. The held drawing retains the same small
marks as the hanging sheet; the hand and railway paper share one moving contact.

The large held insert distinguishes the child's huge lantern, uneven six-person
spacing, taller redrawn second figure and rubbed shorter ghost, extra fingers,
smallest person leaning into the skirt, unequal feet and short-sided scarf. The
older card keeps regular figures and feet on a printed line. No calendar date
or identification is invented. The complete original drawing passage is retained
alongside the original partition passage and useful mirror instructions.

The original route, evidence page 20, mirror settings/receivers, upper-vent and
lower-latch solution, submitted-frame/read guard and ending guard stay intact.
Left/right comparison is reversible and bounded. Quiet holds wait for input;
all nine stages support pause, notes, input loss, neutral-gated recovery,
cancellation/replay and host exit through both HID and XInput.

## Actual evidence and validation

[Nine actual comparisons](images/hollow-trail-glasshouse-drawings/README.md) show
unscaled 960×540 grayscale and packed monochrome. Baseline source `a95ee01ff00c`
and final drawing source `f5668799dfbc` are both cumulative unreleased 1.1.50.
Captions name the actual captured revisions. All source hashes match the
integrated app; each comparison preserves its original rasters byte-for-byte.

Visual review caught two figures leaving the page because their horizontal
positions exceeded signed 8-bit range. The positions now use 16-bit constants,
and a pixel regression verifies both figures. The right hand now follows the
railway paper's corner through placement and withdrawal. The hand/paper and
body limb checks cover every transition tick.

Focused ASan/UBSan controls/contact tests, exact original-prose checks and strict
C++ math pass. All 30 production-renderer gray/mono reference pairs remain
unchanged relative to the counts checkpoint. The complete final native-app
aggregate is running at this checkpoint. S3 ELF/package validation is recorded
with the published PR. Leak detection is disabled only for the runner ptrace
limitation; address/undefined checks remain enabled.

The cumulative app version remains **1.1.50**, minimum firmware **1.3.37**.
Night rest, food/drink and evening tending remain separate chapter gaps.
This is host software verification, not device-panel/FPS or release qualification.

# Hollow Trail: two accounts, one history

This is the implemented narrative and decision design, updated through Hollow
Trail 1.1.5. The cabinet narrative originated in PR #276. It supersedes the earlier
straightforward rescue/reunion synopsis. **Full spoilers below.**

## Narrative contract

The sister describes an evacuation. The keeper describes a controlled transfer
through a signal network that should have stayed closed. They agree on useful
operating facts and disagree about consent, causation and purpose. Neither is
an omniscient voice. Both sometimes protect people; both omit inconvenient facts.
The player can read the sister as coerced, manipulated, or anticipating and
organising events. Handwriting identifies an apparent source, not guaranteed
authorship or consent. On-trail narration and journal recaps record observations
and alternatives, not a narrator-certified account of her intentions.

Thirty documents use attributed passages from both people, arranged as sister,
keeper and juxtaposed records within each chapter. All three contain operational
clues. The same puzzle settings work under either interpretation; there is no
hidden belief score, random truth selection or punishment for believing one
person. Versions 1.1.4–1.1.5 replace recipe-style operating instructions with physical
mechanism clues and setting-specific routes; see HOLLOW_TRAIL.md for the
current camera-mood and discovery-triggered weather tables, and
HOLLOW_TRAIL_PUZZLES.md for the linked mechanical deductions. The
competing accounts and cabinet outcomes remain consistent.

## Evidence and reversals

| Chapter | Shared observation | Rescue reading | Control reading | Later connection |
| --- | --- | --- | --- | --- |
| Forest | Seven names, an older route and a private signal | She leaves a way to follow | The group has been admitted to an existing system | The keeper already knows the warning variant |
| City | A pre-positioned ladder, cancelled light and repeated signatures | Prepared escape and shared watches | Prescribed route and manufactured attendance | An identical blot recurs in the garden and final pen |
| Oil fields | Her ration disappears; an eighth person is budgeted | Sacrifice and hope for a follower | Conditional supplies and an expected arrival | Her plan may include the player's response |
| Railyard | Cancellation predates collapse; she corrects a map | Foresight and mutual help | Engineered dependency or undisclosed instructions | The keeper cannot explain her knowledge |
| Marsh | A permit, two days waiting, matching names on opposite banks | Passage secured for an injured traveller | A delayed transfer under orders | Release and receipt describe the same crossing |
| Quarry | Accurate load setting and a correction from six to seven | A capable guide who forgets herself | A counted subject who knows too much | Knowledge and care do not establish freedom |
| Gardens | Locked shelter, repeated correction and an older drawing | Protection, continuity and a children's promise | Confinement, copies and a rehearsed role | A correction can be reproduced mechanically |
| Dam | Lamp and recorder share power; original/live faces interlock | Messages can survive her departure | Her voice can be used without her | Both final mechanisms exist before the choice |
| High pass | Her mark on a pre-lock tracing; automatic return pulses | A plan and a lasting guide | Advance orders and false reassurance | A light answering does not prove presence |
| Last light | Inward circuit arrows, day-one seal and day-four locks | A system deliberately left for the player | A mechanism directing the player | Final action exposes one independently testable face |

The warning is seeded in the forest: three short flashes mean follow; two short
and one long mean someone is watching. The keeper knows both. It recurs as
knocks, corrected notation and finally the altered signal. It is neither a new
last-minute code nor conclusive proof of a live author.

## Final tower interaction

After the three-flash chapter puzzle, the glass cabinet appears at the exit.
The player must be grounded near it and press A / device Confirm. Merely solving the
puzzle or jumping past the cabinet cannot finish the chapter.

Left/Right selects **Break the circuit** or **Complete the circuit**. A/Confirm
opens a separate confirmation screen; another fresh A/Confirm commits. X/Back or Select first cancels confirmation, then leaves the decision so the player
can revisit the journal. Changing the selected action cancels confirmation.
Commit requires the current confirmation revision to have been submitted to the
display, preventing an old prepared screen from authorising a new selection.
Held A cannot traverse both steps. Start leaves reading without committing. There is no timed choice.

| Choice | Physical result | Changed final testimony | Belief undermined |
| --- | --- | --- | --- |
| Break | Power loss releases the sealed original and shutters the live pen | Her pre-lock instructions anticipate her sibling breaking the feed to release her | She was merely carried through events and the player is acting outside the plan |
| Complete | Power runs the writing arm and seals the original away | The pen reproduces her distinctive correction and a personal arrival message | Familiar handwriting proves her presence, freedom or current intention |

Both texts end with the childhood warning variant. In one it was already on
the original; in the other the pen writes it. The player has proved something
about the machinery and their own participation, without obtaining a convenient
certificate of her innocence or guilt. A pre-lock plan does not prove voluntary
authorship. A copying machine does not prove every earlier letter was forged.

The causal history is fixed: the master predates the holding locks, the keeper
fitted locks, the machine can reproduce strokes, and the two faces interlock.
The choice changes access and the last evidence item, not those prior facts.
The opposite result is not a random surprise selected to contradict a belief
score. Future story additions must preserve this single underlying history.

## Journal, completion and replay

Committing updates document 30, **The testimony under glass**, to the matching
changed testimony and opens it at page one. The chosen result is also added to
**Witnessed endings**. The other result remains absent until actually chosen on
a later journey. The original 30 documents and up to ten reached recaps plus two
endings fit a bounded 42-record index.

Left/Right pages through the result in the configured reader font. On its final
page, A/Confirm acknowledges it and returns to the trail. X can return to the
journal sooner, but does not unlock the exit. Reopening the cabinet returns to
the result; it cannot change the committed action. Walking through the exit
after acknowledgment starts the forest again. Death retains the committed
choice; a new journey clears the current choice and retains witnessed endings
and the latest changed testimony for this app session. No SD save/resume is added.

If the reader capability fails, an explicitly labelled compact page preserves
the full ASCII text and pagination using the game's built-in face. A decision
page that cannot fit at the configured font size uses the same labelled compact
view so its complete consequences and controls are visible. Normal reading
continues to use the leased `reader.typography` capability. No font internals or
new firmware API are imported.

## Performance and validation

The cabinet is a small foreground overlay. Decisions and journal reading freeze
physics and reuse the existing reading bitmap and 4 KiB content buffer. Four
bytes of game state track current choice, latest result, archive flags and
acknowledgment. No new framebuffers, cache invalidations or per-frame narrative
processing are introduced. Full-row reading entry/restoration retains the
existing fast-video submission path.

Production-input tests exercise both outcomes, cancellation, fresh-press and
submitted-revision guards, changed text, hidden opposite endings, pagination,
acknowledgment, death retention, loop reset and reader-service failure. The
route test traverses all ten chapters and now performs a final decision before
restarting. The Xtensa ELF build and native-app host suite remain the build
checks; they do not establish physical display performance or player reactions.

Version remains cumulative 1.0.15 (master and published app 1.0.14); the existing
capability firmware change remains 1.3.35 (master/published 1.3.34). This story
extension makes no further firmware changes and does not publish a release.

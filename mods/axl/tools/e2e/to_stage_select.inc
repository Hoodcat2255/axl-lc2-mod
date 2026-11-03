# (included by scenarios) the intro ended by its stage-end flag, the Hunter base's save prompt cancelled (A),
# COMMAND -> STAGE SELECT SCREEN, Isoc's cutscene skipped (Space), the briefing paged (Z) until a stage is picked
# (stage select = stage 13; the picked stage's number is set at once), then PLAYER SELECT (it starts on AXL since 2026-10-29)
wait free == 1 9000
set 4B 1
talk 6000 stage == 15
idle 500
press 4 A
idle 120
press 4 DOWN
idle 30
press 4 Z
wait stage == 11 600
talk 4000 stage == 13
talk 9000 stage != 13
idle 150

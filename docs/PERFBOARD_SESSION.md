# Perfboard session — receiver #1 (planned 2026-10-23)

Layout: `../layout/side_layout.png` (matches the actual board:
D0–D6 on one edge, 5V/GND/3V3/D7–D10 on the other).
State going in: XIAO flashed + paired to the COM3 central,
LINK PASS on the breadboard, side switch breadboard-verified.
Pairing lives in the XIAO's NVS — nothing to redo.

## Build order

1. Trim a 5×7 cm perfboard to 22×16 holes (~55×40 mm), deburr.
2. Female sockets first: 2× 1×7 at cols 8/14, solder, then all
   the top-side parts: 220 Ω ×3, RGB LED, 12 mm tact, both
   SS12D00 switches (PWR at left edge, SIDE at right edge).
   RGB LED: bend the leads 90° (bend starts ~3 mm from the
   epoxy, not flush) so the body lies parallel to the board at
   its end, dome pointing out past the end edge — the unit
   hangs from the reel-end ring with that end down, and the
   diffused body then glows visibly from every side. Glue the
   body to the board edge after the electrical test (step 6)
   for strain relief — a dangling unit swings.
3. Underside nets per the diagram — GND rail along row 16,
   insulated wire for long runs, clipped leads for short bridges.
   Mirror-image warning: the underside is flipped left-right.
4. B+/B− pigtails onto the XIAO's underside pads — soldered
   with ~4 cm of slack (decided 2026-10-25: no JST break; the
   slack is what lets the socketed XIAO lift out for pad
   access, and a full swap means desoldering two joints).
   Route (POWER REDESIGN 2026-11-08 — the switch is a signal
   now, NOT a power cut): cell+ → B+ pad DIRECT; cell− →
   B− pad DIRECT. PWR switch: middle → D10 pad, one outer →
   GND rail, other outer nc. The battery stays on B+/B−
   permanently so USB charging works while the board is "off".
5. Short checks BEFORE the cell: B+↔GND, B+↔B−, each GPIO net
   ↔ GND rail. Meter continuity, fix anything that beeps wrong.
6. Seat the XIAO, power from USB first: amber should show its
   status, link UP against the central, emulator hits light
   the RGB both colours, switch flips change the side live.
7. Cell LAST: foam/Kapton barrier, cell under the board, leads
   trimmed flush. Check: unit runs unplugged; PWR switch OFF
   (knob toward the GND side) → LEDs dark, console silent
   (deep sleep); switch ON → boots, re-links; USB + switch
   OFF → still charges (charge LED on) — that is the point
   of the redesign. Switch OFF is deep sleep (tens-to-hundreds
   of uA), NOT zero drain; the XIAO's onboard RST button
   still resets a wedged board.

Stop condition: any failed check in 5–7 — stop and diagnose,
don't push through to the next step.

## Power redesign (2026-11-08, IMPLEMENTED in firmware)

The PWR switch is a sleep signal, not a power cut: battery
straight to B+/B− (charging works on USB while "off"), switch
middle → D10/GPIO9, one outer → GND, other nc; LOW = OFF =
deep sleep, HIGH = ON. Wake: switch ON (ext1 — GPIO9 is an
RTC pin) or a 60 s timer tick in the auto-sleep case.
Auto-sleep: 15 min with no central packets (paired only,
never while learning/testing); a timer wake with no central
in 5 s sleeps again. The pairing button (D7/GPIO44) can NOT
wake from deep sleep — it is not an RTC pin — so it stays
pairing-only while awake. Fail-safe: a broken switch wire
reads HIGH (pull-up) = ON, so the board stays awake.

## Pin map (2026-11-08, soldered build — corrected twice)

D-numbers are what the user reads off the silkscreen; the
XIAO S3 variant maps them NON-sequentially (D6=GPIO43,
D7=GPIO44, D8=GPIO7, D9=GPIO8, D10=GPIO9). Final: button
D7/GPIO44, side switch D9/GPIO8, PWR signal D10/GPIO9,
RGB R=D1/GPIO2, G=D4/GPIO5, B=D3/GPIO4 (G/B legs soldered
swapped, compensated in firmware). The earlier "button
GPIO7 / side GPIO9" build listened on the wrong pads.

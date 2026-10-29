# Changelog

## Unreleased

### Physics grab

- **Held objects turn with the hand**: an object held in one hand keeps the angle it had when grabbed and turns as
  the wrist turns, about the point held, instead of hanging from it. Heavy objects turn more slowly. Held in both
  hands, it follows the line between them. Corpses still hang from the limb held.

### Fixes

- **Chairs and benches**: sitting down and getting up work again. The game shows the third person while the player
  sits down and gets up, as it does without VR, and comes back to the first person by itself.
- **Distant water**: water far away no longer turns grey and flat outside the middle of the view; it keeps its
  reflection across the whole field of view of the headset.
- **V.A.T.S. with the manual reload**: when a shot empties the clip in V.A.T.S., the game reloads as it does without
  VR instead of freezing on the empty clip.
- **Throwing spear**: held in the hand as the throw holds it, point forward, instead of as a cane, so it no longer
  turns around when it leaves the hand.

### Feedback

- **No rounds left**: reaching behind the back with no rounds left also plays the game's sound for a lock or terminal
  beyond the player's skill.
- **Power attack without enough action points**: holding the trigger for a charge without the action points shows the
  words V.A.T.S. writes by its action point meter when they run short ("PA Limités" in French) with the game's sound
  for missing action points.

## v0.9.0 (2026-10-25, since v0.8.0 `66809af`)

### Quick wheel, holsters and ammunition types

- **Holsters**: the weapon is put away by bringing it to its holster and pressing the right grip: one-handed weapons,
  grenades and mines at the right hip, rifles, heavy weapons and two-handed melee weapons behind the back over the
  right shoulder. The empty right hand at the holster and the right grip draw it again. A light pulse tells the hand
  is at the holster; the weapon's own sounds play. With the weapon holstered both hands are free and the trigger does
  nothing.
- **Quick wheel**: pushing the right stick down and holding it shows the hotkeys on a wheel in front of the right
  hand, with each item's icon (equipped items marked with a dot). Moving the hand towards a cell picks it (a light
  tick), letting go of the stick uses it as the game's hotkeys do (equip, unequip, use an aid item); letting go in the
  middle does nothing. A weapon chosen on the wheel comes straight into the hand, out of its holster. The right stick
  does not turn while the wheel is open. The top cell is the ammunition: with the manual reload off it swaps to the
  next type held.
- **Hotkeys from the Pip-Boy**: in the inventory, holding RB shows the game's own hotkey wheel; the left stick
  towards a slot, diagonals included, gives the highlighted item that hotkey, with the game's own sound and refusals.
  The item list does not scroll while the wheel is shown.
- **Ammunition types** (manual reload): a magazine, shells or rounds taken behind the back are of the weapon's current
  type while some are left, otherwise of the next type held. While the left hand holds them, X switches to the next
  type held (a label above the hand shows its name and count); pushing them into the weapon loads that type.

### Sneak

- **Sneak by crouching**: **Settings > VR > Sneak** chooses between the stick click (the default, as before) and
  **Real crouch**: lowering the head about 35 cm below its standing height sneaks, standing up stops sneaking, with
  the game's own sounds and rules; the stick click then no longer sneaks. The view stays at the real head's height
  (the game does not lower it a second time). The depth is `d3d9.vrCrouchDepth` in `dxvk.conf` (metres), the
  starting choice `d3d9.vrPhysicalSneak`.
- **Sneak meter**: [HIDDEN], [CAUTION], [DANGER] and [DETECTED] are shown in front of the head while sneaking, just
  below the line of sight.

### Grenades and thrown weapons

- **Lighter** (dynamite, fire bomb): it stands upright in the left fist and its cap opens when it is lit; the
  fingers no longer close through it.
- **Lit fuse**: the fuse of the dynamite and of the fire bomb sparks once lit until it leaves the hand.

### Manual reload

- **No ammunition left**: reaching behind the back for a magazine, shells or rounds with none left in the inventory
  now shows the game's "You don't have enough ammo." message in front of the head and gives a long, dull pulse on the
  left controller, unlike every other reload pulse.

### Fixes

- **Drawing a heavy weapon**: the arms and the weapon are hidden until the game has brought the right hand onto the
  weapon (the minigun and the grenade machine gun hold it still far away for a moment first), instead of showing the
  hand far in front of the controller.
- **Long HUD messages**: messages such as being overencumbered or a discovered location with a long name, and long
  objectives, are no longer cut on the right; the radiation and XP meters stop moving towards the centre where they
  would cover them.
- The console's keyboards, the power attack ring and the other images the mod draws itself are drawn by the GPU
  instead of the CPU.

## v0.8.0 (2026-10-24, since v0.7.0 `b6c9aca`)

### Grenades, mines and thrown weapons

Grenades, mines and thrown weapons are armed with the left hand and thrown with the right hand, outside V.A.T.S.
(V.A.T.S. keeps the game's own throw).

- **Arming** is done at the part of the model it acts on, with the sounds of the game's attack animation:
  - **Pins** (frag, incendiary, Nuka grenades, ...): the left grip or trigger takes the pin and pulling the hand away
    pulls it out. Letting go before puts it back; once out, the pin stays in the left hand until the grip lets go.
  - **Fuses** (dynamite, fire bomb): the lighter is held in the left hand and the left trigger strikes its flame;
    holding the flame at the fuse lights it.
  - **Buttons, switches and dials** (plasma and pulse grenades, gas bomb, time bomb): pressed with the left index
    fingertip or clicked with the left trigger near them.
  - **Mines**: their light is pressed the same way and turns on once armed.
  - Thrown weapons (throwing knives, spears, hatchets, axes) need no arming.
- **Throwing** (grenades, thrown weapons): hold the right trigger, swing and let go. The throw follows the fastest
  motion of the hand just before the release, and its strength follows the hand's speed, up to the game's full throw.
  A slow release, or an unarmed grenade, stays in the hand.
- **Placing** (mines): a press of the right trigger lets the mine go with the hand's motion, or drops it to the ground
  when the hand is still.
- The game spends the weapon, handles damage and crime and shows the next one in the hand half a second later. The fuse
  starts when the grenade leaves the hand, as in the game.
- The Pip-Boy does not open while the left hand holds a pin or a lit lighter.

### Manual reload

- **Heavy weapons**: the ammunition boxes of the minigun, CZ57 Avenger, K9000 and grenade machineguns, the shoulder
  mounted machine gun's box, the Red Glare's canister and the Tesla cannon's cell are taken out and put in like
  magazines (the Gatling laser and plasma casters already were). The box of the minigun family and of the grenade
  machineguns sits under a lock, turned open with the left hand before the box comes out and shut once a new one is
  in; the weapon does not fire while it is open. A cell the game's animation never puts in the left hand (the
  Gatling laser's) is held between the fingers.
- **Launchers** (missile launcher, Annabelle, Fat Man, Esther): after the shot the tube is empty; a missile or mini-nuke
  taken behind the shoulder is pushed into the tube.
- **Flamers, incinerators and arc welder**: no part comes out of them, so they keep the game's reload (automatic when
  empty, or X), with manual reload on or off.
- **Not yet covered**: weapons whose model has no recognised part keep the game's reload (the X button).

### Finger tracking

- The fingers of a free hand follow the player's fingers, read from SteamVR's hand skeleton: each finger bends from
  the relaxed open hand to the game's fist. This covers the left hand with one-handed weapons, both hands with no
  weapon and the Pip-Boy forearm; a hand on a weapon keeps its grip, and a hand holding a magazine or rounds keeps the
  reload grip.
- With no weapon, the right hand now sits at its controller; it orbited around it before.
- **Bindings**: two new hand skeleton actions. SteamVR may need to reload the default bindings once; players with custom
  bindings need to replace them or add the skeleton actions.

### GPU support

- GPUs without `VK_KHR_maintenance5` and `VK_KHR_maintenance6` (for example AMD Polaris, RX 470/570/580, on AMD's final
  Windows driver) can now run the mod. Patch by mrlemme606-ops.

### Fixes

- Flames (flamer, Burning Love, ...) come out of the muzzle of the weapon in the hand; they followed the head.
- The Pip-Boy no longer opens when the left forearm passes in front of the face with its screen turned away, nor while
  the left hand holds a magazine or rounds.

## v0.7.0 (2026-10-18, since `fb87675`)

### Manual reload

Weapons are now reloaded with the hands instead of the reload button. It is on by default and can be turned off in
**Settings > VR > Manual reload** (or with `d3d9.vrManualReload` in `dxvk.conf`; the in-game choice is saved as
`ManualReload` in `fnvvr/vr_options.ini` and takes precedence).

- **Controls changed.** The **left grip** now holds every weapon with both hands, firearms and melee weapons alike. The
  **left trigger** takes and handles magazines, rounds and moving parts. New ammunition is always taken **behind the
  left shoulder**. Letting go of the left trigger drops what the hand holds without losing any round.
- **X no longer reloads** the weapons that are reloaded by hand, and the automatic reload is off for them: an empty
  weapon clicks. Equipping a weapon still refills it, and ejecting a partly used magazine loses no round.
- **Detachable magazines and energy cells**: pull the magazine out of its well, take a new one behind the shoulder and
  push it in. The magazine slides along its well, the hand holds it as the game's reload animation does, and the
  game's own sounds play when it goes out and in.
- **Chambering**: a weapon with a slide, charging handle or bolt keeps a round chambered. When nothing is chambered,
  the part is drawn locked back and a new magazine does nothing until the part is pulled back with the left hand and
  released.
- **Jams**: a weapon reloaded by hand no longer plays the jam animation. It stays jammed (the trigger clicks) and its
  rack part rests partly back until it is racked; a weapon without a rack part is cleared by taking its magazine out.
  V.A.T.S. keeps the game's own jam.
- **Tube and lever weapons** (shotguns, Cowboy repeater, brush gun, ...): a handful of rounds is taken behind the
  shoulder and held between the thumb and the index finger. Held at the loading port, the rounds go in one by one at
  the rhythm and with the sounds of the game's loading animation, until the weapon is full or the hand moves away. A
  full weapon refuses rounds, and a lever weapon that fired its last round needs its lever worked.
- **Break actions** (single shotgun, sawed-off, Caravan shotgun): the left trigger near the barrel breaks the action
  open, the barrel tips down and the spent shells are thrown out. New shells, held between the fingers, are brought to
  the open breech; the left trigger there again closes it. The weapon cannot fire while it is open.
- **Revolvers**: a flick of the right wrist swings the cylinder out (throwing the cases out) and a flick the other way
  closes it. Swing-out revolvers take a speed loader from behind the shoulder. The .357 Magnum revolver and Lucky open
  their loading gate with the same flick and are loaded round by round, like the tube weapons. A revolver cannot fire
  while it is open.
- **Highlighted parts**: the part that needs working next (the empty or jammed magazine or cell, the rack part, a break
  action's barrel, the loading port while rounds are held) glows the way V.A.T.S. highlights a limb, wherever the
  hand is. Outside V.A.T.S. only; V.A.T.S. keeps its own highlight.
- **Not yet covered**: heavy weapons, launchers and weapons whose model has no recognised magazine part keep the
  game's reload (the X button) for now.

**Bindings**: a new SteamVR action, "Take out, take and insert a magazine (left hand)", is bound to the left trigger,
and the two-handed grip moved from the left trigger to the left grip. Players with custom bindings need to bind the
new action and move the grip.

### Console

- The game's console can be opened from the headset: a **Console** button in the Settings list, after VR, in game only
  (it is left out of the main menu's Settings list).
- The console is shown on the game's own **terminal**: the terminal monitor, its font and its screen effects (blur,
  scan lines, flicker), standing in 3D in front of the player at eye height, in both eyes.
- Its keyboard is the keyboard of the world's terminals: it unfolds below the monitor with the terminal's own
  animation and sounds when the console opens, and folds back when it closes. Keys are typed with the lasers, and the
  keyboard's orange light shows Shift and Caps Lock.
- Typed commands run exactly as if typed on a physical keyboard. The right stick scrolls the output; B, ESC and EXIT
  close the console.
- While the console is shown, the menu panel is hidden, the lasers no longer drive the game's cursor and the gamepad
  is at rest. A flat keyboard panel remains as a fallback.

### Camera

- The view is kept in first person: a save made in third person, an Alt+Tab or the POV control no longer leave the
  player in third person (outside V.A.T.S., while alive).

### Fixes

- The Settings list is now sized for all its entries; the sixth entry was cut off.

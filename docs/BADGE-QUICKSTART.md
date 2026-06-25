# Badge Quickstart

**Just got an OnionDAO badge? Start here.** This takes you from box to **badge
linked to your OnionDAO profile**, installing Onion OS along the way.

> **Scope.** One path only: power on → confirm/flash Onion OS → link to your
> profile. It is **not** a guide to games, the tamagonion, the book reader, or
> using the badge as a remote — those come *after* you're linked.
>
> **Time.** ~5 minutes **if Onion OS is already flashed.** Several badges aren't
> flashed out of the box — if yours isn't, budget more: the flashing step has
> real catches. See [Step 2](#2-confirm-onion-os-is-running).

**Why these steps?** The badge is a tiny computer: **hardware** (the board) runs
an **operating system** (Onion OS) that runs **apps**. Before any of that helps,
the OS has to be installed and the badge has to know who you are — that's linking.

> **Where's that control?** The repo has a full board diagram — see
> [PINOUT.md](PINOUT.md), the interactive
> [IO reference](../pcb/oniondao%20badge.html), or the
> [board render](../pcb/oniondao-badge_top.png).

<!-- 📷 the badge powered on, showing the home menu -->

## 1. Power it on

- **Plug in USB-C.** Any charger or laptop port powers it — the badge runs on USB
  alone. *(For flashing or the serial `state` check later you need a **data**
  cable, not charge-only — see [cables](#about-usb-c-cables).)*
- If your badge has a **LiPo battery**, seat it in the small 2-pin socket. Fresh
  badges sometimes ship with it **unplugged**. It only fits one way — don't force it.
- **Power on with the slide switch on the *side* of the badge** (by the screen).
  A separate button on the **back** is also labeled **ON/OFF** — that one isn't
  the power control.
  <!-- 📷 the side slide switch -->
- **E-paper is sneaky:** the screen holds its last image even when off, so a
  static picture ≠ on. When it's **on**, the screen *changes* as you press
  buttons; when you power **off**, the image stays but the buttons stop
  responding — that's how you know it's really off.

## 2. Confirm Onion OS is running

- On boot: a ~3-second **"oniondao" splash**, then a **home menu** that responds
  to buttons.
- **No splash / nothing changes? It isn't flashed yet** — the catch-heavy step.
  > ⚠️ **Flashing is the rough part.** The VS Code / ESP-IDF setup is fiddly and
  > the README doesn't cover every case — if you get stuck, ask a neighbor or the
  > desk. Follow [onion-os/README](../software/mods/onion-os/README.md)
  > (toolchain: [esp-idf-vscode-setup](../software/guides/esp-idf-vscode-setup.md)).
  > 🚫 **Never run `erase-flash`** — it deletes your badge's wallet.

## 3. Let it connect

- Onion OS auto-joins venue WiFi (**`CIC-Guest`** / `1nnovation`), announces
  itself, generates a per-badge wallet, and gets an **Onion ID**.
- To inspect: plug in USB, open a serial monitor at **115200 baud**, type
  `state` — it prints `onionId`, `hardwareId`, `wallet`, and `status` (stays
  `seen` until you link it next).

## 4. Link it to your profile  ← the step most people miss

1. Go to **<https://oniondao.dev/portal/onions>** — the **Onions** page, *not* a
   page called "badge".
2. Enter your badge's **Onion ID** and submit the link request.
3. **Approve on the badge** — press **SELECT** on the prompt.

Your onion **points** migrate to on-chain **coins**; your username + balance now
show on the home screen.

🎉 **That's it — you're linked.**
<!-- 📷 / celebration gif: home screen showing username + balance -->

## About USB-C cables

Some USB-C cables are **charge-only** and carry no data — fine for power, useless
for flashing or the serial `state` check. Quick test: plug a phone into a laptop
with it; if the laptop reacts (mount prompt, shows the device), it's a **data** cable.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Nothing on power-on | Use the **side slide switch** (the back ON/OFF button isn't power); plug in **USB-C**; reseat the LiPo. |
| Screen frozen | Tap **RESET** — labeled **RST1**, lower-left on the **back**. Safe; reboots Onion OS. Leave **BOOT** alone. <!-- 📷 finger on RST1 --> |
| "There's no OS on it" | It needs flashing — see [Step 2](#2-confirm-onion-os-is-running). Ask around; it's the catch-heavy step. |
| Can't find where to link | It's **<https://oniondao.dev/portal/onions>**. Stuck? Ask a neighbor or the desk — that's how a lot of us found it. |

---

*Found a gap or a step that tripped you up? That's exactly what's worth fixing —
PRs welcome.*

<sub>Reviewed cold on 3 badges by @San-ta-Fe — most of the fixes above came from
that pass. 🧅</sub>

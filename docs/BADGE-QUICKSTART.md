# Badge Quickstart

**Just got an OnionDAO badge? Start here.** ~5 minutes from box to linked.
All you need is the badge and a **USB-C cable**.

> This is the attendee "how do I start" guide. For the hardware deep-dive see
> [HARDWARE.md](HARDWARE.md); for building/flashing firmware see
> [`software/mods/onion-os/README.md`](../software/mods/onion-os/README.md).

## 1. Power it on

- **Plug in USB-C** (any phone charger or laptop) — the badge runs on USB alone.
- If your badge has a **LiPo battery**, make sure it's plugged into the small
  2-pin socket. A fresh badge sometimes ships with it **unplugged**, so it won't
  power on until you connect it (or run it on USB). It only fits one way — line
  it up, don't force it.
- Flip the **ON/OFF slide switch** on the back.
- **Heads-up about e-paper:** the screen keeps showing its last image even with
  the power off. A static picture does **not** mean it's on — you want a screen
  that *changes* when you press buttons.

## 2. Confirm Onion OS is running

- On boot you should see a ~3-second **"oniondao" splash**, then a **home menu**.
- If the screen never changes and there's no menu, your badge may **not be
  flashed yet**. Flash Onion OS following
  [`software/mods/onion-os/README.md`](../software/mods/onion-os/README.md)
  (toolchain setup:
  [`software/guides/esp-idf-vscode-setup.md`](../software/guides/esp-idf-vscode-setup.md)).
  ⚠️ **Never run `erase-flash`** — it deletes your badge's wallet.

## 3. Let it connect

- Onion OS auto-joins the venue WiFi (**`CIC-Guest`** / `1nnovation`) and
  announces itself to the server. It generates a per-badge wallet and is
  assigned an **Onion ID**.
- Want to see its details? Plug into USB, open a serial monitor at 115200 baud,
  and type `state` — it prints your `onionId`, `hardwareId`, `wallet`, and a
  `status` (it stays `seen` until you link it in the next step).

## 4. Link it to your profile  ← the step most people miss

1. Go to **<https://oniondao.dev/portal/onions>** — note it's the **Onions**
   page, *not* a page called "badge".
2. Enter your badge's **Onion ID** and submit the link request.
3. **Approve it on the badge** (press **SELECT** on the prompt that appears).

Your onion **points** migrate to on-chain **coins**, and your username + balance
now show on the badge's home screen. Done. 🧅

## Troubleshooting

| Symptom | Fix |
|---|---|
| Nothing happens on power-on | Battery isn't plugged in (or is empty). Plug in **USB-C** — it boots on USB. |
| Screen is frozen | Tap the small **RESET** button (safe — reboots into Onion OS). Leave **BOOT** alone. |
| Someone said "there's no OS on it" | It needs flashing — see [step 2](#2-confirm-onion-os-is-running). |
| Can't find where to link the badge | It's **<https://oniondao.dev/portal/onions>**. If you're stuck, ask a neighbor or the desk — that's how a lot of us found it. |

---

*Found a gap in here, or a step that tripped you up? That's exactly the kind of
thing worth fixing — PRs welcome.*

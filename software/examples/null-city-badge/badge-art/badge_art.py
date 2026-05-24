#!/usr/bin/env python3
"""
NULL City Badge — Art Tool

Draw black-and-white artwork or import any image, preview it at badge scale,
then send it to the badge over USB serial in one click.

Usage:
    pip install pillow pyserial
    python3 badge_art.py
"""

import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

try:
    from PIL import Image, ImageOps, ImageTk
except ImportError:
    raise SystemExit("Missing dependency — run: pip install pillow")

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    raise SystemExit("Missing dependency — run: pip install pyserial")

# ── Constants ──────────────────────────────────────────────────────────────────
DISPLAY_W = 264
DISPLAY_H = 176
SCALE     = 3          # screen pixels per badge pixel
IMG_SIZE  = DISPLAY_W * DISPLAY_H // 8   # 5808 bytes
MAGIC     = b"IMG:"


class BadgeArtTool:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("NULL City Badge — Art Tool")
        self.root.resizable(False, False)

        # Internal 1-bit image.  Pillow mode '1': pixel 1 = white, 0 = black.
        self._img: Image.Image = Image.new("1", (DISPLAY_W, DISPLAY_H), 1)
        self._prev_px: tuple[int, int] | None = None

        self._build_ui()
        self._redraw()

    # ── UI ────────────────────────────────────────────────────────────────────

    def _build_ui(self):
        bar = tk.Frame(self.root, padx=4, pady=4)
        bar.pack(fill="x")

        for label, cmd in [("Open Image", self._open),
                           ("Save PNG",   self._save),
                           ("Clear",      self._clear),
                           ("Invert",     self._invert)]:
            tk.Button(bar, text=label, command=cmd).pack(side="left", padx=2)

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=6, pady=2)

        self._tool = tk.StringVar(value="pencil")
        tk.Radiobutton(bar, text="Draw",  variable=self._tool, value="pencil").pack(side="left")
        tk.Radiobutton(bar, text="Erase", variable=self._tool, value="eraser").pack(side="left")

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=6, pady=2)

        tk.Label(bar, text="Port:").pack(side="left")
        self._port_var = tk.StringVar()
        self._port_cb = ttk.Combobox(bar, textvariable=self._port_var, width=16)
        self._port_cb.pack(side="left", padx=2)
        tk.Button(bar, text="↻", width=2, command=self._scan_ports).pack(side="left")

        tk.Button(bar, text="Send to Badge ▶",
                  bg="#1a7a3a", fg="white",
                  command=self._send).pack(side="left", padx=8)

        self._status = tk.StringVar(
            value="Ready — draw or Open Image, then Send to Badge.")
        tk.Label(self.root, textvariable=self._status,
                 relief="sunken", anchor="w", bd=1).pack(fill="x", padx=4, pady=(0, 2))

        border = tk.Frame(self.root, bd=2, relief="groove")
        border.pack(padx=4, pady=(0, 4))

        self._canvas = tk.Canvas(border,
                                  width=DISPLAY_W * SCALE,
                                  height=DISPLAY_H * SCALE,
                                  cursor="crosshair",
                                  highlightthickness=0)
        self._canvas.pack()
        self._img_item = self._canvas.create_image(0, 0, anchor="nw")

        self._canvas.bind("<Button-1>",        self._on_press)
        self._canvas.bind("<B1-Motion>",       self._on_drag)
        self._canvas.bind("<ButtonRelease-1>", lambda _: self._stroke_end())
        # Right-click / right-drag = erase
        self._canvas.bind("<Button-3>",        lambda e: self._on_press(e, erase=True))
        self._canvas.bind("<B3-Motion>",       lambda e: self._on_drag(e, erase=True))
        self._canvas.bind("<ButtonRelease-3>", lambda _: self._stroke_end())

        self._scan_ports()

    # ── Serial ports ──────────────────────────────────────────────────────────

    def _scan_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            ports = ["/dev/ttyUSB0"]
        self._port_cb["values"] = ports
        if self._port_var.get() not in ports:
            self._port_var.set(ports[0])

    # ── Drawing ───────────────────────────────────────────────────────────────

    def _to_px(self, sx: int, sy: int) -> tuple[int, int]:
        return (max(0, min(DISPLAY_W - 1, sx // SCALE)),
                max(0, min(DISPLAY_H - 1, sy // SCALE)))

    def _set_pixel(self, x: int, y: int, black: bool):
        self._img.putpixel((x, y), 0 if black else 1)

    def _line(self, x0: int, y0: int, x1: int, y1: int, black: bool):
        """Bresenham line — draw all pixels between two badge coordinates."""
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self._set_pixel(x0, y0, black)
            if (x0, y0) == (x1, y1):
                break
            e2 = err * 2
            if e2 > -dy: err -= dy; x0 += sx
            if e2 <  dx: err += dx; y0 += sy

    def _on_press(self, e: tk.Event, erase: bool = False):
        black = not erase and self._tool.get() == "pencil"
        x, y = self._to_px(e.x, e.y)
        self._set_pixel(x, y, black)
        self._prev_px = (x, y)
        self._redraw()

    def _on_drag(self, e: tk.Event, erase: bool = False):
        black = not erase and self._tool.get() == "pencil"
        x, y = self._to_px(e.x, e.y)
        if self._prev_px:
            self._line(*self._prev_px, x, y, black)
        self._prev_px = (x, y)
        self._redraw()

    def _stroke_end(self):
        self._prev_px = None

    def _redraw(self):
        """Push self._img to the on-screen canvas via Pillow → ImageTk."""
        scaled = self._img.resize(
            (DISPLAY_W * SCALE, DISPLAY_H * SCALE), Image.NEAREST)
        self._tk_img = ImageTk.PhotoImage(scaled.convert("RGB"))
        self._canvas.itemconfig(self._img_item, image=self._tk_img)

    # ── Image operations ──────────────────────────────────────────────────────

    def _open(self):
        path = filedialog.askopenfilename(
            filetypes=[("Images",
                        "*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tiff"),
                       ("All files", "*.*")])
        if not path:
            return
        src = Image.open(path).convert("RGB")
        # Fit within display bounds, preserve aspect ratio
        src.thumbnail((DISPLAY_W, DISPLAY_H), Image.LANCZOS)
        # Center on white background
        bg = Image.new("RGB", (DISPLAY_W, DISPLAY_H), 255)
        bg.paste(src, ((DISPLAY_W - src.width) // 2,
                        (DISPLAY_H - src.height) // 2))
        # Floyd-Steinberg dither to 1-bit
        self._img = bg.convert("1")
        self._redraw()
        self._status.set(f"Loaded: {path.split('/')[-1]}")

    def _save(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG", "*.png"), ("All files", "*.*")])
        if path:
            self._img.save(path)
            self._status.set(f"Saved: {path.split('/')[-1]}")

    def _clear(self):
        self._img = Image.new("1", (DISPLAY_W, DISPLAY_H), 1)
        self._redraw()
        self._status.set("Canvas cleared.")

    def _invert(self):
        self._img = ImageOps.invert(self._img.convert("L")).convert("1")
        self._redraw()
        self._status.set("Inverted.")

    # ── Send to badge ─────────────────────────────────────────────────────────

    def _make_payload(self) -> bytes:
        """
        Build the 5808-byte bitmap for Adafruit GFX drawBitmap.

        Pillow '1' tobytes(): packed MSB-first, bit 1 = white, bit 0 = black.
        Adafruit GFX drawBitmap(…, GxEPD_BLACK, GxEPD_WHITE): bit 1 = BLACK.
        XOR 0xFF inverts every byte so the polarities match.
        """
        return bytes(b ^ 0xFF for b in self._img.tobytes())

    def _send(self):
        port = self._port_var.get()
        if not port:
            messagebox.showwarning("No port", "Select a serial port first.")
            return
        try:
            self._status.set("Opening port…")
            self.root.update()
            # dsrdtr=False, rtscts=False prevents the CH340C from
            # auto-resetting the ESP32 when we open the port.
            with serial.Serial(port, 115200, timeout=6,
                               dsrdtr=False, rtscts=False) as ser:
                time.sleep(0.15)
                ser.reset_input_buffer()

                payload = self._make_payload()
                ser.write(MAGIC + payload)
                self._status.set(
                    f"Sent {len(payload)} bytes — waiting for badge…")
                self.root.update()

                resp = ser.read_until(b"\n")
                if b"OK" in resp:
                    self._status.set("Done!  Image is on the badge.")
                else:
                    self._status.set(
                        f"Sent (badge replied: {resp.strip()!r})")

        except serial.SerialException as err:
            messagebox.showerror("Serial error", str(err))
            self._status.set("Send failed.")


if __name__ == "__main__":
    root = tk.Tk()
    BadgeArtTool(root)
    root.mainloop()

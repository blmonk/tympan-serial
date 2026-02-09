import tkinter as tk
from tkinter import ttk, messagebox
from queue import Empty

from serialDelay import SerialDelayClient  # imports the class you added


# --- GUI settings (adjust if you want different range/step) ---
DELAY_MIN_MS = 0.0
DELAY_MAX_MS = 1000.0
DELAY_STEP_MS = 0.1
SEND_DEBOUNCE_MS = 120  # avoid spamming Teensy while dragging slider


class SerialDelayGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Teensy Serial Delay Controller")
        self.geometry("740x520")

        self.client = SerialDelayClient()
        self._debounce_after_id = None

        # ---- Top controls: port + connect ----
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=10)

        ttk.Label(top, text="Port:").pack(side="left")

        self.port_var = tk.StringVar(value="")
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=35, state="readonly")
        self.port_combo.pack(side="left", padx=(6, 6))

        self.refresh_btn = ttk.Button(top, text="Refresh", command=self.refresh_ports)
        self.refresh_btn.pack(side="left")

        self.connect_btn = ttk.Button(top, text="Connect", command=self.toggle_connect)
        self.connect_btn.pack(side="left", padx=(10, 0))

        # ---- Delay controls ----
        delay_frame = ttk.LabelFrame(self, text="Delay (ms)")
        delay_frame.pack(fill="x", padx=10, pady=(0, 10))

        self.delay_var = tk.DoubleVar(value=20.0)
        self.delay_label_var = tk.StringVar(value="20.0 ms")

        row1 = ttk.Frame(delay_frame)
        row1.pack(fill="x", padx=10, pady=(10, 6))

        self.delay_scale = tk.Scale(
            row1,
            from_=DELAY_MIN_MS,
            to=DELAY_MAX_MS,
            resolution=DELAY_STEP_MS,
            orient="horizontal",
            variable=self.delay_var,
            command=self.on_slider_move,  # called continuously while moving
            length=500,
        )
        self.delay_scale.pack(side="left", fill="x", expand=True)

        ttk.Label(row1, textvariable=self.delay_label_var, width=12, anchor="e").pack(side="left", padx=(10, 0))

        row2 = ttk.Frame(delay_frame)
        row2.pack(fill="x", padx=10, pady=(0, 10))

        ttk.Label(row2, text="Exact:").pack(side="left")

        self.delay_entry_var = tk.StringVar(value="20.0")
        self.delay_entry = ttk.Entry(row2, textvariable=self.delay_entry_var, width=12)
        self.delay_entry.pack(side="left", padx=(6, 6))
        self.delay_entry.bind("<Return>", self.on_entry_set)
        self.delay_entry.bind("<FocusOut>", self.on_entry_set)

        self.set_btn = ttk.Button(row2, text="Send", command=self.send_current_delay)
        self.set_btn.pack(side="left")

        self.status_var = tk.StringVar(value="Disconnected")
        ttk.Label(row2, textvariable=self.status_var).pack(side="left", padx=(14, 0))

        # ---- Output console ----
        out_frame = ttk.LabelFrame(self, text="Teensy Output")
        out_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        self.text = tk.Text(out_frame, wrap="word", height=12)
        self.text.pack(side="left", fill="both", expand=True)

        scroll = ttk.Scrollbar(out_frame, command=self.text.yview)
        scroll.pack(side="right", fill="y")
        self.text.configure(yscrollcommand=scroll.set)

        # initial ports
        self.refresh_ports()
        self.update_delay_labels()

        # poll serial RX queue
        self.after(40, self.poll_rx)

        # close handling
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def refresh_ports(self):
        ports = self.client.list_ports()
        display = []
        for p in ports:
            desc = p.description or ""
            display.append(f"{p.device} — {desc}")
        self.port_combo["values"] = display

        if display and not self.port_var.get():
            self.port_var.set(display[0])

    def get_selected_device(self):
        s = self.port_var.get().strip()
        if not s:
            return None
        # value is like "COM5 — Teensy ..." or "/dev/ttyACM0 — ..."
        return s.split(" — ", 1)[0].strip()

    def toggle_connect(self):
        if self.client.is_connected():
            self.client.disconnect()
            self.connect_btn.config(text="Connect")
            self.status_var.set("Disconnected")
            return

        dev = self.get_selected_device()
        if not dev:
            messagebox.showerror("No port", "Select a serial port first.")
            return

        try:
            self.client.connect(dev, baud=115200, timeout=0.1, dtr_reset=False)
        except Exception as e:
            messagebox.showerror("Connect failed", str(e))
            return

        self.connect_btn.config(text="Disconnect")
        self.status_var.set(f"Connected to {dev}")

        # Optionally send an initial delay command right away
        self.send_current_delay()

    def update_delay_labels(self):
        val = float(self.delay_var.get())
        self.delay_label_var.set(f"{val:.1f} ms")
        self.delay_entry_var.set(f"{val:.3f}".rstrip("0").rstrip("."))

    def on_slider_move(self, _):
        # Called frequently while dragging
        self.update_delay_labels()

        # Debounce sending to Teensy (avoids tons of serial writes)
        if self._debounce_after_id is not None:
            self.after_cancel(self._debounce_after_id)
        self._debounce_after_id = self.after(SEND_DEBOUNCE_MS, self.send_current_delay)

    def on_entry_set(self, _event=None):
        # Parse entry -> update slider -> send immediately
        try:
            val = float(self.delay_entry_var.get().strip())
        except ValueError:
            return

        val = max(DELAY_MIN_MS, min(DELAY_MAX_MS, val))
        self.delay_var.set(val)
        self.update_delay_labels()
        self.send_current_delay()

    def send_current_delay(self):
        if not self.client.is_connected():
            return
        val = float(self.delay_var.get())
        try:
            self.client.set_delay_ms(val)
        except Exception as e:
            self.append_text(f"\n[Serial write error] {e}\n")

    def poll_rx(self):
        # Pull Teensy output from the queue and display
        try:
            while True:
                chunk = self.client.rx_queue.get_nowait()
                self.append_text(chunk)
        except Empty:
            pass
        self.after(40, self.poll_rx)

    def append_text(self, s: str):
        self.text.insert("end", s)
        self.text.see("end")

    def on_close(self):
        try:
            self.client.disconnect()
        except Exception:
            pass
        self.destroy()


if __name__ == "__main__":
    app = SerialDelayGUI()
    app.mainloop()

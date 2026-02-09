import argparse
import sys
import threading
import time
from queue import Queue, Empty

import serial
from serial.tools import list_ports

# Teensy SerialManager uses a 64-byte buffer including '\0'
MAX_TEENSY_LINE_LEN = 63

# Optional: improves prompt redraw on Linux/macOS (and some Windows terminals)
try:
    import readline  # type: ignore
except Exception:
    readline = None


def iter_ports():
    return list(list_ports.comports())


def print_ports():
    ports = iter_ports()
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        desc = p.description or ""
        manu = getattr(p, "manufacturer", "") or ""
        hwid = p.hwid or ""
        print(f"{p.device:20}  {desc}  {manu}  {hwid}")


def auto_pick_port():
    """
    Best-effort port picker. Prefers ports that look like Teensy/USB serial devices.
    """
    ports = iter_ports()
    if not ports:
        return None

    def score(p):
        text = " ".join([
            p.device or "",
            p.description or "",
            getattr(p, "manufacturer", "") or "",
            p.hwid or "",
        ]).lower()

        s = 0
        if "teensy" in text:
            s += 50
        if "pjrc" in text:
            s += 30
        if "usb serial" in text:
            s += 10

        if "ttyacm" in text:
            s += 8
        if "usbmodem" in text or "usbserial" in text:
            s += 8

        if "bluetooth" in text:
            s -= 50
        return s

    ports_sorted = sorted(ports, key=score, reverse=True)
    return ports_sorted[0].device


class SerialDelayClient:
    """
    Backend client for Teensy SerialManager line protocol:
      - send commands terminated with '\n'
      - read whatever Teensy prints and push to rx_queue
    """

    def __init__(self):
        self.ser = None
        self._stop_evt = threading.Event()
        self._rx_thread = None
        self.rx_queue = Queue()  # strings (decoded)

    @staticmethod
    def list_ports():
        return iter_ports()

    def connect(self, port: str, baud: int = 115200, timeout: float = 0.1, dtr_reset: bool = False,
                flush_on_open: bool = False):
        self.disconnect()
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=timeout)

        # Optional DTR toggle (some boards reset on this)
        if dtr_reset:
            try:
                self.ser.dtr = False
                time.sleep(0.05)
                self.ser.dtr = True
            except Exception:
                pass

        # Optional flush (WARNING: can drop Teensy startup banner)
        if flush_on_open:
            try:
                self.ser.reset_input_buffer()
                self.ser.reset_output_buffer()
            except Exception:
                pass

        self._stop_evt.clear()
        self._rx_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._rx_thread.start()

    def disconnect(self):
        self._stop_evt.set()
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def is_connected(self) -> bool:
        return self.ser is not None and self.ser.is_open

    def send_line(self, line: str):
        if not self.is_connected():
            raise RuntimeError("Not connected")
        line = line.strip()
        if len(line) > MAX_TEENSY_LINE_LEN:
            raise ValueError(f"Command too long for Teensy buffer (max {MAX_TEENSY_LINE_LEN} chars).")
        # Teensy executes on LF and ignores CR
        self.ser.write((line + "\n").encode())

    def set_delay_ms(self, delay_ms: float):
        self.send_line(f"d {delay_ms}")

    def _reader_loop(self):
        while not self._stop_evt.is_set():
            try:
                n = self.ser.in_waiting if self.ser is not None else 0
                if n:
                    data = self.ser.read(n)
                    if data:
                        self.rx_queue.put(data.decode(errors="replace"))
                else:
                    time.sleep(0.01)
            except Exception as e:
                self.rx_queue.put(f"\n[Serial read error] {e}\n")
                self._stop_evt.set()
                return


def cli_output_printer(client: SerialDelayClient, stop_evt: threading.Event, prompt: str = "> "):
    """
    Prints Teensy output to stdout. If readline is available, tries to redraw the prompt
    without losing what the user is typing (best-effort, no extra deps).
    """
    while not stop_evt.is_set():
        try:
            chunk = client.rx_queue.get(timeout=0.05)
        except Empty:
            continue

        if not chunk:
            continue

        if readline is not None:
            # Preserve current input buffer (best effort)
            buf = readline.get_line_buffer()
            # Clear current line, print device output, redraw prompt + buffer
            sys.stdout.write("\r\033[K")
            sys.stdout.write(chunk)
            if not chunk.endswith("\n"):
                sys.stdout.write("\n")
            sys.stdout.write(prompt + buf)
            sys.stdout.flush()
        else:
            sys.stdout.write(chunk)
            sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser(
        description="Interactive serial console for Teensy SerialManager (plus reusable backend for GUI)."
    )
    parser.add_argument("--list", action="store_true", help="List available serial ports and exit.")
    parser.add_argument("--port", help="Serial port (COM5 or /dev/ttyACM0). If omitted, auto-pick.")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200).")
    parser.add_argument("--timeout", type=float, default=0.1, help="Read timeout seconds (default: 0.1).")
    parser.add_argument("--dtr-reset", action="store_true", help="Toggle DTR low->high after opening.")
    parser.add_argument("--flush-on-open", action="store_true",
                        help="Flush buffers after opening (WARNING: may drop startup text).")
    args = parser.parse_args()

    if args.list:
        print_ports()
        return

    port = args.port or auto_pick_port()
    if not port:
        print("No serial ports found. Plug in the Teensy and try --list.")
        sys.exit(1)

    client = SerialDelayClient()
    try:
        client.connect(
            port=port,
            baud=args.baud,
            timeout=args.timeout,
            dtr_reset=args.dtr_reset,
            flush_on_open=args.flush_on_open,
        )
    except Exception as e:
        print(f"Failed to open {port}: {e}")
        sys.exit(1)

    print(f"Connected to {port} @ {args.baud}.")
    print("Protocol: Teensy ignores CR and executes commands on LF (\\n).")
    print("Examples:  h   |  d 10   |  g   |  k 6   |  C")
    print("Type /ports to list ports, /quit to exit.\n")

    stop_evt = threading.Event()
    t = threading.Thread(target=cli_output_printer, args=(client, stop_evt, "> "), daemon=True)
    t.start()

    try:
        while True:
            try:
                line = input("> ").strip()
            except (EOFError, KeyboardInterrupt):
                break

            if not line:
                continue

            if line.lower() in ("/quit", "/exit"):
                break

            if line.lower() == "/ports":
                print_ports()
                continue

            try:
                client.send_line(line)
            except Exception as e:
                print(f"[Local] {e}")

    finally:
        stop_evt.set()
        client.disconnect()
        print("\nDisconnected.")


if __name__ == "__main__":
    main()

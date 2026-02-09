from serial.tools import list_ports

for p in list_ports.comports():
    print(f"{p.device:20}  {p.description}  {getattr(p,'manufacturer','') or ''}  {p.hwid}")

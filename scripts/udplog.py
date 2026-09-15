#!/usr/bin/env python3
"""Live log stream from the display: prints every UDP datagram broadcast to
port 5555 (see LOGB in src/main.cpp). Unlike `nc -ul`, keeps working across
multiple datagrams and device reboots."""
import datetime
import socket

PORT = 5555

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", PORT))
print(f"listening on udp/{PORT} (ctrl-c to quit)")
while True:
    data, addr = s.recvfrom(2048)
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"{ts}  {data.decode(errors='replace').rstrip()}", flush=True)

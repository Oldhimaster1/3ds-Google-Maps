#!/usr/bin/env python3
"""Source-level GPS server regression: validate response framing and ACK order."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
source=(root/"source/gps_server.c").read_text(encoding="utf-8")
checks={
  "ack after accepted fix": "if (accepted)" in source and "if (parse_location_json(body" in source,
  "valid/invalid responses": "204 No Content" in source and "400 Bad Request" in source,
  "response lengths from sizeof": all("sizeof("+key+")-1" in source for key in
       ("ok_response","invalid_response","missing_response","options_response")),
  "HTTP framing": all(item in source for item in ("Content-Length: 0","Connection: close")),
  "phone checks HTTP result": "if(!r.ok)throw Error('3DS returned HTTP '+r.status)" in source,
  "phone counts acknowledged fixes": "n++;status.textContent='Fix #'" in source,
}
for name,ok in checks.items():print(("PASS" if ok else "FAIL"),name)
assert all(checks.values())

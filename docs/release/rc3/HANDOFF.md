# Continue from this point

Do not rebuild or re-upload RC3 merely because the app session was interrupted.
Source `9e09e27`; package commit `9cc4858`; README/QR checkpoint `8ba253e`.
RC2 was not changed. Both immutable RC3 downloads matched SHA256SUMS.

The source has no new changes after the Country Streak fix. The full gate/clean
build logs and focused regression recheck are in this directory. The focused
recheck used ASAN_OPTIONS=detect_leaks=0 due to the container ptrace limit.

Fresh globe/map/menu media are now in docs/media/rc3. Full online-feature capture
remains blocked; see media.md. Do not describe the walkthrough as full gameplay.

Recovered checkout: /workspace/scratch/451b60487e85/repo
Working original recorder: /tmp/rc3-final-worker.py (unique display 32596)
Input helper: /tmp/rc3-input.py
Original binary restart: /tmp/rc3-final-original.py
Command queue: /tmp/rc3-final-capture-command.json
Environment-only proxy experiments: /tmp/azahar-proxy.c and /tmp/rc3-recording-build
Do not ship local test CAs or that diagnostic binary.

No merge, stable tag, GitHub Release, stable replacement or announcement.
The owner must review README/media before approving the production launch.

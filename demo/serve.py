"""Static server for the demos, with byte ranges.

    py serve.py [port]          # from the repository root or from demo/

Python's own `http.server` answers every request with 200 and the whole file.
Safari will not start an <video> on that, and it cannot seek one, so `video.html`
stays black on an iPhone until the server can answer a Range request with a 206.
That is the only reason this file exists.

Serves the repository root, so `/demo/video.html` can reach `/webgl/winliqglass.js`.
"""

import os
import re
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RANGE = re.compile(r"bytes=(\d*)-(\d*)")


class RangeHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Accept-Ranges", "bytes")
        # The demos are edited while they are being watched.
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def send_head(self):
        header = self.headers.get("Range")
        if not header:
            return super().send_head()

        match = RANGE.fullmatch(header.strip())
        path = self.translate_path(self.path)
        if not match or not os.path.isfile(path):
            return super().send_head()

        size = os.path.getsize(path)
        start, end = match.group(1), match.group(2)
        if start == "":
            # A suffix range: the last N bytes.
            length = min(int(end or 0), size)
            start, end = size - length, size - 1
        else:
            start = int(start)
            end = int(end) if end else size - 1
            end = min(end, size - 1)
        if start > end or start >= size:
            self.send_error(416, "Requested range not satisfiable")
            return None

        handle = open(path, "rb")
        handle.seek(start)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        return _Window(handle, end - start + 1)

    def log_message(self, *args):
        pass


class _Window:
    """A file narrowed to one range, so copyfile stops at the end of it."""

    def __init__(self, handle, remaining):
        self.handle = handle
        self.remaining = remaining

    def read(self, amount=-1):
        if self.remaining <= 0:
            return b""
        if amount is None or amount < 0:
            amount = self.remaining
        chunk = self.handle.read(min(amount, self.remaining))
        self.remaining -= len(chunk)
        return chunk

    def close(self):
        self.handle.close()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    handler = partial(RangeHandler, directory=ROOT)
    server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    print(f"serving {ROOT} on http://127.0.0.1:{port}")
    print(f"  http://127.0.0.1:{port}/demo/drag.html")
    print(f"  http://127.0.0.1:{port}/demo/site.html")
    print(f"  http://127.0.0.1:{port}/demo/video.html")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass

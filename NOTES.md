# Capture Viewer: Progress Notes

A C web server, written from scratch, that shows the Arduino UNO camera captures on a web page and updates as new images arrive.

- **Images come from:** `~/Documents/Personal/Projects/Edge Ai Resistor Classifier V1/uno_captures/test`
- **Started from:** [http-server-c](https://github.com/JCano22/http-server-c) (the original server, kept separate)

**Last updated:** 2026-09-30. **Next up:** Step 3.1.

---

## How to run

From the project folder (the server opens `www/index.html` with a relative path):

```bash
cc -Wall -Wextra -o server src/server.c
./server
```

Open **http://localhost:8080**. Stop with **Ctrl+C**.

Test single requests from a second terminal:

```bash
curl -i http://localhost:8080/
curl -i http://localhost:8080/api/images
curl -i "http://localhost:8080/captures/test_20260927_210207_1.jpg?v=123"
```

---

## How the page and server talk

The server only answers; it never starts a conversation. The page asks, and the server responds.

```
Browser                                    server.c
  │ ── GET / ─────────────────────────────► │  send www/index.html
  │ ◄──────────────────────── index.html ── │
  │  (page's JavaScript runs)               │
  │ ── GET /api/images  (every 1 second) ─► │  send JSON list of images
  │ ◄───────────────────────── JSON list ── │
  │ ── GET /captures/<file>.jpg?v=... ────► │  send the image bytes
  │ ◄─────────────────────── image bytes ── │
```

What the page expects from the server (both sides must match):

| Path | Server must send | Content-Type |
|---|---|---|
| `/` | `www/index.html` | `text/html; charset=utf-8` |
| `/api/images` | JSON array (format below) | `application/json` |
| `/captures/<file>.jpg?v=...` | That image file. Ignore everything after `?`. | `image/jpeg` |
| anything else | `404 Not Found` | `text/plain; charset=utf-8` |

JSON format for `/api/images`: one object per `.jpg`:

```json
[
  {"group": "", "name": "test_20260927_210207_1.jpg", "mtime": 1790614686, "size": 183199}
]
```

- `group`: subfolder name, or `""` if the image is directly in the capture folder
- `mtime`: last-modified time in seconds (`st_mtime` from `stat`)
- `size`: file size in bytes (`st_size` from `stat`)

The page uses `mtime` + `size` to detect a changed image. It adds `?v=<mtime>-<size>` to the image URL so the browser downloads the new version.

Where this is in `www/index.html`: `fetch("/api/images")` is in `refresh()`, and image URLs are built in `imageUrl()`.

---

## Done ✅

### Step 1: Routing
- [x] **1.1** Get method and path from the request: `sscanf(req_buf, "%7s %1023s", method, path)`
- [x] **1.2** Skip empty or malformed requests: continue if `sscanf` doesn't return `2`
- [x] **1.3** Send `index.html` only when `strcmp(path, "/") == 0`
- [x] **1.4** Send a proper `404 Not Found` for everything else
- [x] Raised `file_buf` from 8192 to 32768 so the whole `index.html` is sent

### Step 2: Serve one image
- [x] **2.1** Recognize images: `strncmp(path, "/captures/", 10) == 0`, filename is `path + 10`
- [x] **2.2** Remove the query string: `strchr(path, '?')`, then replace `?` with `'\0'` (check for `NULL` first)
- [x] **2.3** Moved the 404 code into `send_text(client_fd, status, body)` above `main`. Response is unchanged.
- [x] **2.4** Block `..` in the filename (`strstr(filename, "..")`) and send `403 Forbidden`, so nobody can read files outside the capture folder (path traversal). The rest of the image code goes in the `else`, so nothing runs after the 403. Test with `curl -i --path-as-is http://localhost:8080/captures/../src/server.c`
- [x] **2.5** Build the full path: `#define CAPTURE_DIR "<absolute path>"` (no `~`, since `open()` doesn't expand it), then `snprintf(full_path, sizeof(full_path), "%s/%s", CAPTURE_DIR, filename)` inside the inner `else`
- [x] **2.6** Send the image in chunks
  - `open` the file (404 + `perror` if it fails), `fstat` it for `Content-Length` (`%lld` with `(long long)st.st_size`), send headers with `Content-Type: image/jpeg`
  - Loop: `read` up to `sizeof(chunk)` (16 KB), then send exactly `nread` bytes. Stop when `read` returns `0` (end of file) or `< 0` (error)
  - `write_all(fd, buf, len)` above `main` keeps calling `write` until every byte is accepted; returns `-1` on error so the chunk loop can `break`
  - `signal(SIGPIPE, SIG_IGN)` at the start of `main`, so a browser disconnect makes `write` return `-1` (logged as `write: Broken pipe`) instead of killing the server
  - **Tested:** downloaded image is byte-identical to the original (183199 bytes); missing file → 404; `..` → 403; clients that reset mid-response log `Broken pipe` and the server keeps running

---

## To do ⏳

### Step 3: Image list (`/api/images`)
- [ ] **3.1** List the capture folder with `opendir` / `readdir` / `closedir`
- [ ] **3.2** Keep only `.jpg` / `.jpeg` files, and skip names starting with `.`
- [ ] **3.3** Get `mtime` and `size` for each with `stat`
- [ ] **3.4** Build the JSON text and send it with `Content-Type: application/json`
- [ ] **Test:** `curl -i http://localhost:8080/api/images`. Then the page should show the images.

### Step 4: Cleanup and improvements
- [ ] Use the chunked file sender for `index.html` too, and remove the fixed 32 KB buffer
- [ ] Fix the server getting stuck on Chrome's idle "preconnect" connections (see Known issues)
- [ ] Optional: images in subfolders become separate `group`s on the page

---

## Known issues

- **Server pauses on idle connections.** Chrome opens spare connections and sends nothing on them. The server handles one connection at a time and waits in `read()` until Chrome closes it, which blocks other requests meanwhile. Possible fix: a read timeout with `setsockopt(..., SO_RCVTIMEO, ...)`.
- **Chrome's own requests** (like `/.well-known/appspecific/com.chrome.devtools.json` or `/favicon.ico`) show up in the log. They correctly get a 404.

---

## Concepts learned

| Concept | Where it came up |
|---|---|
| `(struct sockaddr *)&addr` casts the **pointer**, not the struct | `bind()`, `accept()` |
| `*` in a type means "pointer to"; `*` on a value means dereference | casts, `*query = '\0'` |
| `sscanf` returns how many variables it filled | request parsing |
| Strings are compared with `strcmp` (`== 0` means equal), never `==` | routing |
| `strncmp` compares only the first *n* characters | `/captures/` prefix |
| `path + 10`: pointer arithmetic, no copying | getting the filename |
| Putting `'\0'` in a string cuts it there | removing `?v=...` |
| HTTP response = status line, headers, blank line, body; lines end in `\r\n` | 200 and 404 responses |
| `Content-Length` must match the body exactly | 404 bug (`body_len` vs `errhdr_len`) |
| `%zu` for `size_t` (unsigned), `%zd` for `ssize_t` (signed) | `snprintf` |
| `read()` on a socket returns 0 when the other side closes | empty requests |
| `-Wall -Wextra` warnings point at real bugs | the "unused variable" warning |
| Relative URLs (`/api/images`) go to the same server the page came from | `fetch()` |
| A parameter is already a variable; declaring it again in the function is a redefinition error | `send_text` |
| Never trust a filename from the URL: `..` climbs out of a folder (path traversal) | `/captures/` 403 check |
| `snprintf` writes *into* a buffer (buffer, size, format, values); it returns a length, not a string | building `full_path` |
| `sizeof(array)` = buffer size; `strlen` = text length. `sizeof` on a pointer is just `8` | `snprintf` size argument |
| The browser tab polls `/api/images` every second and floods the log; close it or `grep --line-buffered` when testing with curl | 2.5 test |
| An fd is an index into the process's **file descriptor table** (fd table → open file table → inode/vnode). `fstat` follows it to get the size | `open`, `fstat` |
| `read` returns how many bytes you actually got; `write` exactly that many, never the buffer size | chunk loop (last chunk was 15 bytes) |
| `write` copies into the kernel's send buffer and may accept fewer bytes than offered; loop until all are sent | `write_all` |
| `size_t` is unsigned, so `< 0` is never true; store `read`/`write` results in `ssize_t` | `write_all` bug |
| Writing to a closed connection raises `SIGPIPE`, which kills the process by default; `SIG_IGN` turns it into a `-1` from `write` | 2.6 Part C |
| On localhost a 183 KB image fits in the send buffer, so a slow curl + Ctrl+C may not trigger an error; a client that resets immediately does | testing `SIGPIPE` |

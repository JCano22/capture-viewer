# Capture Viewer: Progress Notes

A C web server, written from scratch, that shows the Arduino UNO camera captures on a web page and updates as new images arrive.

- **Images come from:** `~/Documents/Personal/Projects/Edge Ai Resistor Classifier V1/uno_captures/test`
- **Started from:** [http-server-c](https://github.com/JCano22/http-server-c) (the original server, kept separate)

**Last updated:** 2026-09-29. **Next up:** Step 2.3.

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

### Step 2: Serve one image (in progress)
- [x] **2.1** Recognize images: `strncmp(path, "/captures/", 10) == 0`, filename is `path + 10`
- [x] **2.2** Remove the query string: `strchr(path, '?')`, then replace `?` with `'\0'` (check for `NULL` first)

---

## To do ⏳

### Step 2: Serve one image (continued)
- [ ] **2.3** Move the 404 code into a reusable function above `main`:
  ```c
  static void send_text(int client_fd, const char *status, const char *body)
  ```
  The `else` becomes `send_text(client_fd, "404 Not Found", "Not found\n");`. Behavior should stay exactly the same.
- [ ] **2.4** Block `..` in the filename (`strstr(filename, "..")`) and send `403 Forbidden`, so nobody can read files outside the capture folder
- [ ] **2.5** Build the full path: `snprintf` the capture folder + `/` + filename into a buffer
- [ ] **2.6** Send the image in chunks. Images are ~180 KB, bigger than any single buffer.
  - `open` the file, `fstat` it to get the size for `Content-Length`
  - Send headers with `Content-Type: image/jpeg`
  - Loop: `read` a chunk (e.g. 16 KB), `write` it, until the file is done
  - `write()` can send fewer bytes than asked. Check its return value and keep writing.
  - Call `signal(SIGPIPE, SIG_IGN)` at the start of `main`. Otherwise the server dies if the browser disconnects mid-image.
  - **Test:** `curl -o test.jpg http://localhost:8080/captures/test_20260927_210207_1.jpg`, then open `test.jpg`

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

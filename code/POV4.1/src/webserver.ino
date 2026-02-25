
#include <detail/mimetable.h>   // OK to include; does not define functions
#include "image_processing.h"
#include "staff_config.h"

static File uploadFile;
static String uploadTempPath = "/_upload.bmp";
static String uploadDestPath;
static String uploadStatusMessage;
static bool uploadRotateCw = false;
static uint16_t uploadRequestedHeight = 0;
static bool uploadReady = false;
static bool uploadOk = false;
static size_t uploadBytes = 0;
static bool uploadTooLarge = false;
static const size_t kMaxUploadBytes = 1024 * 1024;

extern WebServer * webserver;
extern const char kIndexHtml[];


static String getContentTypeLocal(const String &path) {
  char buff[sizeof(mime::mimeTable[0].mimeType)];

  // try explicit extensions first
  for (size_t i = 0; i < (sizeof(mime::mimeTable) / sizeof(mime::mimeTable[0])) - 1; i++) {
    strcpy_P(buff, mime::mimeTable[i].endsWith);
    if (path.endsWith(buff)) {
      strcpy_P(buff, mime::mimeTable[i].mimeType);
      return String(buff);
    }
  }

  // default (last entry is usually application/octet-stream)
  const size_t last = (sizeof(mime::mimeTable) / sizeof(mime::mimeTable[0])) - 1;
  strcpy_P(buff, mime::mimeTable[last].mimeType);
  return String(buff);
}

static String sanitizeBaseName(const String &name) {
  String result;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (ok) {
      result += c;
    }
  }
  if (result.length() == 0) {
    result = "image";
  }
  return result;
}

static uint16_t maxColumnsForWidth(uint16_t width) {
  uint32_t rowSize = (static_cast<uint32_t>(width) * 3 + 3) & ~3U;
  if (rowSize == 0) {
    return 0;
  }
  uint32_t maxHeight = 64000 / rowSize;
  if (maxHeight > 65535) {
    maxHeight = 65535;
  }
  return static_cast<uint16_t>(maxHeight);
}

static void handleIndex() {
  webserver->send_P(200, "text/html", kIndexHtml);
}

static void handleApiList() {
  std::vector<String> items;
  if (!readImageList("/imagelist.txt", items)) {
    listBmpFiles(items);
  }
  uint16_t width = STAFF_NUM_PIXELS;
  uint16_t maxColumns = maxColumnsForWidth(width);

  String payload = "{\"width\":" + String(width) + ",\"maxColumns\":" + String(maxColumns) + ",\"items\":[";
  for (size_t i = 0; i < items.size(); i++) {
    payload += "\"" + items[i] + "\"";
    if (i + 1 < items.size()) {
      payload += ",";
    }
  }
  payload += "]}";
  webserver->send(200, "application/json", payload);
}

static void handleApiOrder() {
  String body = webserver->arg("plain");
  std::vector<String> files;
  int start = 0;
  while (start < body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) {
      end = body.length();
    }
    String line = body.substring(start, end);
    line.trim();
    if (line.length() > 0) {
      if (!line.startsWith("/")) {
        line = "/" + line;
      }
      files.push_back(line);
    }
    start = end + 1;
  }

  bool ok = writeImageList("/imagelist.txt", files);
  String payload = ok ? "{\"ok\":true,\"message\":\"Order saved\"}"
                      : "{\"ok\":false,\"message\":\"Failed to write imagelist\"}";
  webserver->send(ok ? 200 : 500, "application/json", payload);
}

static void handleApiUploadFinish() {
  if (!uploadReady) {
    webserver->send(400, "application/json", "{\"ok\":false,\"message\":\"No upload\"}");
    return;
  }

  String payload = "{\"ok\":" + String(uploadOk ? "true" : "false") + ",\"message\":\"" + uploadStatusMessage + "\"}";
  webserver->send(uploadOk ? 200 : 500, "application/json", payload);
  uploadReady = false;
}

static void handleApiUploadChunk() {
  HTTPUpload &upload = webserver->upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadReady = true;
    uploadOk = false;
    uploadStatusMessage = "Upload failed";
    uploadBytes = 0;
    uploadTooLarge = false;
    String baseName = sanitizeBaseName(webserver->arg("name"));
    uploadDestPath = "/" + baseName + ".bmp";
    uploadRotateCw = webserver->arg("rotate") == "1";
    uint16_t maxColumns = maxColumnsForWidth(STAFF_NUM_PIXELS);
    int requested = webserver->arg("columns").toInt();
    if (requested > 0) {
      if (requested > static_cast<int>(maxColumns)) {
        requested = static_cast<int>(maxColumns);
      }
      uploadRequestedHeight = static_cast<uint16_t>(requested);
    } else {
      uploadRequestedHeight = 0;
    }
    uploadFile = LittleFS.open(uploadTempPath, "w");
    if (!uploadFile) {
      uploadStatusMessage = "Failed to open temp file";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadBytes += upload.currentSize;
      if (uploadBytes > kMaxUploadBytes) {
        uploadTooLarge = true;
        uploadStatusMessage = "Upload too large";
      } else {
        size_t written = uploadFile.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          uploadStatusMessage = "Failed to write upload";
        }
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!uploadFile) {
      uploadOk = false;
      uploadStatusMessage = "Upload failed";
      return;
    }
    uploadFile.close();
    if (uploadTooLarge) {
      uploadOk = false;
      LittleFS.remove(uploadTempPath);
      return;
    }
    uint16_t maxColumns = maxColumnsForWidth(STAFF_NUM_PIXELS);
    ImageProcessResult result = processBmpToStaff(uploadTempPath.c_str(),
                                                  uploadDestPath.c_str(),
                                                  STAFF_NUM_PIXELS,
                                                  maxColumns,
                                                  uploadRotateCw,
                                                  uploadRequestedHeight);
    if (result.ok) {
      upsertImageInList("/imagelist.txt", uploadDestPath);
      uploadOk = true;
      uploadStatusMessage = "Saved " + uploadDestPath + " (" + result.outWidth + "x" + result.outHeight + ")";
    } else {
      uploadOk = false;
      uploadStatusMessage = result.message;
    }
    LittleFS.remove(uploadTempPath);
  }
}

// WARNING: This webserver has no access control. It servers everything from your ESP32 file system.
// WARNING: This webserver has no access control. It servers everything from your ESP32 file system.
WebServer * webserver = NULL;
const word webserverServerPort = 80;
const String webserverDefaultname = "/index.html";
// WARNING: This webserver has no access control. It servers everything from your ESP32 file system.
// WARNING: This webserver has no access control. It servers everything from your ESP32 file system.

const char kIndexHtml[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>POV Staff Loader</title>
  <style>
    :root {
      --ink: #1a1612;
      --muted: #5e5752;
      --paper: #f6f1e7;
      --paper-dark: #efe4d2;
      --accent: #1f6f66;
      --accent-2: #c6792a;
      --line: rgba(26, 22, 18, 0.15);
      --shadow: 0 12px 30px rgba(26, 22, 18, 0.12);
    }

    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Georgia", "Times New Roman", serif;
      background: radial-gradient(circle at 20% 10%, #fffaf2, #efe4d2 40%, #e7d7c2 100%);
      color: var(--ink);
    }
    .wrap {
      max-width: 860px;
      margin: 0 auto;
      padding: 28px 18px 40px;
    }
    header {
      display: flex;
      flex-direction: column;
      gap: 6px;
      margin-bottom: 20px;
    }
    h1 {
      font-size: 28px;
      margin: 0;
      letter-spacing: 0.4px;
    }
    .subtitle {
      color: var(--muted);
      font-size: 14px;
    }
    .card {
      background: var(--paper);
      border: 1px solid var(--line);
      border-radius: 16px;
      padding: 18px;
      box-shadow: var(--shadow);
      margin-bottom: 16px;
    }
    label {
      font-size: 13px;
      color: var(--muted);
      display: block;
      margin-bottom: 6px;
    }
    input[type="text"], input[type="number"] {
      width: 100%;
      padding: 10px 12px;
      border-radius: 10px;
      border: 1px solid var(--line);
      font-size: 15px;
      background: #fff;
    }
    input[type="file"] {
      width: 100%;
    }
    .row {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    }
    .toggle {
      display: flex;
      align-items: center;
      gap: 8px;
      font-size: 14px;
    }
    .actions {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      margin-top: 12px;
    }
    button {
      border: none;
      border-radius: 999px;
      padding: 10px 16px;
      font-size: 14px;
      cursor: pointer;
      background: var(--accent);
      color: #fff;
      transition: transform 0.12s ease, background 0.12s ease;
    }
    button.secondary {
      background: #fff;
      color: var(--accent);
      border: 1px solid var(--accent);
    }
    button:active { transform: scale(0.98); }
    .status {
      font-size: 14px;
      margin-top: 8px;
      color: var(--muted);
    }
    ul {
      list-style: none;
      padding: 0;
      margin: 0;
    }
    li {
      display: grid;
      grid-template-columns: 1fr auto;
      align-items: center;
      gap: 10px;
      padding: 10px 12px;
      border-radius: 12px;
      background: #fff;
      border: 1px solid var(--line);
      margin-bottom: 8px;
    }
    .file-name { font-size: 14px; }
    .order-buttons {
      display: flex;
      gap: 6px;
    }
    .order-buttons button {
      padding: 6px 10px;
      font-size: 12px;
      background: var(--accent-2);
    }
    .meta {
      font-size: 12px;
      color: var(--muted);
      margin-top: 6px;
    }
    @media (max-width: 520px) {
      h1 { font-size: 24px; }
      .card { padding: 16px; }
      li { grid-template-columns: 1fr; }
      .order-buttons { justify-content: flex-start; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <header>
      <h1>POV Staff Loader</h1>
      <div class="subtitle">Upload images, auto-scale for the staff, and set the play order.</div>
    </header>

    <section class="card">
      <div class="row">
        <div>
          <label for="file">Image file (any format)</label>
          <input id="file" type="file" accept="image/*" />
        </div>
        <div>
          <label for="name">Save as (no extension)</label>
          <input id="name" type="text" placeholder="moonrise" />
        </div>
        <div>
          <label for="columns">Columns (optional)</label>
          <input id="columns" type="number" min="1" placeholder="auto" />
        </div>
      </div>
      <div class="row" style="margin-top:12px;">
        <label class="toggle">
          <input id="rotate" type="checkbox" /> Rotate 90° clockwise
        </label>
      </div>
      <div class="actions">
        <button id="upload">Upload + Process</button>
        <button id="refresh" class="secondary">Refresh List</button>
      </div>
      <div class="status" id="uploadStatus">Waiting for upload.</div>
      <div class="meta" id="meta"></div>
      <div class="meta">Uploads are downscaled to max 512px before processing.</div>
    </section>

    <section class="card">
      <div style="display:flex;align-items:center;justify-content:space-between;gap:12px;">
        <div>
          <div style="font-size:16px;">Image Order</div>
          <div class="meta">Move items up/down, then save.</div>
        </div>
        <button id="saveOrder" class="secondary">Save Order</button>
      </div>
      <ul id="list" style="margin-top:12px;"></ul>
      <div class="status" id="orderStatus"></div>
    </section>
  </div>

  <script>
    const state = { items: [], width: 72, maxColumns: 296 };

    function slugify(name) {
      return name.replace(/\.[^/.]+$/, "").replace(/[^a-zA-Z0-9-_]/g, "");
    }

    async function loadList() {
      const res = await fetch('/api/list');
      const data = await res.json();
      state.items = data.items || [];
      state.width = data.width || 72;
      state.maxColumns = data.maxColumns || 296;
      document.getElementById('meta').textContent = `Staff width: ${state.width} px | Max columns: ${state.maxColumns}`;
      renderList();
    }

    function renderList() {
      const list = document.getElementById('list');
      list.innerHTML = '';
      state.items.forEach((name, index) => {
        const li = document.createElement('li');
        const label = document.createElement('div');
        label.className = 'file-name';
        label.textContent = name;
        const buttons = document.createElement('div');
        buttons.className = 'order-buttons';
        const up = document.createElement('button');
        up.textContent = 'Up';
        up.onclick = () => move(index, -1);
        const down = document.createElement('button');
        down.textContent = 'Down';
        down.onclick = () => move(index, 1);
        buttons.append(up, down);
        li.append(label, buttons);
        list.appendChild(li);
      });
    }

    function move(index, delta) {
      const target = index + delta;
      if (target < 0 || target >= state.items.length) return;
      const item = state.items.splice(index, 1)[0];
      state.items.splice(target, 0, item);
      renderList();
    }

    async function loadImageBitmap(file) {
      if (window.createImageBitmap) {
        return await createImageBitmap(file);
      }
      return await new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => {
          const img = new Image();
          img.onload = () => resolve(img);
          img.onerror = reject;
          img.src = reader.result;
        };
        reader.onerror = reject;
        reader.readAsDataURL(file);
      });
    }

    async function fileToBmp(file) {
      const image = await loadImageBitmap(file);
      const maxInput = 512;
      const scale = Math.min(1, maxInput / Math.max(image.width, image.height));
      const width = Math.max(1, Math.round(image.width * scale));
      const height = Math.max(1, Math.round(image.height * scale));
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      const ctx = canvas.getContext('2d');
      ctx.drawImage(image, 0, 0, width, height);
      const imageData = ctx.getImageData(0, 0, width, height).data;
      const rowSize = (width * 3 + 3) & ~3;
      const dataSize = rowSize * height;
      const buffer = new ArrayBuffer(54 + dataSize);
      const view = new DataView(buffer);
      const write16 = (offset, value) => { view.setUint16(offset, value, true); };
      const write32 = (offset, value) => { view.setUint32(offset, value, true); };
      view.setUint8(0, 0x42);
      view.setUint8(1, 0x4d);
      write32(2, 54 + dataSize);
      write32(6, 0);
      write32(10, 54);
      write32(14, 40);
      write32(18, width);
      write32(22, height);
      write16(26, 1);
      write16(28, 24);
      write32(30, 0);
      write32(34, dataSize);
      write32(38, 2835);
      write32(42, 2835);
      write32(46, 0);
      write32(50, 0);

      let offset = 54;
      for (let y = height - 1; y >= 0; y--) {
        for (let x = 0; x < width; x++) {
          const idx = (y * width + x) * 4;
          const r = imageData[idx];
          const g = imageData[idx + 1];
          const b = imageData[idx + 2];
          view.setUint8(offset++, b);
          view.setUint8(offset++, g);
          view.setUint8(offset++, r);
        }
        while ((offset - 54) % rowSize !== 0) {
          view.setUint8(offset++, 0);
        }
      }

      return new Blob([buffer], { type: 'image/bmp' });
    }

    async function upload() {
      const fileInput = document.getElementById('file');
      const nameInput = document.getElementById('name');
      const columnsInput = document.getElementById('columns');
      const rotateInput = document.getElementById('rotate');
      const status = document.getElementById('uploadStatus');

      if (!fileInput.files.length) {
        status.textContent = 'Choose a file first.';
        return;
      }

      const file = fileInput.files[0];
      const baseName = nameInput.value.trim() || slugify(file.name) || 'image';
      const columns = columnsInput.value.trim();
      const rotate = rotateInput.checked ? '1' : '0';
      const params = new URLSearchParams({ name: baseName, rotate });
      if (columns) params.set('columns', columns);

      const data = new FormData();
      const bmpBlob = await fileToBmp(file);
      data.append('file', bmpBlob, `${baseName}.bmp`);

      status.textContent = 'Uploading...';
      const res = await fetch(`/api/upload?${params.toString()}`, { method: 'POST', body: data });
      const payload = await res.json();
      status.textContent = payload.message || (payload.ok ? 'Uploaded.' : 'Upload failed.');
      if (payload.ok) {
        nameInput.value = '';
        fileInput.value = '';
        columnsInput.value = '';
        await loadList();
      }
    }

    async function saveOrder() {
      const status = document.getElementById('orderStatus');
      status.textContent = 'Saving order...';
      const body = state.items.join('\n');
      const res = await fetch('/api/order', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body });
      const payload = await res.json();
      status.textContent = payload.message || (payload.ok ? 'Order saved.' : 'Save failed.');
    }

    document.getElementById('upload').addEventListener('click', upload);
    document.getElementById('refresh').addEventListener('click', loadList);
    document.getElementById('saveOrder').addEventListener('click', saveOrder);
    document.getElementById('file').addEventListener('change', (e) => {
      const file = e.target.files[0];
      if (!file) return;
      const nameInput = document.getElementById('name');
      if (!nameInput.value.trim()) {
        nameInput.value = slugify(file.name) || 'image';
      }
    });

    loadList();
  </script>
</body>
</html>
)rawliteral";

//*****************************************************************************************************
void setupWebserver(void) {
  webserver = new WebServer(webserverServerPort);

  // ACCEPT-ENCODING  gzip, deflate, br
  const char * headerkeys[] = {"ACCEPT-ENCODING"} ;
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  //ask server to track these headers
  webserver->collectHeaders(headerkeys, headerkeyssize );

  // add your .ons here
  // ...
  // example for data requester
  webserver->on("/d", webserverServerDataRequest);
  webserver->on("/", HTTP_GET, handleIndex);
  webserver->on("/index.html", HTTP_GET, handleIndex);
  webserver->on("/api/list", HTTP_GET, handleApiList);
  webserver->on("/api/order", HTTP_POST, handleApiOrder);
  webserver->on("/api/upload", HTTP_POST, handleApiUploadFinish, handleApiUploadChunk);

  // Everything else is served by this.
  webserver->onNotFound(webserverServerNotFound);
  webserver->begin();
}

//*****************************************************************************************************
void webserverServerNotFound(void) {
  genericWebserverServerNotFound(webserver);
}

//*****************************************************************************************************
void loopWebServer(void) {
  if (webserver) {
    webserver->handleClient();
  }
}


//*****************************************************************************************************
//*.on-Event Handlers
//*****************************************************************************************************
void webserverServerDataRequest(void) {
  webserver->send(200, F("text/plain"), F("Wonderfull."));
  delay(1);
}


//*****************************************************************************************************
//*internals.
//*****************************************************************************************************
// 404
void genericWebServerHandleNotFound(WebServer * webserver) {
  /** /
    Serial.print(webserver->client().remoteIP().toString());
    Serial.print(F(" 404: "));
    Serial.println(webserver->uri());
    /**/

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += webserver->uri();
  message += "\nhostheader: ";
  message += webserver->hostHeader();
  message += "\nMethod: ";
  message += (webserver->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webserver->args();
  message += "\n";
  for (uint8_t i = 0; i < webserver->args(); i++) {
    message += " " + webserver->argName(i) + ": " + webserver->arg(i) + "\n";
  }
  message += "\nHeaders: ";
  message += webserver->headers();
  message += "\n";
  for (uint8_t i = 0; i < webserver->headers(); i++) {
    message += " " + webserver->headerName(i) + ": " + webserver->header(i) + "\n";
  }

  // IE will mindesten 512 Byte, um es anzuzeigen, sonst interne IE-Seite....
  while (message.length() < 512) {
    message += "                         \n";
  }
  webserver->send(404, "text/plain", message);
}

//*****************************************************************************************************
void genericWebserverServerNotFound(WebServer * webserver) {
  String uri = webserver->uri();

  // default name
  if (uri == "/") {
    uri = webserverDefaultname;
  }

  // String contentTyp = StaticRequestHandler::getContentType(uri);
  String contentTyp = getContentTypeLocal(uri);

  // Are we allowd to send compressed data?
  // (What is more expensive? Checking LittleFS or header first?)
  if (webserver->hasHeader("ACCEPT-ENCODING")) {
    if (webserver->header("ACCEPT-ENCODING").indexOf("gzip") != -1) {
      // gzip version exists?
      if (LittleFS.exists(uri + ".gz"))  {
        uri += ".gz";
      }
    }
  }

  if (LittleFS.exists(uri)) {
    File f = LittleFS.open(uri, "r");
    if (f) {
      if (webserver->streamFile(f, contentTyp) != f.size()) {
        Serial.println(F("Panic: Sent less data than expected!"));
      }
      f.close();
    }
  } else {
    genericWebServerHandleNotFound(webserver);
  }

  delay(1);
}

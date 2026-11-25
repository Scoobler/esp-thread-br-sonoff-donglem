import { ESPLoader, Transport } from "https://unpkg.com/esptool-js@0.7.1/bundle.js";

const $ = (id) => document.getElementById(id);
const modeInputs = [...document.querySelectorAll('input[name="mode"]')];
const MODE = {
  fresh: { address: 0x0, manifestKey: "full", label: "Fresh installation", destructive: true },
  upgrade: { address: 0x20000, manifestKey: "upgrade", label: "Upgrade", destructive: false }
};
let releaseManifest = null;
let busy = false;
const log = (message) => {
  const time = new Date().toLocaleTimeString();
  $("log").textContent += `[${time}] ${String(message)}\n`;
  $("log").scrollTop = $("log").scrollHeight;
};
const status = (message) => { $("status").textContent = message; log(message); };
const currentMode = () => MODE[modeInputs.find(i => i.checked).value];

function refresh() {
  const mode = currentMode();
  const custom = $("source").value === "custom";
  $("latest-details").hidden = custom;
  $("custom-details").hidden = !custom;
  $("mode-warning").innerHTML = mode.destructive
    ? "Fresh installation erases <strong>all ESP32 flash</strong>, including Wi-Fi credentials and the Thread dataset. Back up your dataset first. MG24 firmware is not flashed."
    : "Upgrade writes only the ESP32 application slots and Web UI from address <strong>0x20000</strong>. NVS, Wi-Fi credentials, Thread dataset, OTA selection and PHY calibration are not intentionally overwritten. Back up your dataset as a precaution.";
  $("confirmation-label").lastChild.textContent = mode.destructive
    ? " I have backed up my Thread dataset and accept that all ESP32 settings will be erased."
    : " I have backed up my Thread dataset and understand this is a USB firmware upgrade.";
  $("flash").disabled = busy || !$("confirm").checked ||
    (custom ? !$("custom-file").files.length : !releaseManifest);
}
modeInputs.forEach(i => i.addEventListener("change", () => {
  // The acknowledgement belongs to the selected operation; changing
  // between Upgrade and Fresh installation requires a new acknowledgement.
  $("confirm").checked = false;
  refresh();
}));
$("source").addEventListener("change", refresh);
$("custom-file").addEventListener("change", refresh);
$("confirm").addEventListener("change", refresh);

const bytesToHex = (array) => [...array].map(x => x.toString(16).padStart(2, "0")).join("");
const sha256 = async (data) => bytesToHex(new Uint8Array(await crypto.subtle.digest("SHA-256", data)));
const formatMiB = (n) => (n / 1024 / 1024).toFixed(2) + " MiB";

async function getImage(mode) {
  const custom = $("source").value === "custom";
  if (custom) {
    const file = $("custom-file").files[0];
    if (!file) throw Error("Choose a firmware image");
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length === 0) throw Error("Selected firmware file is empty");
    // Merged full images are based at 0x0 but the bootloader begins at 0x1000.
    if (mode.destructive && (bytes.length <= 0x1000 || bytes[0x1000] !== 0xe9)) {
      throw Error("Fresh install merged image must contain the ESP32 bootloader at 0x1000 (magic 0xE9)");
    }
    if (!mode.destructive && bytes[0] !== 0xe9) {
      throw Error("Upgrade image should start with an ESP32 application header (0xE9)");
    }
    status(`Loaded custom file ${file.name}, ${formatMiB(bytes.length)}`);
    return bytes;
  }
  const record = releaseManifest[mode.manifestKey];
  if (!record || !record.file || !/^[a-f0-9]{64}$/i.test(record.sha256)) {
    throw Error("Invalid published firmware manifest");
  }
  const url = new URL(record.file, window.location.href);
  if (url.origin !== window.location.origin) throw Error("Firmware URL must be hosted on this site");
  status(`Downloading ${record.file}…`);
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok) throw Error(`Firmware download failed (${response.status})`);
  const data = await response.arrayBuffer();
  status(`Verifying SHA-256 of ${formatMiB(data.byteLength)} image…`);
  const digest = await sha256(data);
  if (digest.toLowerCase() !== record.sha256.toLowerCase()) {
    throw Error("SHA-256 mismatch: refusing to flash firmware");
  }
  status("Firmware checksum verified.");
  return new Uint8Array(data);
}

async function flash() {
  if (busy) return;
  const mode = currentMode();
  const custom = $("source").value === "custom";
  if (!$("confirm").checked || (!custom && !releaseManifest)) return;
  // The explicit mode-specific acknowledgement checkbox above is the
  // confirmation. Do not show a redundant native browser confirm dialog.
  busy = true; refresh();
  $("progress").value = 0;
  let transport = null;
  try {
    if (!("serial" in navigator)) throw Error("Web Serial is unavailable. Use desktop Chrome or Edge over HTTPS.");
    // requestPort requires transient user activation. Prompt before fetching
    // and hashing a multi-megabyte image, which can outlast that activation.
    status("Select the Dongle-M USB serial port in the browser prompt.");
    const port = await navigator.serial.requestPort();
    const image = await getImage(mode);
    transport = new Transport(port, true);
    const terminal = {
      clean() {},
      writeLine(text) { log(text); },
      write(text) { if (String(text).trim()) log(text); }
    };
    const loader = new ESPLoader({ transport, baudrate: 115200, terminal, debugLogging: false });
    const chip = await loader.main();
    status(`Connected: ${chip}`);
    // esptool-js may report the classic ESP32 as "Unknown ESP32 (revision 3)".
    // Check variant suffixes only immediately after "ESP32": a broad /P[0-9]/
    // would incorrectly reject the "P3" within the name "ESP32" itself.
    const chipMatch = String(chip).match(/\bESP32(?:[-_]?([SCHP]\d+))?\b/i);
    if (!chipMatch || chipMatch[1]) {
      throw Error(`Unsupported chip ${chip}. Dongle-M requires the original ESP32.`);
    }
    if (mode.destructive) {
      status("Erasing entire ESP32 flash…");
      await loader.eraseFlash();
      status("Full chip erase complete. Programming merged firmware at 0x0…");
    } else {
      status("Programming upgrade image at 0x20000 without full-chip erase…");
    }
    await loader.writeFlash({
      fileArray: [{ data: image, address: mode.address }],
      eraseAll: false,
      compress: true,
      flashMode: mode.destructive ? "dio" : "keep",
      flashFreq: mode.destructive ? "40m" : "keep",
      flashSize: mode.destructive ? "16MB" : "keep",
      reportProgress(_index, written, total) {
        $("progress").value = Math.min(100, 100 * written / total);
      }
    });
    status("Firmware written and verified. Restarting ESP32…");
    await loader.after("hard_reset");
    status("Flash complete. The Dongle-M may take a short while to rejoin the network.");
  } catch (error) {
    status(`Error: ${error.message || error}`);
  } finally {
    if (transport) {
      try { await transport.disconnect(); } catch (e) { log(`Disconnect: ${e.message || e}`); }
    }
    busy = false;
    refresh();
  }
}
$("flash").addEventListener("click", flash);

async function loadManifest() {
  try {
    const response = await fetch("./firmware.json", { cache: "no-store" });
    if (!response.ok) throw Error("No published firmware manifest available yet");
    const json = await response.json();
    if (!json.version || json.full?.offset !== 0 || json.upgrade?.offset !== 0x20000 ||
        !json.full?.file || !json.upgrade?.file) throw Error("Firmware manifest is invalid");
    releaseManifest = json;
    $("release").textContent = `Published release: ${json.version} · Fresh: ${json.full.file} · Upgrade: ${json.upgrade.file}`;
    status(`Release ${json.version} available.`);
  } catch (error) {
    $("release").textContent = `Published release unavailable: ${error.message}. You can still use a trusted custom file.`;
    status("No published firmware loaded.");
  }
  refresh();
}
loadManifest();

const { app, BrowserWindow, Menu, dialog, ipcMain, shell } = require("electron");
const { spawn } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

app.disableHardwareAcceleration();
app.commandLine.appendSwitch("disable-gpu");
app.commandLine.appendSwitch("disable-gpu-sandbox");
app.commandLine.appendSwitch("disable-direct-composition");
app.commandLine.appendSwitch("disable-features", "DirectComposition,CalculateNativeWinOcclusion");

const isDev = Boolean(process.env.VITE_DEV_SERVER_URL);
const logPath = path.resolve(__dirname, "..", "logs", "main.log");

function log(message) {
  const line = `[${new Date().toISOString()}] ${message}\n`;
  try {
    fs.mkdirSync(path.dirname(logPath), { recursive: true });
    fs.appendFileSync(logPath, line, "utf8");
  } catch (_) {
    // Logging must never prevent the desktop shell from opening.
  }
}

process.on("uncaughtException", (error) => {
  log(`uncaughtException: ${error?.stack ?? error}`);
});

process.on("unhandledRejection", (reason) => {
  log(`unhandledRejection: ${reason?.stack ?? reason}`);
});

function resolveCoreExe() {
  if (process.env.HAORENDER_CORE_PATH && fs.existsSync(process.env.HAORENDER_CORE_PATH)) {
    return process.env.HAORENDER_CORE_PATH;
  }
  const candidates = [
    path.resolve(__dirname, "..", "..", "build", "Release", "haorender-core.exe"),
    path.resolve(__dirname, "..", "..", "build", "Debug", "haorender-core.exe"),
    path.resolve(process.resourcesPath ?? "", "haorender-core.exe")
  ];
  return candidates.find((candidate) => fs.existsSync(candidate)) ?? candidates[0];
}

function parseCoreJson(stdout) {
  const text = stdout.trim();
  if (!text) {
    return { ok: false, error: "haorender-core returned no JSON output." };
  }
  try {
    return JSON.parse(text);
  } catch (error) {
    return {
      ok: false,
      error: `Failed to parse haorender-core JSON: ${error.message}`,
      raw: text
    };
  }
}

function runCore(args) {
  return new Promise((resolve) => {
    const corePath = resolveCoreExe();
    const child = spawn(corePath, args, {
      cwd: path.resolve(__dirname, "..", ".."),
      windowsHide: true
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString("utf8");
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString("utf8");
    });
    child.on("error", (error) => {
      resolve({
        ok: false,
        command: args[0]?.replace(/^--/, "") ?? "unknown",
        error: error.message,
        corePath
      });
    });
    child.on("close", (code) => {
      const parsed = parseCoreJson(stdout);
      parsed.exitCode = code;
      parsed.stderr = stderr.trim();
      parsed.corePath = corePath;
      resolve(parsed);
    });
  });
}

function defaultOutputPath(sourcePath, targetFormat) {
  const parsed = path.parse(sourcePath);
  const outputDir = path.resolve(parsed.dir, "HAORENDER-AI-Converted");
  const extension = targetFormat.toLowerCase().replace(/^\./, "");
  return path.join(outputDir, `${parsed.name}.${extension}`);
}

function createWindow() {
  log(`createWindow dev=${isDev} core=${resolveCoreExe()}`);
  Menu.setApplicationMenu(null);
  const win = new BrowserWindow({
    width: 1440,
    height: 940,
    minWidth: 1180,
    minHeight: 760,
    show: false,
    backgroundColor: "#0d1117",
    title: "HAORENDER-AI",
    autoHideMenuBar: true,
    titleBarStyle: "default",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false,
      backgroundThrottling: false
    }
  });
  win.once("ready-to-show", () => {
    log("ready-to-show");
    win.show();
    win.focus();
  });

  win.webContents.on("did-fail-load", (_event, errorCode, errorDescription, validatedURL) => {
    log(`did-fail-load code=${errorCode} description=${errorDescription} url=${validatedURL}`);
  });
  win.webContents.on("render-process-gone", (_event, details) => {
    log(`render-process-gone reason=${details.reason} exitCode=${details.exitCode}`);
  });
  win.webContents.on("console-message", (_event, level, message, line, sourceId) => {
    log(`renderer console level=${level} ${sourceId}:${line} ${message}`);
  });
  win.webContents.on("did-finish-load", async () => {
    try {
      win.webContents.setZoomFactor(1);
      const state = await win.webContents.executeJavaScript(`
        (() => {
          const root = document.getElementById("root");
          const bodyText = document.body ? document.body.innerText.slice(0, 240) : "";
          const htmlText = document.body ? document.body.innerHTML.slice(0, 500) : "";
          return {
            href: location.href,
            readyState: document.readyState,
            title: document.title,
            scriptCount: document.scripts.length,
            styleSheetCount: document.styleSheets.length,
            rootExists: Boolean(root),
            rootChildCount: root ? root.childElementCount : -1,
            bodyText,
            htmlText,
            background: getComputedStyle(document.body).backgroundColor
          };
        })()
      `);
      log(`did-finish-load state=${JSON.stringify(state)}`);
      setTimeout(async () => {
        try {
          const image = await win.webContents.capturePage();
          const capturePath = path.resolve(__dirname, "..", "logs", "boot-capture.png");
          fs.mkdirSync(path.dirname(capturePath), { recursive: true });
          fs.writeFileSync(capturePath, image.toPNG());
          log(`boot capture written: ${capturePath}`);
        } catch (error) {
          log(`boot capture failed: ${error?.stack ?? error}`);
        }
      }, 1500);
    } catch (error) {
      log(`did-finish-load diagnostic failed: ${error?.stack ?? error}`);
    }
  });

  if (isDev) {
    win.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    const indexPath = path.join(__dirname, "..", "dist", "index.html");
    if (fs.existsSync(indexPath)) {
      win.loadFile(indexPath);
    } else {
      log(`dist index missing: ${indexPath}`);
      win.loadURL(`data:text/html;charset=utf-8,${encodeURIComponent(`
        <body style="margin:0;background:#0d1117;color:#e8edf4;font-family:Segoe UI,Microsoft YaHei,sans-serif;display:flex;align-items:center;justify-content:center;height:100vh">
          <main style="max-width:720px;padding:32px;border:1px solid #263241;border-radius:12px;background:#111821">
            <h1>HAORENDER-AI Desktop 未完成前端构建</h1>
            <p style="color:#94a3b8;line-height:1.7">请在 <code>HaoRender-GI/desktop</code> 下运行 <code>pnpm build</code>，然后重新启动。</p>
          </main>
        </body>
      `)}`);
    }
  }
}

app.whenReady().then(() => {
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});

ipcMain.handle("dialog:openModel", async () => {
  const result = await dialog.showOpenDialog({
    title: "Open model",
    properties: ["openFile"],
    filters: [
      { name: "Model files", extensions: ["fbx", "glb", "gltf", "vrm", "obj", "dae", "stl", "ply"] },
      { name: "All files", extensions: ["*"] }
    ]
  });
  return result.canceled ? null : result.filePaths[0];
});

ipcMain.handle("dialog:openRigAsset", async (_event, role) => {
  const result = await dialog.showOpenDialog({
    title: role === "source" ? "Open source animation" : "Open target character",
    properties: ["openFile"],
    filters: [
      { name: "Rig and animation files", extensions: ["fbx", "bvh", "glb", "gltf", "vrm", "vrma"] },
      { name: "All files", extensions: ["*"] }
    ]
  });
  return result.canceled ? null : result.filePaths[0];
});

ipcMain.handle("dialog:chooseOutput", async (_event, sourcePath, targetFormat) => {
  const result = await dialog.showSaveDialog({
    title: "Choose output model",
    defaultPath: sourcePath ? defaultOutputPath(sourcePath, targetFormat ?? "glb") : undefined,
    filters: [
      { name: `${String(targetFormat ?? "GLB").toUpperCase()} model`, extensions: [String(targetFormat ?? "glb").toLowerCase()] },
      { name: "All files", extensions: ["*"] }
    ]
  });
  return result.canceled ? null : result.filePath;
});

ipcMain.handle("core:listFormats", async () => runCore(["--list-export-formats"]));

ipcMain.handle("core:renderCapabilities", async () => runCore(["--render-capabilities"]));

ipcMain.handle("core:inspect", async (_event, sourcePath) => {
  const result = await runCore(["--inspect-model", sourcePath]);
  result.fileUrl = pathToFileURL(sourcePath).toString();
  return result;
});

ipcMain.handle("core:preview", async (_event, sourcePath) => {
  const parsed = path.parse(sourcePath);
  const previewPath = path.join(parsed.dir, "HAORENDER-AI-Converted", `${parsed.name}_preview.png`);
  const result = await runCore(["--render-preview", sourcePath, previewPath, "--width", "1280", "--height", "840"]);
  result.previewPath = previewPath;
  result.previewUrl = fs.existsSync(previewPath) ? pathToFileURL(previewPath).toString() : "";
  return result;
});

ipcMain.handle("core:convert", async (_event, payload) => {
  const sourcePath = payload?.sourcePath;
  const targetFormat = String(payload?.targetFormat ?? "glb").toLowerCase();
  const outputPath = payload?.outputPath || defaultOutputPath(sourcePath, targetFormat);
  const result = await runCore(["--convert-model", sourcePath, outputPath, targetFormat]);
  result.outputPath = outputPath;
  result.outputFileUrl = fs.existsSync(outputPath) ? pathToFileURL(outputPath).toString() : "";
  return result;
});

ipcMain.handle("core:rigMap", async (_event, payload) => {
  const targetPath = payload?.targetPath;
  const sourcePath = payload?.sourcePath;
  return runCore(["--rig-map", targetPath, sourcePath]);
});

ipcMain.handle("path:fileUrl", async (_event, filePath) => pathToFileURL(filePath).toString());

ipcMain.handle("shell:showItem", async (_event, filePath) => {
  if (filePath) {
    shell.showItemInFolder(filePath);
  }
});

const { spawn } = require("node:child_process");

const isWindows = process.platform === "win32";
const npmBin = isWindows ? "npx.cmd" : "npx";

const vite = spawn(npmBin, ["vite", "--host", "127.0.0.1", "--port", "5173", "--strictPort"], {
  stdio: "inherit",
  shell: false
});

let electron = null;
let attempts = 0;

async function waitForVite() {
  while (attempts < 80) {
    attempts += 1;
    try {
      const response = await fetch("http://127.0.0.1:5173");
      if (response.ok) {
        return;
      }
    } catch (_) {
      // Server is still starting.
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error("Vite dev server did not start in time.");
}

waitForVite()
  .then(() => {
    const electronBin = isWindows
      ? "node_modules\\.bin\\electron.cmd"
      : "node_modules/.bin/electron";
    electron = spawn(electronBin, ["."], {
      stdio: "inherit",
      shell: false,
      env: {
        ...process.env,
        VITE_DEV_SERVER_URL: "http://127.0.0.1:5173"
      }
    });
    electron.on("exit", (code) => {
      vite.kill();
      process.exit(code ?? 0);
    });
  })
  .catch((error) => {
    console.error(error);
    vite.kill();
    process.exit(1);
  });

process.on("SIGINT", () => {
  electron?.kill();
  vite.kill();
  process.exit(0);
});

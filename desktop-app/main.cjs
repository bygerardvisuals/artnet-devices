const { app, BrowserWindow, ipcMain, session } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

let selectedPorts = new Map();

function firmwarePath() {
  return app.isPackaged
    ? path.join(process.resourcesPath, 'app.asar', 'firmware', 'merged-firmware.bin')
    : path.join(__dirname, 'firmware', 'merged-firmware.bin');
}

function createWindow() {
  const window = new BrowserWindow({
    width: 980,
    height: 760,
    minWidth: 760,
    minHeight: 600,
    title: 'Art-Net Relay',
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true
    }
  });

  const ses = window.webContents.session;
  ses.setPermissionCheckHandler((_contents, permission) => permission === 'serial');
  ses.setDevicePermissionHandler((details) => details.deviceType === 'serial');
  ses.on('select-serial-port', (event, ports, webContents, callback) => {
    event.preventDefault();
    const wanted = selectedPorts.get(webContents.id);
    const selected = ports.find((port) => port.portId === wanted) || ports[0];
    callback(selected ? selected.portId : '');
  });

  window.loadFile('index.html');
}

app.whenReady().then(() => {
  ipcMain.handle('serial-ports', () => session.defaultSession.getAllSerialPorts());
  ipcMain.handle('select-serial-port', (event, portId) => selectedPorts.set(event.sender.id, portId));
  ipcMain.handle('firmware', () => fs.readFileSync(firmwarePath()));
  createWindow();
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});

app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('relayDesktop', {
  listPorts: () => ipcRenderer.invoke('serial-ports'),
  selectPort: (id) => ipcRenderer.invoke('select-serial-port', id),
  firmware: (target) => ipcRenderer.invoke('firmware', target)
});

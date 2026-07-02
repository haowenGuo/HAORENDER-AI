const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("haoRender", {
  openModel: () => ipcRenderer.invoke("dialog:openModel"),
  openRigAsset: (role) => ipcRenderer.invoke("dialog:openRigAsset", role),
  chooseOutput: (sourcePath, targetFormat) => ipcRenderer.invoke("dialog:chooseOutput", sourcePath, targetFormat),
  listFormats: () => ipcRenderer.invoke("core:listFormats"),
  renderCapabilities: () => ipcRenderer.invoke("core:renderCapabilities"),
  inspectModel: (sourcePath) => ipcRenderer.invoke("core:inspect", sourcePath),
  renderPreview: (sourcePath) => ipcRenderer.invoke("core:preview", sourcePath),
  convertModel: (payload) => ipcRenderer.invoke("core:convert", payload),
  rigMap: (payload) => ipcRenderer.invoke("core:rigMap", payload),
  toFileUrl: (filePath) => ipcRenderer.invoke("path:fileUrl", filePath),
  showItemInFolder: (filePath) => ipcRenderer.invoke("shell:showItem", filePath)
});

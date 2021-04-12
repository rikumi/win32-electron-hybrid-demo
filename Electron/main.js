const { app, BrowserWindow, dialog } = require('electron');
const path = require('path');
const ref = require('ref');
const ffi = require('ffi');
const { DTypes } = require('win32-def');

let mainWindow = null;
let hWndMain = null;
let hWndParent = null;

const {
  INT, UINT, DWORD, LRESULT,
  LPDWORD, LPCWSTR, LPVOID,
  HHOOK, HINSTANCE,
  WPARAM, LPARAM
} = DTypes;

// 天坑,这个HWND类型不能直接用handle之类的buffer类型,ffi对buffer似乎做了错误的转换
// 需要定义成整数,传入整数,才能生成正常的小端序buffer
const HWND = 'uint64';

const User32 = ffi.Library('user32', {
  GetParent: [HWND, [HWND]],

  // DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId);
  GetWindowThreadProcessId: [DWORD, [HWND, LPDWORD]],

  // HHOOK SetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);
  SetWindowsHookExA: [HHOOK, [INT, LPVOID, HINSTANCE, DWORD]],

  // int MessageBoxA(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
  MessageBoxA: [INT, [HWND, LPCWSTR, LPCWSTR, UINT]],

  // LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
  SendMessageA: [LRESULT, [HWND, UINT, WPARAM, LPARAM]],
});

// process.on('uncaughtException', (e) => {
//   let message = e.stack;
//   User32.MessageBoxA(hWndMain, message, 'Error', 0);
// });

// process.on('unhandledRejection', (e) => { throw e })

app.on('ready', () => {
  mainWindow = new BrowserWindow({
    show: false
  });

  mainWindow.loadURL('file://' + path.join(__dirname, 'renderer.html'));
  mainWindow.on('ready-to-show', () => {
    mainWindow.show();
    mainWindow.webContents.openDevTools();
  });

  hWndMain = mainWindow.getNativeWindowHandle().readUInt32LE();

  // let dwThreadId = User32.GetWindowThreadProcessId(hWndMain, null);

  // const WH_CALLWNDPROC = 4;
  // let lpfnCallback = ffi.Callback(LONG, [INT, WPARAM, LPARAM], (code, wParam, lParam) => {
  //   if (code === 0x004A) {
  //     return 0;
  //   }
  // });
  
  // User32.SetWindowsHookExW(WH_CALLWNDPROC, lpfnCallback, 0, dwThreadId);

  mainWindow.hookWindowMessage(0x004A, (wParam, lParam) => {
    hWndParent = User32.GetParent(hWndMain);
    const pointLen = Buffer.alloc(0).ref().length;
    let copyDataBuffer = ref.readPointer(lParam, 0, pointLen * 3);
    let dwData = copyDataBuffer.subarray(0, pointLen).readInt32LE();
    let cbData = copyDataBuffer.subarray(pointLen, pointLen * 2).readInt32LE();
    let lpDataBuffer = copyDataBuffer.subarray(pointLen * 2, pointLen * 3);
    let strBuffer = ref.readPointer(lpDataBuffer, 0, Number(cbData));
    let str = strBuffer.toString();
    dialog.showMessageBox({title: 'received', message: str });
    
    // 发送
    process.nextTick(() => {
      str = 'Message from Node ' + process.version + '\0';
      strBuffer = Buffer.from(str);
      cbData = strBuffer.length;
      copyDataBuffer.writeInt32LE(cbData, pointLen);
      // 延长copyDataBuffer的生命周期,闭包结束时再随copyDataBuffer一起被回收
      ref.writePointer(copyDataBuffer, pointLen * 2, strBuffer);
      // 同步调用,等native处理完该消息后再返回
      User32.SendMessageA(hWndParent, 0x004A, hWndMain, copyDataBuffer.address());
    });

    return true;
  });
});

app.on('window-all-closed', () => {
  // app.quit();
  process.exit(0);
});
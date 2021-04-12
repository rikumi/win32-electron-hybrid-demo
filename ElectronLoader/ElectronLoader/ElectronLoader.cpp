/**
 * 调起 Electron 程序并最大化挂载到当前窗口下
 * 支持跟随当前窗口调节大小、退出时清理 Electron 进程等。
 */

#include "stdafx.h"
#include "resource.h"
#include "ElectronLoader.h"

#include <shellapi.h>
#include <string>
#include <TlHelp32.h>
#include "time.h"

#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;                     // 当前实例
WCHAR szTitle[MAX_LOADSTRING];       // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING]; // 主窗口类名
HWND hWndChild = NULL;               // Electron 的窗口句柄
HANDLE hProcessChild = NULL;         // Electron 的进程句柄

// 此代码模块中包含的函数的前向声明:
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
VOID CallUpElectron(HWND);
void sendMessageToElectron(HWND recvHwnd, HWND thisHwnd);
void handleCopyDataMessage(HWND hWnd, WPARAM wParam, LPARAM lParam);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ELECTRONLOADER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ELECTRONLOADER));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDC_ELECTRONLOADER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_ELECTRONLOADER);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // 将实例句柄存储在全局变量中

    SetProcessDPIAware();

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, 0, 1800, 1200, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

	// 允许低权限进程回发消息
	ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 启动时调起 Electron 进程
    CallUpElectron(hWnd);

    return TRUE;
}

/**
 * 事件处理
 */
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        // 窗口调整大小时，同步更新 Electron 窗口大小
        if (hWndChild)
        {
            MoveWindow(hWndChild, 0, 0, LOWORD(lParam), HIWORD(lParam), false);
        }
        break;
	case WM_COPYDATA:
		{
			handleCopyDataMessage(hWnd, wParam, lParam);
		}
		break;
    case WM_DESTROY:
        // 退出程序时，结束 Electron 进程
        if (hProcessChild)
        {
            TerminateProcess(hProcessChild, 0);
            hProcessChild = NULL;
            hWndChild = NULL;
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

/**
 * 获取进程的根进程，以便于确定某个 hWnd 是否是 Electron 的主窗口
 */
DWORD GetRootProcess(DWORD dwProcessId)
{

    // 一次性获取当前进程列表
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    BOOL bAtRoot = FALSE;

    // 循环查找进程的父进程，直到没有父进程
    while (!bAtRoot)
    {
        PROCESSENTRY32 lpProcEntry;
        BOOL bContinue = Process32First(hSnapshot, &lpProcEntry);
        DWORD pid = 0;

        // 默认没有找到父进程
        bAtRoot = TRUE;

        // 在进程列表中查找父进程
        while (bContinue)
        {
            if (dwProcessId == lpProcEntry.th32ProcessID)
            {
                // 查找到父进程，表示没有到达根进程，继续下一次大循环
                dwProcessId = lpProcEntry.th32ParentProcessID;
                bAtRoot = FALSE;
                break;
            }

            lpProcEntry.dwSize = sizeof(PROCESSENTRY32);
            bContinue = !pid && Process32Next(hSnapshot, &lpProcEntry);
        }
    }

    return dwProcessId;
}

// 调起 Electron 并显示为子窗口
VOID CallUpElectron(HWND hWnd)
{
    // 子进程执行临时文件
    SHELLEXECUTEINFO sei;
    sei.cbSize = sizeof(SHELLEXECUTEINFO); // 结构的大小
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;   // 允许 sei.hProcess 接收新进程的句柄
    sei.hwnd = NULL;                       // 不为该过程中可能出现的对话框指定父窗口（没什么用）
    sei.lpVerb = NULL;                     // open

    // 拼接子进程路径名
    TCHAR szBuffer[240];
    GetCurrentDirectoryW(240, szBuffer);
    std::wstring strPathName = szBuffer;
    for (int i = 0; i < 2; i++) // 两次上级目录
    {
        const size_t last_slash_idx = strPathName.rfind('\\');
        if (std::string::npos != last_slash_idx)
        {
			strPathName = strPathName.substr(0, last_slash_idx);
        }
    }
	std::wstring strFileName = strPathName + L"\\Electron\\dist\\win-unpacked\\timweb.exe";
    sei.lpFile = strFileName.c_str();
    sei.lpParameters = L"";
    sei.lpDirectory = NULL;       // 以当前路径为工作路径，之后可改为以 timweb.exe 所在路径为工作路径
    sei.nShow = SW_SHOWMAXIMIZED; // 启动时先不显示窗口

    // 执行子进程程序
    ShellExecuteEx(&sei);

    // 获取子进程 processId
    hProcessChild = sei.hProcess;
    DWORD dwProcessId = GetProcessId(hProcessChild);

    // 获取子进程 hWnd
    // TODO 超时的异常处理
    while (!hWndChild)
    {
        EnumWindows(
            [](HWND hWnd, LPARAM lParam) -> BOOL {
                DWORD lpdwProcessId;
                RECT rect;
                GetWindowThreadProcessId(hWnd, &lpdwProcessId);
                if (
                    GetRootProcess(lpdwProcessId) == lParam &&
                    !GetParent(hWnd) &&
                    GetWindowRect(hWnd, &rect) &&
                    rect.bottom - rect.top > 0 // 必要的判断，否则可能会返回一些不可见的窗口
                )
                {
                    hWndChild = hWnd;
                    return FALSE;
                }
                return TRUE;
            },
            dwProcessId);
    }

    // 将子进程窗口挂载到当前窗口内
    SetParent(hWndChild, hWnd);

    // 将子进程窗口设置为子窗口样式
    SetWindowLongA(hWndChild, GWL_STYLE, WS_CHILD);

    // 最大化显示子进程窗口
    ShowWindow(hWndChild, 3);

	sendMessageToElectron(hWndChild, hWnd);
}

void sendMessageToElectron(HWND recvHwnd,HWND thisHwnd)
{
	COPYDATASTRUCT copyData;
	char szSendBuf[100];
	time_t  timenow;
	time(&timenow);
	ctime_s(szSendBuf, 100, &timenow);
	copyData.dwData = 0;
	copyData.cbData = strlen(szSendBuf);
	szSendBuf[copyData.cbData - 1] = '\0';
	copyData.lpData = szSendBuf;
	SendMessage(recvHwnd, WM_COPYDATA, (WPARAM)thisHwnd, (LPARAM)&copyData);
}

std::wstring CharToWchar(const char* c, size_t m_encode = CP_ACP)
{
	std::wstring str;
	int len = MultiByteToWideChar(m_encode, 0, c, strlen(c), NULL, 0);
	wchar_t*	m_wchar = new wchar_t[len + 1];
	MultiByteToWideChar(m_encode, 0, c, strlen(c), m_wchar, len);
	m_wchar[len] = '\0';
	str = m_wchar;
	delete m_wchar;
	return str;
}

void handleCopyDataMessage(HWND hWnd,WPARAM wParam, LPARAM lParam)
{
	if (!lParam)
	{
		return;
	}
	COPYDATASTRUCT * pCopyData = (COPYDATASTRUCT *)lParam;
	std::string str = std::string((const char *)pCopyData->lpData, pCopyData->cbData);
	MessageBox(hWnd, CharToWchar(str.data()).data() , L"Title", 0);
}
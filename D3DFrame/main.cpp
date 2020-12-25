#include <Windows.h>
#include "d3dApp.h"
#include <sstream>

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	auto prevHWorkerW = FindWindowEx(hwnd, NULL, L"SHELLDLL_DefView", L"");
	if (prevHWorkerW != nullptr)
	{
		*(reinterpret_cast<HWND*>(lParam)) = FindWindowEx(NULL, hwnd, L"WorkerW", L"");
	}
	return TRUE;
}

BOOL CALLBACK Monitorenumproc(HMONITOR hMonitor, HDC hDC, LPRECT lpRect, LPARAM lpAppData)
{
	std::vector<RECT>* pMonitorRect = reinterpret_cast<std::vector<RECT>*>(lpAppData);
	pMonitorRect->push_back(*lpRect);
	return TRUE;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{

	std::wstring pcmdLine = pCmdLine;
	std::wstringstream cmd(pcmdLine);

	std::wstring opt;
	int i = 0;
	cmd >> opt;
	if (opt == L"-m")
	{
		cmd >> i;
	}

	HWND hProgram = FindWindow(L"Progman", L"Program Manager");

	DWORD_PTR sth{};
	auto result = SendMessageTimeout(hProgram, 0x052C, 0, 0, SMTO_NORMAL, 1000, &sth);
	HWND hWorkerW{};
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&hWorkerW));

	std::vector<RECT> pMonitorRects;
	EnumDisplayMonitors(NULL, NULL, Monitorenumproc, reinterpret_cast<LPARAM>(&pMonitorRects));

	RECT rc = pMonitorRects[i];
	POINT origin = { rc.left,rc.top };	// 坐标必须转换，因为要挂在 WorkerW 下，坐标并非按照屏幕坐标来算
	ScreenToClient(hWorkerW, &origin);	// 解决显示在错误显示器的行为
	try
	{
		MainWindow se;
		if (!se.Create(L"GAL", WS_POPUP,0,origin.x,origin.y,rc.right-rc.left,rc.bottom-rc.top))
			return 0;
		// 热键 alt+B 关闭程序
		if (!RegisterHotKey(se.Window(), GlobalAddAtom(L"unique"), MOD_ALT | MOD_NOREPEAT, 0x42))
			return 0;
		if (!SetParent(se.Window(), hWorkerW))
			return 0;
		se.Initialize();
		/*BLENDFUNCTION bf{};
		bf.BlendOp = AC_SRC_OVER; 
		bf.BlendFlags = 0;
		bf.SourceConstantAlpha = 255;
		bf.AlphaFormat = AC_SRC_ALPHA;
		UpdateLayeredWindow(se.Window(), NULL, NULL, NULL, NULL, NULL, RGB(0, 1, 2), &bf, ULW_ALPHA);*/
		ShowWindow(se.Window(), nCmdShow);

		se.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
	return 0;
}
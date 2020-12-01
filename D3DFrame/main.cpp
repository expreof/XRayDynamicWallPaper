#include <Windows.h>
#include "d3dApp.h"

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	auto prevHWorkerW = FindWindowEx(hwnd, NULL, L"SHELLDLL_DefView", L"");
	if (prevHWorkerW != nullptr)
	{
		*(reinterpret_cast<HWND*>(lParam)) = FindWindowEx(NULL, hwnd, L"WorkerW", L"");
	}
	return TRUE;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	HWND hProgram = FindWindow(L"Progman", L"Program Manager");

	DWORD_PTR sth{};
	auto result = SendMessageTimeout(hProgram, 0x052C, 0, 0, SMTO_NORMAL, 1000, &sth);
	HWND hWorkerW{};
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&hWorkerW));

	try
	{
		MainWindow se;
		if (!se.Create(L"GAL", WS_POPUP,0,1920,0,1920,1080))
			return 0;
		// ÈÈ¼ü alt+B ¹Ø±Õ³ÌÐò
		// (alt+Z ×¢²áÊ§°ÜÁË
		if (!RegisterHotKey(se.Window(), 2323, MOD_ALT | MOD_NOREPEAT, 0x42))
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
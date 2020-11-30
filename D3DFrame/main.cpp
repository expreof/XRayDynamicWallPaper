#include <Windows.h>
#include "d3dApp.h"

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	try
	{
		MainWindow se;
		if (!se.Create(L"GAL", WS_OVERLAPPEDWINDOW))
			return 0;
		se.Initialize();
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
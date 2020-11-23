#include <Windows.h>
#include "d3dApp.h"

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	MainWindow se;
	if (!se.Create(L"GAL", WS_OVERLAPPEDWINDOW))
		return 0;
	se.Initialize();
	ShowWindow(se.Window(), nCmdShow);

	se.Run();

	return 0;
}
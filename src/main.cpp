#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <random>
#include <chrono>
#include <thread>

#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>
// #include "trivial_signaling_client.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Ws2_32.lib")

#ifdef _MSC_VER
#pragma warning(disable : 4702) /* unreachable code */
#endif

#include "App.h"

int main(int argc, const char **argv)
{
	App app(argc, argv);
	// app.init();
	app.run();
	app.shutdown();

	return 0;
}

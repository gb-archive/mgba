#include "main.h"

#ifdef USE_CLI_DEBUGGER
#include "debugger/cli-debugger.h"
#endif

#ifdef USE_GDB_STUB
#include "debugger/gdb-stub.h"
#endif

#include "gba-thread.h"
#include "gba.h"
#include "gba-config.h"
#include "platform/commandline.h"
#include "util/configuration.h"

#include <SDL.h>

#include <errno.h>
#include <signal.h>
#include <sys/time.h>

#define PORT "sdl"

static bool _GBASDLInit(struct SDLSoftwareRenderer* renderer);
static void _GBASDLDeinit(struct SDLSoftwareRenderer* renderer);
static void _GBASDLStart(struct GBAThread* context);
static void _GBASDLClean(struct GBAThread* context);

int main(int argc, char** argv) {
	struct SDLSoftwareRenderer renderer;
	GBAVideoSoftwareRendererCreate(&renderer.d);

	struct GBAInputMap inputMap;
	GBAInputMapInit(&inputMap);

	struct GBAConfig config;
	GBAConfigInit(&config, PORT);
	GBAConfigLoad(&config);

	struct GBAOptions opts = {
		.audioBuffers = 512,
		.videoSync = false,
		.audioSync = true,
	};
	GBAConfigLoadDefaults(&config, &opts);

	struct GBAArguments args = {};
	struct GraphicsOpts graphicsOpts = {};

	struct SubParser subparser;

	GBAConfigMap(&config, &opts);

	initParserForGraphics(&subparser, &graphicsOpts);
	if (!parseArguments(&args, &config, argc, argv, &subparser)) {
		usage(argv[0], subparser.usage);
		freeArguments(&args);
		GBAConfigFreeOpts(&opts);
		GBAConfigDeinit(&config);
		return 1;
	}

	renderer.viewportWidth = opts.width;
	renderer.viewportHeight = opts.height;
#if SDL_VERSION_ATLEAST(2, 0, 0)
	renderer.events.fullscreen = opts.fullscreen;
	renderer.events.windowUpdated = 0;
#endif
	renderer.ratio = graphicsOpts.multiplier;

	if (!_GBASDLInit(&renderer)) {
		freeArguments(&args);
		GBAConfigFreeOpts(&opts);
		GBAConfigDeinit(&config);
		return 1;
	}

	struct GBAThread context = {
		.renderer = &renderer.d.d,
		.startCallback = _GBASDLStart,
		.cleanCallback = _GBASDLClean,
		.userData = &renderer
	};

	context.debugger = createDebugger(&args);

	GBAMapOptionsToContext(&opts, &context);
	GBAMapArgumentsToContext(&args, &context);

	renderer.audio.samples = context.audioBuffers;
	GBASDLInitAudio(&renderer.audio);

	renderer.events.bindings = &inputMap;
	GBASDLInitindings(&inputMap);
	GBASDLInitEvents(&renderer.events);
	GBASDLEventsLoadConfig(&renderer.events, &config.configTable); // TODO: Don't use this directly

	GBAThreadStart(&context);

	GBASDLRunloop(&context, &renderer);

	GBAThreadJoin(&context);
	freeArguments(&args);
	GBAConfigFreeOpts(&opts);
	GBAConfigDeinit(&config);
	free(context.debugger);
	GBAInputMapDeinit(&inputMap);

	_GBASDLDeinit(&renderer);

	return 0;
}

static bool _GBASDLInit(struct SDLSoftwareRenderer* renderer) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		return false;
	}

	return GBASDLInit(renderer);
}

static void _GBASDLDeinit(struct SDLSoftwareRenderer* renderer) {
	free(renderer->d.outputBuffer);

	GBASDLDeinitEvents(&renderer->events);
	GBASDLDeinitAudio(&renderer->audio);
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_DestroyWindow(renderer->window);
#endif

	GBASDLDeinit(renderer);

	SDL_Quit();

}

static void _GBASDLStart(struct GBAThread* threadContext) {
	struct SDLSoftwareRenderer* renderer = threadContext->userData;
	renderer->audio.audio = &threadContext->gba->audio;
	renderer->audio.thread = threadContext;
}

static void _GBASDLClean(struct GBAThread* threadContext) {
	struct SDLSoftwareRenderer* renderer = threadContext->userData;
	renderer->audio.audio = 0;
}

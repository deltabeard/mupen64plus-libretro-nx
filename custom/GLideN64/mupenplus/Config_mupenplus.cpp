#include "GLideN64_mupenplus.h"
#include "GLideN64_libretro.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "../Config.h"
#include "../GLideN64.h"
#include "../GBI.h"
#include "../RSP.h"
#include "../Log.h"
extern "C" {
#include "main/util.h"
}

Config config;

std::string replaceChars(std::string myString)
{
	for (size_t pos = myString.find(' '); pos != std::string::npos; pos = myString.find(' ', pos))
	{
		myString.replace(pos, 1, "%20");
	}
	for (size_t pos = myString.find('\''); pos != std::string::npos; pos = myString.find('\'', pos))
	{
		myString.replace(pos, 1, "%27");
	}
	return myString;
}

/* FIXME: Remove ini. */
void LoadCustomSettings(void)
{
#include "GLideN64.custom.ini.h"
	std::string myString = replaceChars(RSP.romname);
	bool found = false;
	char buffer[256];
	char* line;
	std::transform(myString.begin(), myString.end(), myString.begin(), ::toupper);
	line = strtok(customini, "\n");

	while (line != NULL)
	{
		ini_line l = ini_parse_line(&line);
		switch (l.type)
		{
			case INI_SECTION:
			{
				if (myString == replaceChars(l.name))
					found = true;
				else
					found = false;
			}
			case INI_PROPERTY:
			{
				if (found) {
					if (!strcmp(l.name, "video\\multisampling"))
						config.video.multisampling = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\aspect"))
						config.frameBufferEmulation.aspect = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\nativeResFactor"))
						config.frameBufferEmulation.nativeResFactor = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyToRDRAM"))
						config.frameBufferEmulation.copyToRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyFromRDRAM"))
						config.frameBufferEmulation.copyFromRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyDepthToRDRAM"))
						config.frameBufferEmulation.copyDepthToRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyAuxToRDRAM"))
						config.frameBufferEmulation.copyAuxToRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\N64DepthCompare"))
						config.frameBufferEmulation.N64DepthCompare = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\bufferSwapMode"))
						config.frameBufferEmulation.bufferSwapMode = atoi(l.value);
					else if (!strcmp(l.name, "texture\\bilinearMode"))
						config.texture.bilinearMode = atoi(l.value);
					else if (!strcmp(l.name, "texture\\maxAnisotropy"))
						config.texture.maxAnisotropy = atoi(l.value);
					else if (!strcmp(l.name, "generalEmulation\\enableNativeResTexrects"))
						config.graphics2D.enableNativeResTexrects = atoi(l.value);
					else if (!strcmp(l.name, "generalEmulation\\correctTexrectCoords"))
						config.graphics2D.correctTexrectCoords = atoi(l.value);
					else if (!strcmp(l.name, "generalEmulation\\enableLegacyBlending"))
						config.generalEmulation.enableLegacyBlending = atoi(l.value);
					else if (!strcmp(l.name, "generalEmulation\\enableFragmentDepthWrite"))
						config.generalEmulation.enableFragmentDepthWrite = atoi(l.value);
				}
			}
		}
		line = strtok(NULL, "\n");
	}
}

extern "C" void Config_LoadConfig()
{
	u32 hacks = config.generalEmulation.hacks;

	config.resetToDefaults();
	config.frameBufferEmulation.aspect = AspectRatio;
	config.frameBufferEmulation.enable = EnableFBEmulation;
	config.frameBufferEmulation.N64DepthCompare = EnableN64DepthCompare;

	config.texture.bilinearMode = bilinearMode;
	config.generalEmulation.enableHWLighting = EnableHWLighting;
	config.generalEmulation.enableLegacyBlending = enableLegacyBlending;
	config.generalEmulation.enableNoise = EnableNoiseEmulation;
	config.generalEmulation.enableLOD = EnableLODEmulation;

	config.frameBufferEmulation.copyDepthToRDRAM = EnableCopyDepthToRDRAM;
#if defined(GLES2) && !defined(ANDROID)
	config.frameBufferEmulation.copyToRDRAM = Config::ctDisable;
#else
	config.frameBufferEmulation.copyToRDRAM = EnableCopyColorToRDRAM;
#endif
#ifdef HAVE_OPENGLES
	config.frameBufferEmulation.bufferSwapMode = Config::bsOnColorImageChange;
#endif
#ifdef HAVE_OPENGLES2
	config.generalEmulation.enableFragmentDepthWrite = 0;
#else
	config.generalEmulation.enableFragmentDepthWrite = EnableFragmentDepthWrite;
#endif

#ifdef VC
	config.generalEmulation.enableShadersStorage = 0;
#else
	config.generalEmulation.enableShadersStorage = EnableShadersStorage;
#endif

	config.textureFilter.txFilterMode = txFilterMode;
	config.textureFilter.txEnhancementMode = txEnhancementMode;
	config.textureFilter.txFilterIgnoreBG = txFilterIgnoreBG;
	config.textureFilter.txHiresEnable = txHiresEnable;
	config.textureFilter.txHiresFullAlphaChannel = txHiresFullAlphaChannel;
	config.video.fxaa = EnableFXAA;
	config.video.multisampling = MultiSampling;

	config.graphics2D.correctTexrectCoords = CorrectTexrectCoords;
	config.graphics2D.enableNativeResTexrects = enableNativeResTexrects;
	config.graphics2D.bgMode = BackgroundMode;

	config.frameBufferEmulation.nativeResFactor = EnableNativeResFactor;

	config.generalEmulation.hacks = hacks;
	LoadCustomSettings();
}

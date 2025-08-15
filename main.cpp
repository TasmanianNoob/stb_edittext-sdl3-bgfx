// main.cpp
// A graphical C++/BGFX implementation demonstrating stb_textedit.h
//
// Dependencies: bgfx, bx, bimg, SDL3, SDL3_ttf
//
// To compile, you must have the bgfx library and its dependencies compiled and
// linked. The include paths for bgfx, bx, bimg, SDL3, and SDL3_ttf must be
// configured for your build system. This is a non-trivial setup.
//
// Example Linux g++ command (paths are illustrative):
// g++ main.cpp -o stb_editor_bgfx \
//   -I/path/to/bgfx/include -I/path/to/bx/include -I/path/to/bimg/include \
//   -L/path/to/bgfx/build/linux-gmake/bin \
//   `sdl3-config --cflags --libs` -lSDL3_ttf \
//   -lbgfx-shared-libDebug -l-bimg-shared-libDebug -l-bx-shared-libDebug \
//   -ldl -lpthread

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <chrono>
#include <memory> // For std::unique_ptr

// SDL for windowing and input
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// BGFX for rendering
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

// --- Graphics Context and STB Callbacks ---
struct AppContext {
	TTF_TextEngine* textEngine = nullptr;
	TTF_Font* font = nullptr;
};
static AppContext g_AppContext;

// STB Text Edit Library Configuration
// -----------------------------------
// Define all the #defines needed for stb_textedit

#define STB_TEXTEDIT_CHARTYPE           char
#define STB_TEXTEDIT_STRING             text_control

#include "stb_textedit.h"

struct text_control
{
	char *string;
	int stringlen;
	int capacity;
	STB_TexteditState state;
};

void getTextSize(std::string_view text, int* w, int* h);
int delete_chars(text_control *str, int pos, int num);
int insert_chars(text_control *str, int pos, char *newtext, int num);
float get_width_func(text_control* str, int n, int i);
void layout_func(StbTexteditRow *row, text_control *str, int start_i);

#define STB_TEXTEDIT_STRINGLEN(tc)      ((tc)->stringlen)
#define STB_TEXTEDIT_LAYOUTROW          layout_func
#define STB_TEXTEDIT_GETWIDTH           get_width_func
#define STB_TEXTEDIT_KEYTOTEXT(key)     (((key) & 0xff000000) ? 0 : (key))
#define STB_TEXTEDIT_GETCHAR(tc,i)      ((tc)->string[i])
#define STB_TEXTEDIT_NEWLINE            '\n'
#define STB_TEXTEDIT_IS_SPACE(ch)       isspace(ch)
#define STB_TEXTEDIT_DELETECHARS        delete_chars
#define STB_TEXTEDIT_INSERTCHARS        insert_chars

// Define key constants. These are arbitrary but must be consistent.
// We use high bits to distinguish from character inputs.
#define KEYDOWN_BIT                     0x80000000
#define STB_TEXTEDIT_K_SHIFT            0x40000000
#define STB_TEXTEDIT_K_CONTROL          0x20000000
#define STB_TEXTEDIT_K_LEFT             (KEYDOWN_BIT | 1)
#define STB_TEXTEDIT_K_RIGHT            (KEYDOWN_BIT | 2)
#define STB_TEXTEDIT_K_UP               (KEYDOWN_BIT | 3)
#define STB_TEXTEDIT_K_DOWN             (KEYDOWN_BIT | 4)
#define STB_TEXTEDIT_K_LINESTART        (KEYDOWN_BIT | 5) // Home
#define STB_TEXTEDIT_K_LINEEND          (KEYDOWN_BIT | 6) // End
#define STB_TEXTEDIT_K_DELETE           (KEYDOWN_BIT | 7)
#define STB_TEXTEDIT_K_BACKSPACE        (KEYDOWN_BIT | 8)
#define STB_TEXTEDIT_K_INSERT           (KEYDOWN_BIT | 9)
#define STB_TEXTEDIT_K_PGUP             (KEYDOWN_BIT | 10)
#define STB_TEXTEDIT_K_PGDOWN           (KEYDOWN_BIT | 11)
#define STB_TEXTEDIT_K_TEXTSTART        (STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_TEXTEND          (STB_TEXTEDIT_K_LINEEND   | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_UNDO             (KEYDOWN_BIT | STB_TEXTEDIT_K_CONTROL | 'z')
#define STB_TEXTEDIT_K_REDO             (KEYDOWN_BIT | STB_TEXTEDIT_K_CONTROL | 'y')
#define STB_TEXTEDIT_K_WORDLEFT         (STB_TEXTEDIT_K_LEFT  | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_WORDRIGHT        (STB_TEXTEDIT_K_RIGHT | STB_TEXTEDIT_K_CONTROL)

#define STB_TEXTEDIT_IMPLEMENTATION
#include "stb_textedit.h"


int delete_chars(text_control *str, int pos, int num) {
	memmove(&str->string[pos], &str->string[pos+num], str->stringlen - (pos+num));
	str->stringlen -= num;
	str->string[str->stringlen] = '\0'; // re-null-terminate
	return 1;
}

int insert_chars(text_control *str, int pos, char *newtext, int num) {
	if (str->stringlen + num > str->capacity) {
		str->capacity = str->capacity * 2;
		if (str->capacity < str->stringlen + num) {
			str->capacity = str->stringlen + num;
		}
		str->string = (char*)realloc(str->string, str->capacity);
		if (!str->string) return 0; // realloc failed
	}
	memmove(&str->string[pos+num], &str->string[pos], str->stringlen - pos);
	memcpy(&str->string[pos], newtext, num);
	str->stringlen += num;
	str->string[str->stringlen] = '\0'; // re-null-terminate
	return 1;
}

float get_width_func(text_control* str, int n, int i) {
	if (!g_AppContext.textEngine) return 0;
	char single_char_str[2] = { str->string[i], 0 };
	int width = 0;
	getTextSize(std::string_view(single_char_str, 1), &width, nullptr);
	return (float)width;
}

bool loadFont(const std::string& path, int size) {
	g_AppContext.font = TTF_OpenFont(path.c_str(), size);
	if (!g_AppContext.font) {
		//std::cerr << "Failed to load font: " << path << " | TTF_Error: " << TTF_GetError() << std::endl;
		return false;
	}
	return true;
}

int getFontHeight() {
	return TTF_GetFontHeight(g_AppContext.font);
}

void getTextSize(std::string_view text, int* w, int* h) {
	if (text.empty()) {
		if (w) *w = 0;
		if (h) *h = getFontHeight();
		return;
	}

	TTF_GetStringSize(g_AppContext.font, text.data(), text.length(), w, h);
}

bgfx::TextureHandle createTextTexture(std::string_view strView) {
	if (strView.empty()) {
		return BGFX_INVALID_HANDLE;
	}

	int height, width;
	getTextSize(strView, &width, &height);

	TTF_Text *text = TTF_CreateText(g_AppContext.textEngine, g_AppContext.font, strView.data(), strView.length());
	TTF_SetTextColor(text, 0, 0, 0, 255);
	SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
	TTF_DrawSurfaceText(text, 0, 0, surface);

	if (!surface) return BGFX_INVALID_HANDLE;

	// BGFX requires BGRA8 format on many platforms, SDL_Surface is often RGBA8.
	// For simplicity, we assume the formats are compatible or would be converted here.
	// A robust implementation would use bimg to convert pixel formats.
	const bgfx::Memory* mem = bgfx::copy(surface->pixels, surface->w * surface->h * 4);

	bgfx::TextureHandle handle = bgfx::createTexture2D(
		(uint16_t)surface->w, (uint16_t)surface->h, false, 1, bgfx::TextureFormat::RGBA8, 0, mem
	);

	SDL_DestroySurface(surface);
	return handle;
}


void layout_func(StbTexteditRow *row, text_control *str, int start_i) {
	if (!g_AppContext.textEngine) return;
	const int remaining_chars = str->stringlen - start_i;
	std::string_view text_view = std::string_view(str->string + start_i, remaining_chars);
	int width = 0;
	getTextSize(text_view, &width, nullptr);
	row->x0 = 0.0f;
	row->x1 = (float)width;
	row->baseline_y_delta = (float) getFontHeight();
	row->ymin = 0.0f;
	row->ymax = (float) getFontHeight();
	row->num_chars = remaining_chars;
}

// --- BGFX Rendering Details ---
// Simple vertex format for 2D rendering
struct PosColorVertex {
	float x, y;
	uint32_t abgr;
	static bgfx::VertexLayout s_decl;
	static void init() {
		s_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
	}
};
bgfx::VertexLayout PosColorVertex::s_decl;

struct PosTexCoordVertex {
	float x, y;
	float u, v;
	static bgfx::VertexLayout s_decl;
	static void init() {
		s_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	}
};
bgfx::VertexLayout PosTexCoordVertex::s_decl;

std::vector<char> ReadAssetFile(const std::string& fileName, const std::string& mode = "rb")
{
	SDL_IOStream* file = SDL_IOFromFile(fileName.c_str(), mode.c_str());
	std::vector<char> data;

	if (!file)
	{
		SDL_Log("File (%s) failed to load: %s", fileName.c_str(), SDL_GetError());
		SDL_CloseIO(file);
		return data;
	}

	const int64_t size = SDL_SeekIO(file, 0, SDL_IO_SEEK_END);
	data.resize(size);

	// go back to the start of the file. TODO: Is this right?
	SDL_SeekIO(file, -size, SDL_IO_SEEK_END);
	if (SDL_ReadIO(file, data.data(), size) == 0 && SDL_GetIOStatus(file) != SDL_IO_STATUS_EOF)
	{
		SDL_Log("File (%s) failed to read: %s", fileName.c_str(), SDL_GetError());
		SDL_CloseIO(file);
		return data;
	}

	SDL_CloseIO(file);
	return data;
}

bgfx::ShaderHandle LoadShader(const std::string& shaderName)
{
	const std::vector<char> data = ReadAssetFile(shaderName);

	if (data.empty())
	{
		return BGFX_INVALID_HANDLE;
	}

	const bgfx::Memory* const mem = bgfx::copy(data.data(), static_cast<uint32_t>(data.size()));

	const bgfx::ShaderHandle handle = bgfx::createShader(mem);
	bgfx::setName(handle, shaderName.c_str());

	return handle;
}


// --- Main Application Class ---
class TextEditorApp {
private:
	SDL_Window* window = nullptr;
	//StbTextEditor text_editor_string;
	text_control text_edit_state;

	bgfx::ProgramHandle solid_program;
	bgfx::ProgramHandle textured_program;
	bgfx::UniformHandle tex_uniform;
	bgfx::TextureHandle text_texture = BGFX_INVALID_HANDLE;
	std::string last_rendered_text;

	std::chrono::time_point<std::chrono::high_resolution_clock> currentTime = std::chrono::high_resolution_clock::now();
	bool showingCursor = true;

	const int SCREEN_WIDTH = 800;
	const int SCREEN_HEIGHT = 600;
	const int TEXT_BOX_X = 50;
	const int TEXT_BOX_Y = 50;
public:
	TextEditorApp() {
		stb_textedit_initialize_state(&text_edit_state.state, 0);

		const char* initial_text = "";
		int initial_len = strlen(initial_text);

		// Initialize the text_control struct
		text_edit_state.stringlen = initial_len;
		text_edit_state.capacity = initial_len + 1; // +1 for the null terminator
		text_edit_state.string = (char*)malloc(text_edit_state.capacity);

		// Copy the initial text into the buffer.
		if (text_edit_state.string) {
			strcpy(text_edit_state.string, initial_text);
		}

		// Initialize the stb_textedit state.
		stb_textedit_initialize_state(&text_edit_state.state, 0); // 0 = single line

		// Optionally, move the cursor to the end of the initial text.
		text_edit_state.state.cursor = initial_len;
	}

	bool initialize() {
		if (!SDL_Init(SDL_INIT_VIDEO)) return false;
		if (!TTF_Init()) return false;

		window = SDL_CreateWindow("STB TextEdit BGFX Demo (SDL3)", SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE );
		if (!window) return false;

		// Initialize BGFX
		bgfx::PlatformData platformData;
#if defined(SDL_PLATFORM_ANDROID)
		platformData.ndt = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_SURFACE_POINTER, nullptr);
		platformData.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_WIN32)
		platformData.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
		platformData.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
		if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0)
		{
			platformData.ndt = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
			platformData.nwh = reinterpret_cast<void*>(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
		}
		else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
		{
			platformData.ndt = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
			platformData.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
		}
#elif defined(SDL_PLATFORM_IOS)
		platformData.nwh = CreateMetalLayer(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr));
#else
#error Unsupported platform
#endif

		bgfx::Init init;
		init.type = bgfx::RendererType::Count; // Autoselect renderer
		init.resolution.width = SCREEN_WIDTH;
		init.resolution.height = SCREEN_HEIGHT;
		init.resolution.reset = BGFX_RESET_VSYNC;
		init.platformData = platformData;
		if (!bgfx::init(init)) return false;

		bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xdcdcdcU, 1.0f, 0);
		bgfx::setViewRect(0, 0, 0, (uint16_t)SCREEN_WIDTH, (uint16_t)SCREEN_HEIGHT);

		// Initialize Text Engine
		g_AppContext.textEngine = TTF_CreateSurfaceTextEngine();
		if (!loadFont("C:/Windows/Fonts/Arial.ttf", 24)) {
			return false;
		}

		// Create BGFX resources
		tex_uniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

		bgfx::ShaderHandle solid_vertex = LoadShader("../shaders/vs_simple.bin");
		bgfx::ShaderHandle solid_fragment = LoadShader("../shaders/fs_simple.bin");
		bgfx::ShaderHandle textured_vertex = LoadShader("../shaders/vs_textured.bin");
		bgfx::ShaderHandle textured_fragment = LoadShader("../shaders/fs_textured.bin");
		solid_program = bgfx::createProgram(solid_vertex, solid_fragment, true);
		textured_program = bgfx::createProgram(textured_vertex, textured_fragment, true);

		SDL_StartTextInput(window);
		return true;
	}

	void shutdown() {
		SDL_StopTextInput(window);
		if(bgfx::isValid(text_texture)) bgfx::destroy(text_texture);
		bgfx::destroy(tex_uniform);
		bgfx::destroy(solid_program);
		bgfx::destroy(textured_program);
		bgfx::shutdown();
		TTF_DestroySurfaceTextEngine(g_AppContext.textEngine);
		if (window) SDL_DestroyWindow(window);
		TTF_Quit();
		SDL_Quit();
	}

	void renderFrame() {
		// Set up orthographic projection matrix
		float proj[16];
		bx::mtxOrtho(proj, 0.0f, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT, 0.0f, 0.0f, 100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
		bgfx::setViewTransform(0, NULL, proj);
		bgfx::touch(0);

		// Update text texture if text has changed
		if (last_rendered_text != text_edit_state.string) {
			if (bgfx::isValid(text_texture)) bgfx::destroy(text_texture);
			text_texture = createTextTexture(text_edit_state.string);
			last_rendered_text = text_edit_state.string;
		}

		// --- Draw Selection ---
		if (text_edit_state.state.select_start != text_edit_state.state.select_end) {
			int start_idx = std::min(text_edit_state.state.select_start, text_edit_state.state.select_end);
			int end_idx = std::max(text_edit_state.state.select_start, text_edit_state.state.select_end);
			int x_start=0, x_sel_w=0, h=0;
			getTextSize(std::string_view(text_edit_state.string, start_idx), &x_start, &h);
			getTextSize(std::string_view(text_edit_state.string + start_idx, end_idx - start_idx), &x_sel_w, nullptr);
			drawSolidQuad(TEXT_BOX_X + x_start, TEXT_BOX_Y, x_sel_w, h, 0xffFF9664); // Blue selection
		}

		// --- Draw Text ---
		if (bgfx::isValid(text_texture)) {
			int w=0, h=0;
			getTextSize(text_edit_state.string, &w, &h);
			drawTexturedQuad(TEXT_BOX_X, TEXT_BOX_Y, w, h, text_texture);
		}

		// --- Draw Cursor ---
		if (showingCursor) {
			int cursor_x = 0, cursor_h = 0;
			getTextSize(std::string_view(text_edit_state.string, text_edit_state.state.cursor), &cursor_x, &cursor_h);
			drawSolidQuad(TEXT_BOX_X + cursor_x, TEXT_BOX_Y, 2, cursor_h, 0xff000000); // Black cursor
		}


		bgfx::frame();
	}

	void drawSolidQuad(float x, float y, float w, float h, uint32_t color_abgr) {
		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		if (bgfx::allocTransientBuffers(&tvb, PosColorVertex::s_decl, 4, &tib, 6)) {
			PosColorVertex* vertex = (PosColorVertex*)tvb.data;
			vertex[0] = {x,     y,     color_abgr};
			vertex[1] = {x + w, y,     color_abgr};
			vertex[2] = {x + w, y + h, color_abgr};
			vertex[3] = {x,     y + h, color_abgr};
			uint16_t* indices = (uint16_t*)tib.data;
			indices[0] = 0; indices[1] = 1; indices[2] = 2;
			indices[3] = 0; indices[4] = 2; indices[5] = 3;
			bgfx::setVertexBuffer(0, &tvb);
			bgfx::setIndexBuffer(&tib);
			bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
			bgfx::submit(0, solid_program);
		}
	}

	void drawTexturedQuad(float x, float y, float w, float h, bgfx::TextureHandle texture) {
		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		if (bgfx::allocTransientBuffers(&tvb, PosTexCoordVertex::s_decl, 4, &tib, 6)) {
			PosTexCoordVertex* vertex = (PosTexCoordVertex*)tvb.data;
			vertex[0] = {x,     y,     0.0f, 0.0f};
			vertex[1] = {x + w, y,     1.0f, 0.0f};
			vertex[2] = {x + w, y + h, 1.0f, 1.0f};
			vertex[3] = {x,     y + h, 0.0f, 1.0f};
			uint16_t* indices = (uint16_t*)tib.data;
			indices[0] = 0; indices[1] = 1; indices[2] = 2;
			indices[3] = 0; indices[4] = 2; indices[5] = 3;
			bgfx::setVertexBuffer(0, &tvb);
			bgfx::setIndexBuffer(&tib);
			bgfx::setTexture(0, tex_uniform, texture);
			bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_NORMAL);
			bgfx::submit(0, textured_program);
		}
	}


	void run() {
		// Initialize vertex declarations once
		PosColorVertex::init();
		PosTexCoordVertex::init();

		bool quit = false;
		SDL_Event e;
		while (!quit) {
			if (std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - currentTime).count() >= 0.53) {
				currentTime = std::chrono::high_resolution_clock::now();
				showingCursor = !showingCursor;
			}

			while (SDL_PollEvent(&e) != 0) {
				if (e.type == SDL_EVENT_QUIT) {
					quit = true;
				} else if (e.type == SDL_EVENT_KEY_DOWN) {
					int key = 0;
					switch (e.key.key) {
						case SDLK_LEFT:      key = STB_TEXTEDIT_K_LEFT;     break;
						case SDLK_RIGHT:     key = STB_TEXTEDIT_K_RIGHT;    break;
						case SDLK_UP:        key = STB_TEXTEDIT_K_UP;       break;
						case SDLK_DOWN:      key = STB_TEXTEDIT_K_DOWN;     break;
						case SDLK_HOME:      key = STB_TEXTEDIT_K_LINESTART;break;
						case SDLK_END:       key = STB_TEXTEDIT_K_LINEEND;  break;
						case SDLK_BACKSPACE: key = STB_TEXTEDIT_K_BACKSPACE;break;
						case SDLK_DELETE:    key = STB_TEXTEDIT_K_DELETE;   break;
						case SDLK_INSERT:    key = STB_TEXTEDIT_K_INSERT;   break;
						case SDLK_PAGEUP:    key = STB_TEXTEDIT_K_LINESTART;break;
						case SDLK_PAGEDOWN:  key = STB_TEXTEDIT_K_LINEEND;  break;
					}

					if (e.key.key == SDLK_C && SDL_GetModState() & SDL_KMOD_CTRL && text_edit_state.state.select_start - text_edit_state.state.select_end != 0) {
						int min = std::min(text_edit_state.state.select_start, text_edit_state.state.select_end);
						int max = std::max(text_edit_state.state.select_start, text_edit_state.state.select_end);
						std::string copy(text_edit_state.string + min, max - min);

						SDL_SetClipboardText(copy.c_str());

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (e.key.key == SDLK_X && SDL_GetModState() & SDL_KMOD_CTRL && text_edit_state.state.select_start - text_edit_state.state.select_end != 0) {
						int min = std::min(text_edit_state.state.select_start, text_edit_state.state.select_end);
						int max = std::max(text_edit_state.state.select_start, text_edit_state.state.select_end);
						std::string cut(text_edit_state.string + min, max - min);

						SDL_SetClipboardText(cut.c_str());
						stb_textedit_cut(&text_edit_state, &text_edit_state.state);

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (e.key.key == SDLK_V && SDL_GetModState() & SDL_KMOD_CTRL) {
						SDL_Event event;

						event.type = SDL_EVENT_TEXT_INPUT;
						event.text.text = SDL_GetClipboardText();

						SDL_PushEvent(&event);

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (e.key.key == SDLK_A && SDL_GetModState() & SDL_KMOD_CTRL) {
						text_edit_state.state.select_start = 0;
						text_edit_state.state.select_end = text_edit_state.stringlen;

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (key) {
						if (SDL_GetModState() & SDL_KMOD_SHIFT) key |= STB_TEXTEDIT_K_SHIFT;
						if (SDL_GetModState() & SDL_KMOD_CTRL)  key |= STB_TEXTEDIT_K_CONTROL;
						stb_textedit_key(&text_edit_state, &text_edit_state.state, key);

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

				} else if (e.type == SDL_EVENT_TEXT_INPUT) {
					int length = std::strlen(e.text.text);

					if (length > 1) {
						stb_textedit_paste(&text_edit_state, &text_edit_state.state, e.text.text, length);
						SDL_free((void*) e.text.text);
					}
					else {
						stb_textedit_key(&text_edit_state, &text_edit_state.state, e.text.text[0]);
					}

					showingCursor = true;
					currentTime = std::chrono::high_resolution_clock::now();
				}
			}
			renderFrame();
		}
	}
};


int main(int argc, char* args[]) {
	TextEditorApp app;
	if (app.initialize()) {
		app.run();
	}
	app.shutdown();
	return 0;
}
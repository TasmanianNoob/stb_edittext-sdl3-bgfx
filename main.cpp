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
#include <array>
#include <string>
#include <string_view>
#include <algorithm>
#include <chrono>
#include <memory> // For std::unique_ptr

// SDL for windowing and input
#include <SDL3/SDL.h>
#include <msdf-atlas-gen/msdf-atlas-gen.h>

// BGFX for rendering
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;

using AtlasGeneratorType = msdf_atlas::ImmediateAtlasGenerator<
	float, // pixel type of buffer for individual glyphs depends on generator function
	3, // number of atlas color channels
	msdf_atlas::msdfGenerator, // function to generate bitmaps for individual glyphs
	msdf_atlas::BitmapAtlasStorage<msdf_atlas::byte, 3> // class that stores the atlas bitmap
	// For example, a custom atlas storage class that stores it in VRAM can be used.
>;

// --- Graphics Context and STB Callbacks ---
struct AppContext {
	msdfgen::FreetypeHandle *ft = nullptr;

	bgfx::TextureHandle fontAtlas = BGFX_INVALID_HANDLE;
	std::vector<msdf_atlas::GlyphGeometry> glyphs;
	std::unique_ptr<msdf_atlas::FontGeometry> fontGeometry = nullptr;
	msdf_atlas::TightAtlasPacker packer;
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
	std::string string;
	STB_TexteditState state;
};

void getTextSize(text_control *str, std::string_view text, int* w, int* h);
int delete_chars(text_control *str, int pos, int num);
int insert_chars(text_control *str, int pos, char *newtext, int num);
float get_width_func(text_control* str, int n, int i);
void layout_func(StbTexteditRow *row, text_control *str, int start_i);

#define STB_TEXTEDIT_STRINGLEN(tc)      ((tc)->string.length())
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
	str->string.erase(pos, num);
	return 1;
}

int insert_chars(text_control *str, int pos, char *newtext, int num) {
	str->string.insert(pos, newtext, num);
	return 1;
}

float get_width_func(text_control* str, int n, int i) {
	if (!g_AppContext.ft) return 0;
	char single_char_str[2] = { str->string[i], 0 };
	int width = 0;
	getTextSize(str, std::string_view(single_char_str, 1), &width, nullptr);
	return (float)width;
}

bool loadFont(const std::string& path) {
	if (msdfgen::FontHandle* font = msdfgen::loadFont(g_AppContext.ft, path.c_str())) {
		// FontGeometry is a helper class that loads a set of glyphs from a single font.
		// It can also be used to get additional font metrics, kerning information, etc.
		g_AppContext.fontGeometry = std::make_unique<msdf_atlas::FontGeometry>(&g_AppContext.glyphs);
		// Load a set of character glyphs:
		// The second argument can be ignored unless you mix different font sizes in one atlas.
		// In the last argument, you can specify a charset other than ASCII.
		// To load specific glyph indices, use loadGlyphs instead.
		g_AppContext.fontGeometry->loadCharset(font, 1.0, msdf_atlas::Charset::ASCII);
		// Apply MSDF edge coloring. See edge-coloring.h for other coloring strategies.
		const double maxCornerAngle = 3.0;
		for (msdf_atlas::GlyphGeometry &glyph : g_AppContext.glyphs)
			glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
		// TightAtlasPacker class computes the layout of the atlas.
		// Set atlas parameters:
		// setDimensions or setDimensionsConstraint to find the best value

		g_AppContext.packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
		// setScale for a fixed size or setMinimumScale to use the largest that fits
		g_AppContext.packer.setMinimumScale(24.0);
		// setPixelRange or setUnitRange
		g_AppContext.packer.setPixelRange(2.0);
		g_AppContext.packer.setMiterLimit(1.0);
		// Compute atlas layout - pack glyphs
		g_AppContext.packer.pack(g_AppContext.glyphs.data(), g_AppContext.glyphs.size());
		// Get final atlas dimensions
		int width = 0, height = 0;
		g_AppContext.packer.getDimensions(width, height);
		// The ImmediateAtlasGenerator class facilitates the generation of the atlas bitmap.
		AtlasGeneratorType generator(width, height);
		// GeneratorAttributes can be modified to change the generator's default settings.
		msdf_atlas::GeneratorAttributes attributes;
		generator.setAttributes(attributes);
		generator.setThreadCount(4);
		// Generate atlas bitmap
		generator.generate(g_AppContext.glyphs.data(), g_AppContext.glyphs.size());
		// The atlas bitmap can now be retrieved via atlasStorage as a BitmapConstRef.
		// The glyphs array (or fontGeometry) contains positioning data for typesetting text.
		msdfgen::BitmapConstRef<msdfgen::byte, 3> ref = generator.atlasStorage();

		const bgfx::Memory* memory = bgfx::copy(ref.pixels, ref.width * ref.height * 3);
		g_AppContext.fontAtlas = bgfx::createTexture2D(static_cast<uint16_t>(ref.width), static_cast<uint16_t>(ref.height), false, 1, bgfx::TextureFormat::RGB8, 0, memory);

		msdfgen::destroyFont(font);
		return true;
	}
	return false;
}

int getFontHeight(text_control* str) {
	//str->shape.
	//auto rect = g_AppContext.glyphs[0].getBoxRect();
	//auto shape = g_AppContext.glyphs[0].getShape();
	//auto bounds = shape.getBounds();
	//double l, b, r, t;
	//g_AppContext.glyphs[0].getQuadPlaneBounds(l, b, r, t);
	//g_AppContext.glyphs[0].getQuadAtlasBounds(l, b, r, t);
	//auto spacing = g_AppContext.glyphs[0].getAdvance();
	// TODO: font knows of font size => return font height
	return 24; // none return 24, g_AppContext.packer.getScale()
	//return TTF_GetFontHeight(g_AppContext.font);
}

void getTextSize(text_control* str, std::string_view text, int* w, int* h) {
	if (text.empty()) {
		if (w) *w = 0;
		if (h) *h = getFontHeight(str);
		return;
	}

	const msdf_atlas::FontGeometry* fontGeometry = g_AppContext.fontGeometry.get();
	const msdfgen::FontMetrics& metrics = fontGeometry->getMetrics();

	// Scaling factor to convert font units to your desired pixel size (e.g., 24px)
	double fsScale = 24.0 / (metrics.ascenderY - metrics.descenderY);
	double lineHeight = fsScale * metrics.lineHeight;

	double maxWidth = 0.0;
	double currentLineWidth = 0.0;
	double totalHeight = getFontHeight(str);

	for (size_t i = 0; i < text.length(); ++i) {
		char character = text[i];

		if (character == '\n') {
			maxWidth = std::max(maxWidth, currentLineWidth);
			currentLineWidth = 0;
			totalHeight += lineHeight;
			continue;
		}
		else if (character == '\r') {
			continue;
		}

		double advance = 0.0;
		const msdf_atlas::GlyphGeometry* glyph = fontGeometry->getGlyph(character);
		if (!glyph) {
			glyph = fontGeometry->getGlyph('?');
		}
		if (!glyph) {
			continue;
		}

		advance = glyph->getAdvance();

		if (i < text.length() - 1) {
			char nextCharacter = text[i + 1];
			fontGeometry->getAdvance(advance, character, nextCharacter);
		}

		currentLineWidth += fsScale * advance;
	}

	maxWidth = std::max(maxWidth, currentLineWidth);

	if (w) *w = static_cast<int>(ceil(maxWidth));
	if (h) *h = static_cast<int>(ceil(totalHeight));
}

struct TextLayoutFormat
{
	std::array<float, 2> pos;
	std::array<float, 2> texCoords;
	std::array<float, 2> screenPxRange;
	std::array<float, 4> colour;
};

void createTextTexture(float offsetX, float offsetY, text_control* str, std::string_view strView, bgfx::VertexLayout layout, bgfx::ProgramHandle program, bgfx::UniformHandle tex_uniform) {
	const msdf_atlas::FontGeometry* fontGeometry = g_AppContext.fontGeometry.get();
	const msdfgen::FontMetrics& metrics = fontGeometry->getMetrics();
	const msdf_atlas::TightAtlasPacker& atlasPacker = g_AppContext.packer;

	double x = offsetX;
	double fsScale = 24.0 / (metrics.ascenderY - metrics.descenderY);
	double y = offsetY;

	const float spaceGlyphAdvance = fontGeometry->getGlyph(' ')->getAdvance();

	int maxIndices = 6;
	int maxVertices = 4;
	bgfx::TransientIndexBuffer indexBuffer;
	bgfx::TransientVertexBuffer vertexBuffer;
	bgfx::allocTransientIndexBuffer(&indexBuffer, maxIndices * strView.length());
	bgfx::allocTransientVertexBuffer(&vertexBuffer, maxVertices * strView.length(), layout);

	uint16_t* indexData = (uint16_t*) indexBuffer.data;

	auto* data = (TextLayoutFormat*) vertexBuffer.data;
	int numVerts = 0;
	int numIndices = 0;

	for (size_t i = 0; i < strView.size(); i++)
	{
		char character = strView[i];
		if (character == '\r')
			continue;

		if (character == '\n')
		{
			x = 0;
			y -= fsScale * metrics.lineHeight;
			continue;
		}

		if (character == ' ')
		{
			float advance = spaceGlyphAdvance;
			if (i < strView.size() - 1)
			{
				char nextCharacter = strView[i + 1];
				double dAdvance;
				fontGeometry->getAdvance(dAdvance, character, nextCharacter);
				advance = (float)dAdvance;
			}

			x += fsScale * advance;
			continue;
		}

		if (character == '\t')
		{
			// NOTE(Yan): is this right?
			x += 4.0f * (fsScale * spaceGlyphAdvance);
			continue;
		}

		auto glyph = fontGeometry->getGlyph(character);
		if (!glyph)
			glyph = fontGeometry->getGlyph('?');
		if (!glyph)
			continue;

		double al, ab, ar, at;
		glyph->getQuadAtlasBounds(al, ab, ar, at);

		double pl, pb, pr, pt;
		glyph->getQuadPlaneBounds(pl, pb, pr, pt);

		pl *= fsScale, pb *= fsScale, pr *= fsScale, pt *= fsScale;
		pl += x, pb = y - pb, pr += x, pt = y - pt;
		pb += 24, pt += 24;

		int width;
		int height;
		atlasPacker.getDimensions(width, height);
		float texelWidth = 1.0f / width;
		float texelHeight = 1.0f / height;
		al *= texelWidth, ab *= texelHeight, ar *= texelWidth, at *= texelHeight;

		// render here
		int baseVert = numVerts;
		// 0
		data[numVerts].pos[0] = pl;
		data[numVerts].pos[1] = pb;
		data[numVerts].texCoords[0] = al;
		data[numVerts].texCoords[1] = ab;
		data[numVerts].screenPxRange[0] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].screenPxRange[1] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].colour[0] = 0.0;
		data[numVerts].colour[1] = 0.0;
		data[numVerts].colour[2] = 0.0;
		data[numVerts].colour[3] = 1.0;
		numVerts++;

		// 1
		data[numVerts].pos[0] = pr;
		data[numVerts].pos[1] = pb;
		data[numVerts].texCoords[0] = ar;
		data[numVerts].texCoords[1] = ab;
		data[numVerts].screenPxRange[0] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].screenPxRange[1] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].colour[0] = 0.0;
		data[numVerts].colour[1] = 0.0;
		data[numVerts].colour[2] = 0.0;
		data[numVerts].colour[3] = 1.0;
		numVerts++;

		// 2
		data[numVerts].pos[0] = pr;
		data[numVerts].pos[1] = pt;
		data[numVerts].texCoords[0] = ar;
		data[numVerts].texCoords[1] = at;
		data[numVerts].screenPxRange[0] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].screenPxRange[1] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].colour[0] = 0.0;
		data[numVerts].colour[1] = 0.0;
		data[numVerts].colour[2] = 0.0;
		data[numVerts].colour[3] = 1.0;
		numVerts++;

		// 4
		data[numVerts].pos[0] = pl;
		data[numVerts].pos[1] = pt;
		data[numVerts].texCoords[0] = al;
		data[numVerts].texCoords[1] = at;
		data[numVerts].screenPxRange[0] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].screenPxRange[1] = (pt - pb) * SCREEN_HEIGHT;
		data[numVerts].colour[0] = 0.0;
		data[numVerts].colour[1] = 0.0;
		data[numVerts].colour[2] = 0.0;
		data[numVerts].colour[3] = 1.0;
		numVerts++;

		indexData[numIndices++] = baseVert + 0;
		indexData[numIndices++] = baseVert + 1;
		indexData[numIndices++] = baseVert + 2;

		indexData[numIndices++] = baseVert + 2;
		indexData[numIndices++] = baseVert + 3;
		indexData[numIndices++] = baseVert + 0;

		if (i < strView.size() - 1)
		{
			double advance = glyph->getAdvance();
			char nextCharacter = strView[i + 1];
			fontGeometry->getAdvance(advance, character, nextCharacter);

			x += fsScale * advance;
		}
	}

	if (numVerts > 0)
	{
		bgfx::setIndexBuffer(&indexBuffer, 0, numIndices);
		bgfx::setVertexBuffer(0, &vertexBuffer, 0, numVerts);
		bgfx::setTexture(0, tex_uniform, g_AppContext.fontAtlas);
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | BGFX_STATE_BLEND_NORMAL);
		bgfx::submit(0, program);
	}
}


void layout_func(StbTexteditRow *row, text_control *str, int start_i) {
	if (!g_AppContext.ft) return;
	const int remaining_chars = str->string.length() - start_i;
	int width = 0;
	getTextSize(str, std::string_view(str->string.data() + start_i, remaining_chars), &width, nullptr);
	row->x0 = 0.0f;
	row->x1 = (float)width;
	row->baseline_y_delta = (float) getFontHeight(str);
	row->ymin = 0.0f;
	row->ymax = (float) getFontHeight(str);
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
	bgfx::VertexLayout layout;
	std::string last_rendered_text;

	std::chrono::time_point<std::chrono::high_resolution_clock> currentTime = std::chrono::high_resolution_clock::now();
	bool showingCursor = true;

	const int TEXT_BOX_X = 50;
	const int TEXT_BOX_Y = 50;
public:
	TextEditorApp() {

		// Initialize the text_control struct
		text_edit_state.string = "";

		// Initialize the stb_textedit state.
		stb_textedit_initialize_state(&text_edit_state.state, 0); // 0 = single line

		// Optionally, move the cursor to the end of the initial text.
		text_edit_state.state.cursor = text_edit_state.string.length();
	}

	bool initialize() {
		if (!SDL_Init(SDL_INIT_VIDEO)) return false;

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
		g_AppContext.ft = msdfgen::initializeFreetype();
		if (!loadFont("C:/Windows/Fonts/Arial.ttf")) {
			return false;
		}

		// Create BGFX resources
		tex_uniform = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

		layout
			.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
			.end();

		bgfx::ShaderHandle solid_vertex = LoadShader("../shaders/vs_simple.bin");
		bgfx::ShaderHandle solid_fragment = LoadShader("../shaders/fs_simple.bin");
		bgfx::ShaderHandle textured_vertex = LoadShader("../shaders/vs_textured.bin");
		bgfx::ShaderHandle textured_fragment = LoadShader("../shaders/fs_msdf.bin");
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
		if (window) SDL_DestroyWindow(window);
		SDL_Quit();
	}

	void renderFrame() {
		// Set up orthographic projection matrix
		float proj[16];
		bx::mtxOrtho(proj, 0.0f, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT, 0.0f, 0.0f, 100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
		bgfx::setViewTransform(0, NULL, proj);
		bgfx::touch(0);

		// Update text texture if text has changed
		//if (last_rendered_text != text_edit_state.string) {
		//	if (bgfx::isValid(text_texture)) bgfx::destroy(text_texture);
		//	text_texture = createTextTexture(&text_edit_state, text_edit_state.string);
		//	last_rendered_text = text_edit_state.string;
		//}

		// --- Draw Selection ---
		if (text_edit_state.state.select_start != text_edit_state.state.select_end) {
			int start_idx = std::min(text_edit_state.state.select_start, text_edit_state.state.select_end);
			int end_idx = std::max(text_edit_state.state.select_start, text_edit_state.state.select_end);
			int offset=0, width=0, height=0;
			getTextSize(&text_edit_state, std::string_view(text_edit_state.string.data(), start_idx), &offset, &height);
			getTextSize(&text_edit_state, std::string_view(text_edit_state.string.data() + start_idx, end_idx - start_idx), &width, nullptr);
			drawSolidQuad(TEXT_BOX_X + offset, TEXT_BOX_Y, width, height, 0xffFF9664); // Blue selection
		}

		// --- Draw Text ---
		if (!text_edit_state.string.empty()) {
			createTextTexture(TEXT_BOX_X, TEXT_BOX_Y, &text_edit_state, text_edit_state.string, layout, textured_program, tex_uniform);
		}

		// --- Draw Cursor ---
		if (showingCursor) {
			int cursor_x = 0, cursor_h = 0;
			getTextSize(&text_edit_state, std::string_view(text_edit_state.string.data(), text_edit_state.state.cursor), &cursor_x, &cursor_h);
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
						std::string copy(text_edit_state.string.data() + min, max - min);

						SDL_SetClipboardText(copy.c_str());

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (e.key.key == SDLK_X && SDL_GetModState() & SDL_KMOD_CTRL && text_edit_state.state.select_start - text_edit_state.state.select_end != 0) {
						int min = std::min(text_edit_state.state.select_start, text_edit_state.state.select_end);
						int max = std::max(text_edit_state.state.select_start, text_edit_state.state.select_end);
						std::string cut(text_edit_state.string.data() + min, max - min);

						SDL_SetClipboardText(cut.c_str());
						stb_textedit_cut(&text_edit_state, &text_edit_state.state);

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (e.key.key == SDLK_V && SDL_GetModState() & SDL_KMOD_CTRL) {
						SDL_Event event;

						event.type = SDL_EVENT_TEXT_INPUT;
						event.text.text = SDL_GetClipboardText();
						event.text.reserved = 8372194;

						SDL_PushEvent(&event);

						showingCursor = true;
						currentTime = std::chrono::high_resolution_clock::now();
					}

					if (e.key.key == SDLK_A && SDL_GetModState() & SDL_KMOD_CTRL) {
						text_edit_state.state.select_start = 0;
						text_edit_state.state.select_end = text_edit_state.string.length();

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

						if (e.text.reserved == 8372194) {
							SDL_free((void*) e.text.text);
						}
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
	msdfgen::deinitializeFreetype(g_AppContext.ft);
	return 0;
}
#include <stdio.h>
#include <openvr.h>
#include <iostream>

#include <unistd.h>

#define GST_USE_UNSTABLE_API
#include <gst/gst.h>
#include <gst/gl/gl.h>
#include <gst/app/gstappsink.h>
#include <gst/gl/x11/gstgldisplay_x11.h>
#include <gst/gl/gstglmemory.h>

#include <gtk/gtk.h>

#define GL_GLEXT_PROTOTYPES 1
#define GL3_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glx.h>
#include <SDL.h>

using namespace vr;

void check_error(int line, EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, VR_GetVRInitErrorAsSymbol(error)); }


void GLAPIENTRY messageCallback( GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam ) {
	printf("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ), type, severity, message );
}

int VIDEOFPS = 30;

int cached_width = -1;
int cached_height = -1;
GLuint cached_texture = 0;

VROverlayHandle_t handle;

SDL_Window *mainwindow;
SDL_GLContext maincontext;

float x = 2.0;
float y = 1.0;
float z = -1.0;
float overlaywidth = 2.0;

void setTranslation() {
	VROverlay()->ShowOverlay(handle);
	std::cout << "Setting translation " << x << " " << y << " " << z << std::endl;
	vr::HmdMatrix34_t transform = {
		1.0f, 0.0f, 0.0f, x,
		0.0f, 1.0f, 0.0f, y,
		0.0f, 0.0f, 1.0f, z};
	VROverlay()->SetOverlayTransformAbsolute(handle, TrackingUniverseStanding, &transform);
}

void setWidth() {
	std::cout << "Setting width " << overlaywidth << "m" << std::endl;
	VROverlay()->SetOverlayWidthInMeters(handle, overlaywidth);
}

static GstFlowReturn
on_new_sample_from_sink (GstElement * elt, gpointer data)
{
	//std::cout << "Got sample" << std::endl;
	GstSample *sample;
	GstBuffer *buffer;
	/* get the sample from appsink */
	sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
	buffer = gst_sample_get_buffer (sample);

	int width, height;
	GstCaps *caps = gst_sample_get_caps(sample);
	GstStructure *capsStruct = gst_caps_get_structure(caps,0);
	gst_structure_get_int(capsStruct,"width",&width);
	gst_structure_get_int(capsStruct,"height",&height);

	static int counter = 0;
	GstMapInfo map;
	if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data (map.data, GDK_COLORSPACE_RGB, TRUE, 8, width, height, GST_ROUND_UP_4 (width * 4), NULL, NULL);

		// TODO: not on the CPU and not with a copy
		GdkPixbuf *original_pixbuf = pixbuf;
		pixbuf = gdk_pixbuf_flip  (pixbuf, FALSE);
		g_object_unref(original_pixbuf);

		int bps = gdk_pixbuf_get_bits_per_sample(pixbuf);
		guchar* rgb = gdk_pixbuf_get_pixels(pixbuf);

		// Worst: Save images and set from image.
		// SteamVR maintains cache filename - image, so we need a new filename every time
		/*
		 std::string fn = std::string("/tmp/test") + std::to_string(counter) + std::string(".png");
		 unlink(fn.c_str());
		 fn = std::string("/tmp/test") + std::to_string(++counter) + std::string(".png");
		 gdk_pixbuf_save (pixbuf, fn.c_str(), "png", NULL, NULL);
		 VROverlay()->SetOverlayFromFile(handle, fn.c_str());
		 */

		// better: raw pixel
		// not optimized in SteamVR, flickers on update
		//std::cout << "Uploading " << width << "x" << height << " " << bps << " buffer to overlay " << handle << std::endl;
		//VROverlay()->SetOverlayRaw(handle, rgb, width, height, 4);

		SDL_GL_MakeCurrent(mainwindow,maincontext);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		// still better: Copy to GL texture and update overlay with it
		if (width != cached_width || height != cached_height) {
			if (cached_texture != 0) {
				glDeleteTextures(1, &cached_texture);
			}
			glGenTextures(1, &cached_texture);
			glBindTexture(GL_TEXTURE_2D, cached_texture);
			std::cout << "Allocating new texture: " << width << "x" << height << std::endl;
			cached_height = height;
			cached_width = width;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)rgb);
		}

		glTexSubImage2D(GL_TEXTURE_2D, 0 ,0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)rgb);
		g_object_unref(pixbuf);

		Texture_t tex;
		tex.handle = (void*)cached_texture;
		tex.eColorSpace = ColorSpace_Auto;
		tex.eType = TextureType_OpenGL;
		//std::cout << "Uploading " << width << "x" << height << " " << bps << " OpenGL texture " << texid << "  to overlay " << handle << std::endl;
		VROverlay()->SetOverlayTexture(handle, &tex);

		// best: use gstreamer gstglupload to get GL texture directly
		// TODO

		gst_buffer_unmap(buffer, &map);
	}

	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
			exit(0); // screw cleaning up
		}
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_UP)
			y += 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_DOWN)
			y -= 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LEFT)
			x -= 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RIGHT)
			x += 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PAGEDOWN)
			z -= 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PAGEUP)
			z += 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PLUS)
			overlaywidth += 0.2;
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_MINUS)
			overlaywidth -= 0.2;
		if (event.type == SDL_KEYDOWN) {
			setTranslation();
			setWidth();
		}
	}

	gst_sample_unref (sample);
	return GST_FLOW_OK;
}

void checkSDLError(int line = -1)
{
	#ifndef NDEBUG
	const char *error = SDL_GetError();
	if (*error != '\0')
	{
		printf("SDL Error: %s\n", error);
		if (line != -1)
			printf(" + line: %i\n", line);
		SDL_ClearError();
	}
	#endif
}

int main(int argc, char **argv) { (void) argc; (void) argv;
	if (argc <= 1) {
		std::cout << "Arg 1 should be xid. Tipp: Start " << argv[0] << " " << "$(xwininfo |grep 'Window id' | awk '{print $4;}')" << std::endl;
		return 1;
	}

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		return 1;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	mainwindow = SDL_CreateWindow("window overlay companion window",
				      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
				      512, 512, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!mainwindow)
		return 1;
	checkSDLError(__LINE__);
	maincontext = SDL_GL_CreateContext(mainwindow);
	checkSDLError(__LINE__);
	SDL_GL_SetSwapInterval(0);

	printf("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
	printf("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
	printf("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));

	SDL_GL_MakeCurrent(0,0);

	glEnable              ( GL_DEBUG_OUTPUT );
	glDebugMessageCallback( messageCallback, 0 );

	EVRInitError error;
	VR_Init(&error, vr::VRApplication_Overlay);
	check_error(__LINE__, error);

	VROverlay()->CreateOverlay ("openvr.overlay.window", "A Window", &handle);
	std::cout << "Created overlay" << std::endl;

	setWidth();
	setTranslation();

	GstElement *pipeline;
	GstState state;
	GError *gsterror = NULL;
	gst_init (&argc, &argv);
	std::string pipelinestr = std::string("ximagesrc xid=") + std::string(argv[1]) + std::string(" use-damage=0 ! videoconvert ! video/x-raw,format=RGBA,framerate=") + std::to_string(VIDEOFPS) + std::string("/1 ! appsink name=\"appsink_element\" emit-signals=true");
	std::cout << "Starting pipeline: " << pipelinestr << std::endl;
	pipeline = gst_parse_launch (pipelinestr.c_str(), &gsterror);
	if (gsterror) {
		g_print ("Error while parsing pipeline description: %s\n", gsterror->message);
		return -1;
	}
	GstElement *appsink= gst_bin_get_by_name (GST_BIN (pipeline), "appsink_element");
	//g_object_set (G_OBJECT (appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

	g_signal_connect (appsink, "new-sample", G_CALLBACK (on_new_sample_from_sink), NULL);
	//gst_object_unref (appsink);

	GMainLoop *loop = g_main_loop_new (NULL, FALSE);
	gst_element_set_state (pipeline, GST_STATE_PLAYING);
	if (gst_element_get_state (pipeline, &state, NULL,
		GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_FAILURE ||
		state != GST_STATE_PLAYING) {
		g_warning ("State change to playing failed");
	}
	g_main_loop_run (loop);
	return 0;
}

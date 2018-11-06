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

using namespace vr;

void check_error(int line, EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, VR_GetVRInitErrorAsSymbol(error)); }


void GLAPIENTRY messageCallback( GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam ) {
	printf("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n", ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ), type, severity, message );
}

int VIDEOFPS = 5;

VROverlayHandle_t handle;
Display *dpy;
GLXContext ctx;
GLXPbuffer pbuf;


float x = 2.0;
float y = 1.0;
float z = -1.0;

static GstFlowReturn
on_new_sample_from_sink (GstElement * elt, gpointer data)
{
	//std::cout << "Got sample" << std::endl;
	GstSample *sample;
	GstBuffer *app_buffer, *buffer;
	GstElement *source;
	GstFlowReturn ret;
	/* get the sample from appsink */
	sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
	buffer = gst_sample_get_buffer (sample);

	/* make a copy */
	app_buffer = gst_buffer_copy (buffer);
	/* we don't need the appsink sample anymore */

	int width, height;
	GstCaps *caps = gst_sample_get_caps(sample);
	GstStructure *capsStruct = gst_caps_get_structure(caps,0);
	gst_structure_get_int(capsStruct,"width",&width);
	gst_structure_get_int(capsStruct,"height",&height);

	static int counter = 0;
	GstMapInfo map;
	if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
		auto pixbuf = gdk_pixbuf_new_from_data (map.data, GDK_COLORSPACE_RGB, TRUE, 8, width, height, GST_ROUND_UP_4 (width * 4), NULL, NULL);
		guchar* rgb = gdk_pixbuf_get_pixels(pixbuf);
		int bps = gdk_pixbuf_get_bits_per_sample(pixbuf);

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

		// still better: Copy to GL texture and update overlay with it
		if ( !glXMakeContextCurrent(dpy, pbuf, pbuf, ctx) ){ printf("failed to make current\n"); }
		GLuint texid;
		glGenTextures(1, &texid); // TODO: reuse
		glBindTexture(GL_TEXTURE_2D, texid);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)rgb);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);


		//glTexSubImage2D(GL_TEXTURE_2D, 0 ,0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)rgb);

		Texture_t tex;
		tex.handle = &texid;
		tex.eColorSpace = ColorSpace_Auto;
		tex.eType = TextureType_OpenGL;
		std::cout << "Uploading " << width << "x" << height << " " << bps << " OpenGL texture " << texid << "  to overlay " << handle << std::endl;
		VROverlay()->SetOverlayTexture(handle, &tex);

		glDeleteTextures(1, &texid);

		// best: use gstreamer gstglupload to get GL texture directly
		// TODO

		g_object_unref(pixbuf);
	}

	gst_sample_unref (sample);
	return GST_FLOW_OK;
}

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
typedef Bool (*glXMakeContextCurrentARBProc)(Display*, GLXDrawable, GLXDrawable, GLXContext);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
static glXMakeContextCurrentARBProc glXMakeContextCurrentARB = 0;

int main(int argc, char **argv) { (void) argc; (void) argv;
	if (argc <= 1) {
		std::cout << "Arg 1 should be xid. Tipp: Start " << argv[0] << " " << "$(xwininfo |grep 'Window id' | awk '{print $4;}')" << std::endl;
		return 1;
	}

	static int visual_attribs[] = {
		None
	};
	int context_attribs[] = {
		GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
		GLX_CONTEXT_MINOR_VERSION_ARB, 0,
		None
	};

	dpy = XOpenDisplay(0);
	int fbcount = 0;
	GLXFBConfig* fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), visual_attribs, &fbcount);
	if ( !fbc) { fprintf(stderr, "Failed to get FBConfig\n"); }

	glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)glXGetProcAddressARB( (const GLubyte *) "glXCreateContextAttribsARB");
	glXMakeContextCurrentARB = (glXMakeContextCurrentARBProc)glXGetProcAddressARB( (const GLubyte *) "glXMakeContextCurrent");
	if ( !(glXCreateContextAttribsARB && glXMakeContextCurrentARB) ){ fprintf(stderr, "missing support for GLX_ARB_create_context\n"); }

	if ( !( ctx = glXCreateContextAttribsARB(dpy, fbc[0], 0, True, context_attribs)) ){ fprintf(stderr, "Failed to create opengl context\n"); }

	int pbuffer_attribs[] = {
		GLX_PBUFFER_WIDTH, 800,
		GLX_PBUFFER_HEIGHT, 600,
		None
	};
	pbuf = glXCreatePbuffer(dpy, fbc[0], pbuffer_attribs);

	if ( !glXMakeContextCurrent(dpy, pbuf, pbuf, ctx) ){ printf("failed to make current\n"); }

	XFree(fbc);
	XSync(dpy, False);

	printf("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
	printf("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
	printf("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));

	glEnable              ( GL_DEBUG_OUTPUT );
	glDebugMessageCallback( messageCallback, 0 );

	if ( !glXMakeContextCurrent(dpy, 0, 0, 0) ){ printf("failed to make current\n"); }



	EVRInitError error;
	VR_Init(&error, vr::VRApplication_Overlay);
	check_error(__LINE__, error);

	VROverlay()->CreateOverlay ("openvr.overlay.window", "A Window", &handle);
	std::cout << "Created overlay" << std::endl;

	VROverlay()->SetOverlayWidthInMeters(handle, 3);
	VROverlay()->ShowOverlay(handle);
	vr::HmdMatrix34_t transform = {1.0f, 0.0f, 0.0f, x,
	                               0.0f, 1.0f, 0.0f, y,
	                               0.0f, 0.0f, 1.0f, z};
	VROverlay()->SetOverlayTransformAbsolute(handle, TrackingUniverseStanding, &transform);

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

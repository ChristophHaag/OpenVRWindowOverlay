#include <stdio.h>
#include <openvr.h>
#include <iostream>

#include <unistd.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
//#include <gdk-pixbuf-2.0/gdk-pixbuf/gdk-pixbuf-core.h>
#include <gtk/gtk.h>

using namespace vr;

void check_error(int line, EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, VR_GetVRInitErrorAsSymbol(error)); }

int VIDEOFPS = 5;

VROverlayHandle_t handle;

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

		// this crashes steamvr, badly
		int bps = gdk_pixbuf_get_bits_per_sample(pixbuf);
		std::cout << "Uploading " << width << "x" << height << " " << bps << " buffer to overlay " << handle << std::endl;
		VROverlay()->SetOverlayRaw(handle, rgb, width, height, 4);

		//eww
		/*
		std::string fn = std::string("/tmp/test") + std::to_string(counter) + std::string(".png");
		unlink(fn.c_str());
		fn = std::string("/tmp/test") + std::to_string(++counter) + std::string(".png");
		gdk_pixbuf_save (pixbuf, fn.c_str(), "png", NULL, NULL);
		VROverlay()->SetOverlayFromFile(handle, fn.c_str());
		*/

		g_object_unref(pixbuf);
	}

	gst_sample_unref (sample);
	return GST_FLOW_OK;
}

int main(int argc, char **argv) { (void) argc; (void) argv;
	if (argc <= 1) {
		std::cout << "Arg 1 should be xid. Tipp: Start " << argv[0] << " " << "$(xwininfo |grep 'Window id' | awk '{print $4;}')" << std::endl;
		return 1;
	}
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

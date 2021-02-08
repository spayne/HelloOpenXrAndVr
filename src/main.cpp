#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include <openvr.h>
#include <string>

#include "gfxwrapper_opengl.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // !WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <combaseapi.h>

using namespace std;

constexpr float DarkSlateGray[] = { 0.184313729f, 0.309803933f, 0.309803933f, 1.0f };

static const char* VertexShaderGlsl = R"_(
    #version 410

    in vec3 VertexPos;
    in vec3 VertexColor;

    out vec3 PSVertexColor;

    uniform mat4 ModelViewProjection;

    void main() {
       gl_Position = ModelViewProjection * vec4(VertexPos, 1.0);
       PSVertexColor = VertexColor;
    }
    )_";

static const char* FragmentShaderGlsl = R"_(
    #version 410

    in vec3 PSVertexColor;
    out vec4 FragColor;

    void main() {
       FragColor = vec4(PSVertexColor, 1);
    }
    )_";


struct {
	XrInstance m_instance;
	XrSystemId m_system_id;
	ksGpuWindow m_window;
} g_xr_state;

inline XrResult CheckXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
	if (XR_FAILED(res)) {
		printf("xrfailure: %d, line: %s\n", res, sourceLocation);
		exit(1);
	}

	return res;
}

#define CHK_STRINGIFY(x) #x
#define TOSTRING(x) CHK_STRINGIFY(x)
#define FILE_AND_LINE __FILE__ ":" TOSTRING(__LINE__)
#define CHECK_XRCMD(cmd) CheckXrResult(cmd, #cmd, FILE_AND_LINE);



inline int CheckOpenVrResult(int res, const char* originator = nullptr, const char* sourceLocation = nullptr)
{
	if (res != 0) {
		printf("openvr failure: %d, line: %s\n", res, sourceLocation);
	}
	return res;
}
#define CHECK_OPENVRCMD(cmd) CheckOpenVrResult(cmd, #cmd, FILE_AND_LINE)


void initialize_graphics(bool do_openxr)
{
	if (do_openxr)
	{
		// Extension function must be loaded by name
		PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;
		CHECK_XRCMD(xrGetInstanceProcAddr(g_xr_state.m_instance, "xrGetOpenGLGraphicsRequirementsKHR",
			reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLGraphicsRequirementsKHR)));

		XrGraphicsRequirementsOpenGLKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
		CHECK_XRCMD(pfnGetOpenGLGraphicsRequirementsKHR(g_xr_state.m_instance, g_xr_state.m_system_id, &graphicsRequirements));

		// Initialize the gl extensions. Note we have to open a window.
		ksDriverInstance driverInstance{};
		ksGpuQueueInfo queueInfo{};
		ksGpuSurfaceColorFormat colorFormat{ KS_GPU_SURFACE_COLOR_FORMAT_B8G8R8A8 };
		ksGpuSurfaceDepthFormat depthFormat{ KS_GPU_SURFACE_DEPTH_FORMAT_D24 };
		ksGpuSampleCount sampleCount{ KS_GPU_SAMPLE_COUNT_1 };
		if (!ksGpuWindow_Create(&g_xr_state.m_window, &driverInstance, &queueInfo, 0, colorFormat, depthFormat, sampleCount, 640, 480, false)) {
			printf("Unable to create GL context\n");
			exit(1);
		}


	}


}

void initialize_system(bool do_openxr)
{
	if (do_openxr)
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
		strcpy(createInfo.applicationInfo.applicationName, "HelloXrAndVr");
		createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
		const char *extensions = "XR_KHR_opengl_enable";
		createInfo.enabledExtensionCount = 1;
		createInfo.enabledExtensionNames = &extensions;

		CHECK_XRCMD(xrCreateInstance(&createInfo, &g_xr_state.m_instance));

		XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY };
		CHECK_XRCMD(xrGetSystem(g_xr_state.m_instance, &systemInfo, &g_xr_state.m_system_id));
	}
	else
	{
		vr::EVRInitError error;
		vr::VR_Init(&error, vr::VRApplication_Scene, nullptr);
		CHECK_OPENVRCMD(error);
		

		

	}
}

int main(int argc, char **argv)
{
	bool do_openxr = false;
	if (argc > 1 && string(argv[1]) == "--openxr")
	{
		do_openxr = true;
	}

	initialize_system(do_openxr);
	initialize_graphics(do_openxr);

	return 0;
}
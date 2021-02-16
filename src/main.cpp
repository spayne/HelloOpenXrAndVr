#include <windows.h>
#include <combaseapi.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include <openvr.h>
#include <string>
#include <list>
#include <vector>
#include <array>

#include "gfxwrapper_opengl.h"
#include "check_macros.h"
#include "geometry.h"
#include "shaders.cpp"

using namespace std;

namespace Side {
	const int LEFT = 0;
	const int RIGHT = 1;
	const int COUNT = 2;
}  // namespace Side

struct InputState {
	XrActionSet actionSet{ XR_NULL_HANDLE };
	XrAction grabAction{ XR_NULL_HANDLE };
	XrAction poseAction{ XR_NULL_HANDLE };
	XrAction vibrateAction{ XR_NULL_HANDLE };
	XrAction quitAction{ XR_NULL_HANDLE };
	std::array<XrPath, Side::COUNT> handSubactionPath;
	std::array<XrSpace, Side::COUNT> handSpace;
	std::array<float, Side::COUNT> handScale = { {1.0f, 1.0f} };
	std::array<XrBool32, Side::COUNT> handActive;
};

struct {
	XrInstance m_instance;
	XrSystemId m_system_id;
	XrSession m_session;
	InputState m_input;

	ksGpuWindow m_window;
	XrGraphicsBindingOpenGLWin32KHR m_graphicsBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
} g_xr_state;

std::list<std::vector<XrSwapchainImageOpenGLKHR>> m_swapchainImageBuffers;
GLuint m_swapchainFramebuffer{ 0 };
GLuint m_program{ 0 };
GLint m_modelViewProjectionUniformLocation{ 0 };
GLint m_vertexAttribCoords{ 0 };
GLint m_vertexAttribColor{ 0 };
GLuint m_vao{ 0 };
GLuint m_cubeVertexBuffer{ 0 };
GLuint m_cubeIndexBuffer{ 0 };





void initialize_resources()
{
	glGenFramebuffers(1, &m_swapchainFramebuffer);

	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &VertexShaderGlsl, nullptr);
	glCompileShader(vertexShader);
	CheckShader(vertexShader);

	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &FragmentShaderGlsl, nullptr);
	glCompileShader(fragmentShader);
	CheckShader(fragmentShader);

	m_program = glCreateProgram();
	glAttachShader(m_program, vertexShader);
	glAttachShader(m_program, fragmentShader);
	glLinkProgram(m_program);
	CheckProgram(m_program);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	m_modelViewProjectionUniformLocation = glGetUniformLocation(m_program, "ModelViewProjection");

	m_vertexAttribCoords = glGetAttribLocation(m_program, "VertexPos");
	m_vertexAttribColor = glGetAttribLocation(m_program, "VertexColor");

	glGenBuffers(1, &m_cubeVertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_cubeVertices), Geometry::c_cubeVertices, GL_STATIC_DRAW);

	glGenBuffers(1, &m_cubeIndexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_cubeIndices), Geometry::c_cubeIndices, GL_STATIC_DRAW);

	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);
	glEnableVertexAttribArray(m_vertexAttribCoords);
	glEnableVertexAttribArray(m_vertexAttribColor);
	glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
	glVertexAttribPointer(m_vertexAttribCoords, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex), nullptr);
	glVertexAttribPointer(m_vertexAttribColor, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex),
		reinterpret_cast<const void*>(sizeof(XrVector3f)));

}

void initialize_graphics(bool do_openxr)
{
	if (do_openxr)
	{
		// ref: graphicsplugin_opengl.cpp:InitializeDevice

		PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;
		CHECK_XRCMD(xrGetInstanceProcAddr(g_xr_state.m_instance, "xrGetOpenGLGraphicsRequirementsKHR",
			reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLGraphicsRequirementsKHR)));

		XrGraphicsRequirementsOpenGLKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
		CHECK_XRCMD(pfnGetOpenGLGraphicsRequirementsKHR(g_xr_state.m_instance, g_xr_state.m_system_id, &graphicsRequirements));

		ksDriverInstance driverInstance{};
		ksGpuQueueInfo queueInfo{};
		ksGpuSurfaceColorFormat colorFormat{ KS_GPU_SURFACE_COLOR_FORMAT_B8G8R8A8 };
		ksGpuSurfaceDepthFormat depthFormat{ KS_GPU_SURFACE_DEPTH_FORMAT_D24 };
		ksGpuSampleCount sampleCount{ KS_GPU_SAMPLE_COUNT_1 };
		if (!ksGpuWindow_Create(&g_xr_state.m_window, &driverInstance, &queueInfo, 0, colorFormat, depthFormat, sampleCount, 640, 480, false)) {
			THROW("Unable to create GL context\n");
		}

		GLint major = 0;
		GLint minor = 0;
		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);

		const XrVersion desiredApiVersion = XR_MAKE_VERSION(major, minor, 0);
		if (graphicsRequirements.minApiVersionSupported > desiredApiVersion) {
			THROW("Runtime does not support desired Graphics API and/or version");
		}

		g_xr_state.m_graphicsBinding.hDC = g_xr_state.m_window.context.hDC;
		g_xr_state.m_graphicsBinding.hGLRC = g_xr_state.m_window.context.hGLRC;

		glEnable(GL_DEBUG_OUTPUT);
		
		glDebugMessageCallback(DebugMessageCallback, nullptr);

		initialize_resources();


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

void initialize_session(bool do_openxr)
{
	XrSessionCreateInfo createInfo{ XR_TYPE_SESSION_CREATE_INFO };
	createInfo.next = &g_xr_state.m_graphicsBinding;
	createInfo.systemId = g_xr_state.m_system_id;
	CHECK_XRCMD(xrCreateSession(g_xr_state.m_instance, &createInfo, &g_xr_state.m_session));
}

void initialize_actions(bool do_openxr)
{
	{
		XrActionSetCreateInfo actionSetInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
		strcpy_s(actionSetInfo.actionSetName, "gameplay");
		strcpy_s(actionSetInfo.localizedActionSetName, "Gameplay");
		actionSetInfo.priority = 0;
		CHECK_XRCMD(xrCreateActionSet(g_xr_state.m_instance, &actionSetInfo, &g_xr_state.m_input.actionSet));
	}

	// Get the XrPath for the left and right hands - we will use them as subaction paths.
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left", &g_xr_state.m_input.handSubactionPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right", &g_xr_state.m_input.handSubactionPath[Side::RIGHT]));

	// Create actions.
	{
		// Create an input action for grabbing objects with the left and right hands.
		XrActionCreateInfo actionInfo{ XR_TYPE_ACTION_CREATE_INFO };
		actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
		strcpy_s(actionInfo.actionName, "grab_object");
		strcpy_s(actionInfo.localizedActionName, "Grab Object");
		actionInfo.countSubactionPaths = uint32_t(g_xr_state.m_input.handSubactionPath.size());
		actionInfo.subactionPaths = g_xr_state.m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(g_xr_state.m_input.actionSet, &actionInfo, &g_xr_state.m_input.grabAction));

		// Create an input action getting the left and right hand poses.
		actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
		strcpy_s(actionInfo.actionName, "hand_pose");
		strcpy_s(actionInfo.localizedActionName, "Hand Pose");
		actionInfo.countSubactionPaths = uint32_t(g_xr_state.m_input.handSubactionPath.size());
		actionInfo.subactionPaths = g_xr_state.m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(g_xr_state.m_input.actionSet, &actionInfo, &g_xr_state.m_input.poseAction));

		// Create output actions for vibrating the left and right controller.
		actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
		strcpy_s(actionInfo.actionName, "vibrate_hand");
		strcpy_s(actionInfo.localizedActionName, "Vibrate Hand");
		actionInfo.countSubactionPaths = uint32_t(g_xr_state.m_input.handSubactionPath.size());
		actionInfo.subactionPaths = g_xr_state.m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(g_xr_state.m_input.actionSet, &actionInfo, &g_xr_state.m_input.vibrateAction));

		// Create input actions for quitting the session using the left and right controller.
		// Since it doesn't matter which hand did this, we do not specify subaction paths for it.
		// We will just suggest bindings for both hands, where possible.
		actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
		strcpy_s(actionInfo.actionName, "quit_session");
		strcpy_s(actionInfo.localizedActionName, "Quit Session");
		actionInfo.countSubactionPaths = 0;
		actionInfo.subactionPaths = nullptr;
		CHECK_XRCMD(xrCreateAction(g_xr_state.m_input.actionSet, &actionInfo, &g_xr_state.m_input.quitAction));
	}

	std::array<XrPath, Side::COUNT> selectPath;
	std::array<XrPath, Side::COUNT> squeezeValuePath;
	std::array<XrPath, Side::COUNT> squeezeForcePath;
	std::array<XrPath, Side::COUNT> squeezeClickPath;
	std::array<XrPath, Side::COUNT> posePath;
	std::array<XrPath, Side::COUNT> hapticPath;
	std::array<XrPath, Side::COUNT> menuClickPath;
	std::array<XrPath, Side::COUNT> bClickPath;
	std::array<XrPath, Side::COUNT> triggerValuePath;
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/select/click", &selectPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/select/click", &selectPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/squeeze/value", &squeezeValuePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/squeeze/force", &squeezeForcePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/squeeze/force", &squeezeForcePath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/squeeze/click", &squeezeClickPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/squeeze/click", &squeezeClickPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/grip/pose", &posePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/grip/pose", &posePath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/output/haptic", &hapticPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/output/haptic", &hapticPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/menu/click", &menuClickPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/menu/click", &menuClickPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/b/click", &bClickPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/b/click", &bClickPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/left/input/trigger/value", &triggerValuePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(g_xr_state.m_instance, "/user/hand/right/input/trigger/value", &triggerValuePath[Side::RIGHT]));

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
	initialize_session(do_openxr);
	initialize_actions(do_openxr);

	return 0;
}
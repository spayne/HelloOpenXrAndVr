#include <windows.h>
#include <combaseapi.h>

#include <openxr/openxr.h>

#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include <openvr.h>
#include <string>
#include <locale>
#include <list>
#include <vector>
#include <array>
#include <algorithm>
#include <map>
#include <thread>

#include "gfxwrapper_opengl.h"
#include "check_macros.h"
#include "geometry.h"
#include "xr_linear.h"
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

struct Cube {
	XrPosef Pose;
	XrVector3f Scale;
};

struct Swapchain {
	XrSwapchain handle;
	int32_t width;
	int32_t height;
};

bool g_quitKeyPressed = false;

struct {
	XrInstance m_instance;
	XrSystemId m_system_id;
	XrSession m_session;
	InputState m_input;

	ksGpuWindow m_window;
	XrGraphicsBindingOpenGLWin32KHR m_graphicsBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };

	std::vector<XrSpace> m_visualizedSpaces;
	XrSpace m_appSpace;

	std::vector<XrViewConfigurationView> m_configViews;
	std::vector<XrView> m_views;

	int64_t m_color_swapchain_format;
	std::vector<Swapchain> m_swapchains;
	std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader*>> m_swapchain_images;

	XrEventDataBuffer m_eventDataBuffer;

	XrSessionState m_sessionState{ XR_SESSION_STATE_UNKNOWN };
	bool m_sessionRunning{ false };


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
// Map color buffer to associated depth buffer. This map is populated on demand.
std::map<uint32_t, uint32_t> m_colorToDepthMap;


inline bool EqualsIgnoreCase(const std::string& s1, const std::string& s2, const std::locale& loc = std::locale()) {
	const std::ctype<char>& ctype = std::use_facet<std::ctype<char>>(loc);
	const auto compareCharLower = [&](char c1, char c2) { return ctype.tolower(c1) == ctype.tolower(c2); };
	return s1.size() == s2.size() && std::equal(s1.begin(), s1.end(), s2.begin(), compareCharLower);
}

namespace Math {
	namespace Pose {
		XrPosef Identity() {
			XrPosef t{};
			t.orientation.w = 1;
			return t;
		}

		XrPosef Translation(const XrVector3f& translation) {
			XrPosef t = Identity();
			t.position = translation;
			return t;
		}

		XrPosef RotateCCWAboutYAxis(float radians, XrVector3f translation) {
			XrPosef t = Identity();
			t.orientation.x = 0.f;
			t.orientation.y = std::sin(radians * 0.5f);
			t.orientation.z = 0.f;
			t.orientation.w = std::cos(radians * 0.5f);
			t.position = translation;
			return t;
		}
	}  // namespace Pose
}  // namespace Math


inline XrReferenceSpaceCreateInfo util_GetXrReferenceSpaceCreateInfo(const std::string& referenceSpaceTypeStr) {
	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Identity();
	if (EqualsIgnoreCase(referenceSpaceTypeStr, "View")) {
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ViewFront")) {
		// Render head-locked 2m in front of device.
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Translation({ 0.f, 0.f, -2.f }),
			referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Local")) {
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Stage")) {
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeft")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, { -2.f, 0.f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, { 2.f, 0.f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(3.14f / 3.f, { -2.f, 0.5f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(-3.14f / 3.f, { 2.f, 0.5f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else {
		throw std::invalid_argument(Fmt("Unknown reference space type '%s'", referenceSpaceTypeStr.c_str()));
	}
	return referenceSpaceCreateInfo;
}

std::vector<XrSwapchainImageBaseHeader*> opengl_AllocateSwapchainImageStructs(
	uint32_t capacity, const XrSwapchainCreateInfo& /*swapchainCreateInfo*/) 
{
	// Allocate and initialize the buffer of image structs (must be sequential in memory for xrEnumerateSwapchainImages).
	// Return back an array of pointers to each swapchain image struct so the consumer doesn't need to know the type/size.
	std::vector<XrSwapchainImageOpenGLKHR> swapchainImageBuffer(capacity);
	std::vector<XrSwapchainImageBaseHeader*> swapchainImageBase;
	for (XrSwapchainImageOpenGLKHR& image : swapchainImageBuffer) {
		image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
		swapchainImageBase.push_back(reinterpret_cast<XrSwapchainImageBaseHeader*>(&image));
	}

	// Keep the buffer alive by moving it into the list of buffers.
	m_swapchainImageBuffers.push_back(std::move(swapchainImageBuffer));

	return swapchainImageBase;
}



//
// create the shader and vertex programs
//
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

		
		// create a GL window
		{
			ksDriverInstance driverInstance{};
			ksGpuQueueInfo queueInfo{};
			ksGpuSurfaceColorFormat colorFormat{ KS_GPU_SURFACE_COLOR_FORMAT_B8G8R8A8 };
			ksGpuSurfaceDepthFormat depthFormat{ KS_GPU_SURFACE_DEPTH_FORMAT_D24 };
			ksGpuSampleCount sampleCount{ KS_GPU_SAMPLE_COUNT_1 };
			if (!ksGpuWindow_Create(&g_xr_state.m_window, &driverInstance, &queueInfo, 0, colorFormat, depthFormat, sampleCount, 640, 480, false)) {
				THROW("Unable to create GL context\n");
			}
		}
		
		// make sure it meets the minimum requirements
		{
			PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;
			CHECK_XRCMD(xrGetInstanceProcAddr(g_xr_state.m_instance, "xrGetOpenGLGraphicsRequirementsKHR",
				reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLGraphicsRequirementsKHR)));

			XrGraphicsRequirementsOpenGLKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
			CHECK_XRCMD(pfnGetOpenGLGraphicsRequirementsKHR(g_xr_state.m_instance, g_xr_state.m_system_id, &graphicsRequirements));

			GLint major = 0;
			GLint minor = 0;
			glGetIntegerv(GL_MAJOR_VERSION, &major);
			glGetIntegerv(GL_MINOR_VERSION, &minor);

			const XrVersion desiredApiVersion = XR_MAKE_VERSION(major, minor, 0);
			if (graphicsRequirements.minApiVersionSupported > desiredApiVersion) {
				THROW("Runtime does not support desired Graphics API and/or version");
			}
		}
		
		// update my internal state
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


	// Suggest bindings for the Valve Index Controller.
	{
		XrPath indexControllerInteractionProfilePath;
		CHECK_XRCMD(
			xrStringToPath(g_xr_state.m_instance, "/interaction_profiles/valve/index_controller", &indexControllerInteractionProfilePath));
		std::vector<XrActionSuggestedBinding> bindings{ {
														{g_xr_state.m_input.grabAction, squeezeForcePath[Side::LEFT]},
														{g_xr_state.m_input.grabAction, squeezeForcePath[Side::RIGHT]},
														{g_xr_state.m_input.poseAction, posePath[Side::LEFT]},
														{g_xr_state.m_input.poseAction, posePath[Side::RIGHT]},
														{g_xr_state.m_input.quitAction, bClickPath[Side::LEFT]},
														{g_xr_state.m_input.quitAction, bClickPath[Side::RIGHT]},
														{g_xr_state.m_input.vibrateAction, hapticPath[Side::LEFT]},
														{g_xr_state.m_input.vibrateAction, hapticPath[Side::RIGHT]}} };
		XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggestedBindings.interactionProfile = indexControllerInteractionProfilePath;
		suggestedBindings.suggestedBindings = bindings.data();
		suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
		CHECK_XRCMD(xrSuggestInteractionProfileBindings(g_xr_state.m_instance, &suggestedBindings));
	}

	XrActionSpaceCreateInfo actionSpaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
	actionSpaceInfo.action = g_xr_state.m_input.poseAction;
	actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
	actionSpaceInfo.subactionPath = g_xr_state.m_input.handSubactionPath[Side::LEFT];
	CHECK_XRCMD(xrCreateActionSpace(g_xr_state.m_session, &actionSpaceInfo, &g_xr_state.m_input.handSpace[Side::LEFT]));
	actionSpaceInfo.subactionPath = g_xr_state.m_input.handSubactionPath[Side::RIGHT];
	CHECK_XRCMD(xrCreateActionSpace(g_xr_state.m_session, &actionSpaceInfo, &g_xr_state.m_input.handSpace[Side::RIGHT]));

	XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &g_xr_state.m_input.actionSet;
	CHECK_XRCMD(xrAttachSessionActionSets(g_xr_state.m_session, &attachInfo));
}

// these spaces will be used during render
void create_visualized_spaces(bool do_openxr)
{
	std::string visualizedSpaces[] = { "ViewFront", "Local", "Stage", "StageLeft", "StageRight", "StageLeftRotated",
										  "StageRightRotated" };

	for (const auto& visualizedSpace : visualizedSpaces) {
		XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = util_GetXrReferenceSpaceCreateInfo(visualizedSpace);
		XrSpace space;
		XrResult res = xrCreateReferenceSpace(g_xr_state.m_session, &referenceSpaceCreateInfo, &space);
		if (XR_SUCCEEDED(res)) {
			g_xr_state.m_visualizedSpaces.push_back(space);
		}
		else {
			THROW("Failed to create reference space");
		}
	}
}

void create_app_space(bool do_open_xr)
{
	std::string AppSpace{ "Local" };
	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = util_GetXrReferenceSpaceCreateInfo(AppSpace);
	CHECK_XRCMD(xrCreateReferenceSpace(g_xr_state.m_session, &referenceSpaceCreateInfo, &g_xr_state.m_appSpace));
}

int64_t util_SelectColorSwapchainFormat(const std::vector<int64_t>& runtimeFormats) {
	// List of supported color swapchain formats.
	constexpr int64_t SupportedColorSwapchainFormats[] = {
		GL_RGB10_A2,
		GL_RGBA16F,
		// The two below should only be used as a fallback, as they are linear color formats without enough bits for color
		// depth, thus leading to banding.
		GL_RGBA8,
		GL_RGBA8_SNORM,
	};

	auto swapchainFormatIt =
		std::find_first_of(runtimeFormats.begin(), runtimeFormats.end(), std::begin(SupportedColorSwapchainFormats),
			std::end(SupportedColorSwapchainFormats));
	if (swapchainFormatIt == runtimeFormats.end()) {
		THROW("No runtime swapchain format supported for color swapchain");
	}

	return *swapchainFormatIt;
}

void create_swap_chains(bool do_open_xr)
{
	// Read graphics properties for preferred swapchain length and logging.
	//XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };
	//CHECK_XRCMD(xrGetSystemProperties(g_xr_state.m_instance, g_xr_state.m_system_id, &systemProperties));

	// create a swapchain per view / two call protocol
	// Query and cache view configuration views.
	uint32_t view_count;
	CHECK_XRCMD(xrEnumerateViewConfigurationViews(g_xr_state.m_instance, g_xr_state.m_system_id, 
								XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr));
	g_xr_state.m_configViews.resize(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	CHECK_XRCMD(xrEnumerateViewConfigurationViews(g_xr_state.m_instance, g_xr_state.m_system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, g_xr_state.m_configViews.data()));

	// Create and cache view buffer for XrLocateViews later in render_layer
	g_xr_state.m_views.resize(view_count, { XR_TYPE_VIEW });

	// Create the swapchain and get the images
	if (view_count > 0)
	{
		// Select a swapchain format.
		uint32_t swapchain_format_count;
		CHECK_XRCMD(xrEnumerateSwapchainFormats(g_xr_state.m_session, 0, &swapchain_format_count, nullptr));
		std::vector<int64_t> swapchainFormats(swapchain_format_count);
		CHECK_XRCMD(xrEnumerateSwapchainFormats(g_xr_state.m_session, (uint32_t)swapchainFormats.size(), &swapchain_format_count,
			swapchainFormats.data()));
		CHECK(swapchain_format_count == swapchainFormats.size());

		// Used by RenderLayer
		g_xr_state.m_color_swapchain_format = util_SelectColorSwapchainFormat(swapchainFormats);

		// Create a swapchain for each view.
		for (uint32_t i = 0; i < view_count; i++) {
			const XrViewConfigurationView& vp = g_xr_state.m_configViews[i];

			// Create the swapchain.
			XrSwapchainCreateInfo swapchainCreateInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			swapchainCreateInfo.arraySize = 1;
			swapchainCreateInfo.format = g_xr_state.m_color_swapchain_format;
			swapchainCreateInfo.width = vp.recommendedImageRectWidth;
			swapchainCreateInfo.height = vp.recommendedImageRectHeight;
			swapchainCreateInfo.mipCount = 1;
			swapchainCreateInfo.faceCount = 1;
			swapchainCreateInfo.sampleCount = 1; // graphicsplugin_opengl.cpp
			swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			Swapchain swapchain;
			swapchain.width = swapchainCreateInfo.width;
			swapchain.height = swapchainCreateInfo.height;
			CHECK_XRCMD(xrCreateSwapchain(g_xr_state.m_session, &swapchainCreateInfo, &swapchain.handle));

			g_xr_state.m_swapchains.push_back(swapchain);

			uint32_t imageCount;
			CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr));

			std::vector<XrSwapchainImageBaseHeader*> swapchainImages =
				opengl_AllocateSwapchainImageStructs(imageCount, swapchainCreateInfo);
			CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, swapchainImages[0]));

			g_xr_state.m_swapchain_images.insert(std::make_pair(swapchain.handle, std::move(swapchainImages)));
		}
	}
}

const XrEventDataBaseHeader* TryReadNextEvent() {
	// It is sufficient to clear the just the XrEventDataBuffer header to
	// XR_TYPE_EVENT_DATA_BUFFER
	XrEventDataBaseHeader* baseHeader = reinterpret_cast<XrEventDataBaseHeader*>(&g_xr_state.m_eventDataBuffer);
	*baseHeader = { XR_TYPE_EVENT_DATA_BUFFER };
	const XrResult xr = xrPollEvent(g_xr_state.m_instance, &g_xr_state.m_eventDataBuffer);
	if (xr == XR_SUCCESS) {
		if (baseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
			const XrEventDataEventsLost* const eventsLost = reinterpret_cast<const XrEventDataEventsLost*>(baseHeader);
			printf("%d events lost\n", eventsLost->lostEventCount);
		}

		return baseHeader;
	}
	if (xr == XR_EVENT_UNAVAILABLE) {
		return nullptr;
	}
	THROW_XR(xr, "xrPollEvent");
}

void HandleSessionStateChangedEvent(const XrEventDataSessionStateChanged& stateChangedEvent, bool* exitRenderLoop,
	bool* requestRestart) {
	const XrSessionState oldState = g_xr_state.m_sessionState;
	g_xr_state.m_sessionState = stateChangedEvent.state;

	printf("XrEventDataSessionStateChanged: state %s->%s session=%lld time=%lld", to_string(oldState).c_str(),
		to_string(g_xr_state.m_sessionState).c_str(), (int64_t)stateChangedEvent.session, stateChangedEvent.time);

	if ((stateChangedEvent.session != XR_NULL_HANDLE) && (stateChangedEvent.session != g_xr_state.m_session)) {
		printf("XrEventDataSessionStateChanged for unknown session\n");
		return;
	}

	switch (g_xr_state.m_sessionState) {
	case XR_SESSION_STATE_READY: {
		CHECK(g_xr_state.m_session != XR_NULL_HANDLE);
		XrSessionBeginInfo sessionBeginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
		sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		CHECK_XRCMD(xrBeginSession(g_xr_state.m_session, &sessionBeginInfo));
		g_xr_state.m_sessionRunning = true;
		break;
	}
	case XR_SESSION_STATE_STOPPING: {
		CHECK(g_xr_state.m_session != XR_NULL_HANDLE);
		g_xr_state.m_sessionRunning = false;
		CHECK_XRCMD(xrEndSession(g_xr_state.m_session))
			break;
	}
	case XR_SESSION_STATE_EXITING: {
		*exitRenderLoop = true;
		// Do not attempt to restart because user closed this session.
		*requestRestart = false;
		break;
	}
	case XR_SESSION_STATE_LOSS_PENDING: {
		*exitRenderLoop = true;
		// Poll for a new instance.
		*requestRestart = true;
		break;
	}
	default:
		break;
	}
}

void poll_events(bool* exitRenderLoop, bool* requestRestart)
{
	*exitRenderLoop = *requestRestart = false;

	// Process all pending messages.
	while (const XrEventDataBaseHeader* event = TryReadNextEvent()) {
		switch (event->type) {
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
			const auto& instanceLossPending = *reinterpret_cast<const XrEventDataInstanceLossPending*>(event);
			*exitRenderLoop = true;
			*requestRestart = true;
			return;
		}
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			auto sessionStateChangedEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(event);
			HandleSessionStateChangedEvent(sessionStateChangedEvent, exitRenderLoop, requestRestart);
			break;
		}
		case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
			break;
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
		default: {
			break;
		}
		}
	}
}

void poll_actions()
{
	g_xr_state.m_input.handActive = { XR_FALSE, XR_FALSE };

	// Sync actions
	const XrActiveActionSet activeActionSet{ g_xr_state.m_input.actionSet, XR_NULL_PATH };
	XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	CHECK_XRCMD(xrSyncActions(g_xr_state.m_session, &syncInfo));

	// Get pose and grab action state and start haptic vibrate when hand is 90% squeezed.
	for (auto hand : { Side::LEFT, Side::RIGHT }) {
		XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		getInfo.action = g_xr_state.m_input.grabAction;
		getInfo.subactionPath = g_xr_state.m_input.handSubactionPath[hand];

		XrActionStateFloat grabValue{ XR_TYPE_ACTION_STATE_FLOAT };
		CHECK_XRCMD(xrGetActionStateFloat(g_xr_state.m_session, &getInfo, &grabValue));
		if (grabValue.isActive == XR_TRUE) {
			// Scale the rendered hand by 1.0f (open) to 0.5f (fully squeezed).
			g_xr_state.m_input.handScale[hand] = 1.0f - 0.5f * grabValue.currentState;
			if (grabValue.currentState > 0.9f) {
				XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
				vibration.amplitude = 0.5;
				vibration.duration = XR_MIN_HAPTIC_DURATION;
				vibration.frequency = XR_FREQUENCY_UNSPECIFIED;

				XrHapticActionInfo hapticActionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
				hapticActionInfo.action = g_xr_state.m_input.vibrateAction;
				hapticActionInfo.subactionPath = g_xr_state.m_input.handSubactionPath[hand];
				CHECK_XRCMD(xrApplyHapticFeedback(g_xr_state.m_session, &hapticActionInfo, (XrHapticBaseHeader*)&vibration));
			}
		}

		getInfo.action = g_xr_state.m_input.poseAction;
		XrActionStatePose poseState{ XR_TYPE_ACTION_STATE_POSE };
		CHECK_XRCMD(xrGetActionStatePose(g_xr_state.m_session, &getInfo, &poseState));
		g_xr_state.m_input.handActive[hand] = poseState.isActive;
	}

	// There were no subaction paths specified for the quit action, because we don't care which hand did it.
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, g_xr_state.m_input.quitAction, XR_NULL_PATH };
	XrActionStateBoolean quitValue{ XR_TYPE_ACTION_STATE_BOOLEAN };
	CHECK_XRCMD(xrGetActionStateBoolean(g_xr_state.m_session, &getInfo, &quitValue));
	if ((quitValue.isActive == XR_TRUE) && (quitValue.changedSinceLastSync == XR_TRUE) && (quitValue.currentState == XR_TRUE)) {
		CHECK_XRCMD(xrRequestExitSession(g_xr_state.m_session));
	}
}

uint32_t GetDepthTexture(uint32_t colorTexture) {
	// If a depth-stencil view has already been created for this back-buffer, use it.
	auto depthBufferIt = m_colorToDepthMap.find(colorTexture);
	if (depthBufferIt != m_colorToDepthMap.end()) {
		return depthBufferIt->second;
	}

	// This back-buffer has no corresponding depth-stencil texture, so create one with matching dimensions.

	GLint width;
	GLint height;
	glBindTexture(GL_TEXTURE_2D, colorTexture);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

	uint32_t depthTexture;
	glGenTextures(1, &depthTexture);
	glBindTexture(GL_TEXTURE_2D, depthTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

	m_colorToDepthMap.insert(std::make_pair(colorTexture, depthTexture));

	return depthTexture;
}

void OpenGL_RenderView(
	const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage,
	int64_t swapchainFormat, const std::vector<Cube>& cubes) 
{
	CHECK(layerView.subImage.imageArrayIndex == 0);  // Texture arrays not supported.
	UNUSED_PARM(swapchainFormat);                    // Not used in this function for now.

	glBindFramebuffer(GL_FRAMEBUFFER, m_swapchainFramebuffer);

	const uint32_t colorTexture = reinterpret_cast<const XrSwapchainImageOpenGLKHR*>(swapchainImage)->image;

	glViewport(static_cast<GLint>(layerView.subImage.imageRect.offset.x),
		static_cast<GLint>(layerView.subImage.imageRect.offset.y),
		static_cast<GLsizei>(layerView.subImage.imageRect.extent.width),
		static_cast<GLsizei>(layerView.subImage.imageRect.extent.height));

	glFrontFace(GL_CW);
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	const uint32_t depthTexture = GetDepthTexture(colorTexture);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

	// Clear swapchain and depth buffer.
	glClearColor(DarkSlateGray[0], DarkSlateGray[1], DarkSlateGray[2], DarkSlateGray[3]);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	// Set shaders and uniform variables.
	glUseProgram(m_program);

	const auto& pose = layerView.pose;
	XrMatrix4x4f proj;
	XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_OPENGL, layerView.fov, 0.05f, 100.0f);
	XrMatrix4x4f toView;
	XrVector3f scale{ 1.f, 1.f, 1.f };
	XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scale);
	XrMatrix4x4f view;
	XrMatrix4x4f_InvertRigidBody(&view, &toView);
	XrMatrix4x4f vp;
	XrMatrix4x4f_Multiply(&vp, &proj, &view);

	// Set cube primitive data.
	glBindVertexArray(m_vao);

	// Render each cube
	for (const Cube& cube : cubes) {
		// Compute the model-view-projection transform and set it..
		XrMatrix4x4f model;
		XrMatrix4x4f_CreateTranslationRotationScale(&model, &cube.Pose.position, &cube.Pose.orientation, &cube.Scale);
		XrMatrix4x4f mvp;
		XrMatrix4x4f_Multiply(&mvp, &vp, &model);
		glUniformMatrix4fv(m_modelViewProjectionUniformLocation, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(&mvp));

		// Draw the cube.
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_cubeIndices)), GL_UNSIGNED_SHORT, nullptr);
	}

	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Swap our window every other eye for RenderDoc
	static int everyOther = 0;
	if ((everyOther++ & 1) != 0) {
		ksGpuWindow_SwapBuffers(&g_xr_state.m_window);
	}
}


bool render_layer(XrTime predictedDisplayTime, std::vector<XrCompositionLayerProjectionView>& projectionLayerViews,
	XrCompositionLayerProjection& layer) {
	XrResult res;

	XrViewState viewState{ XR_TYPE_VIEW_STATE };
	uint32_t viewCapacityInput = (uint32_t)g_xr_state.m_views.size();
	uint32_t viewCountOutput;

	XrViewLocateInfo viewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
	viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	viewLocateInfo.displayTime = predictedDisplayTime;
	viewLocateInfo.space = g_xr_state.m_appSpace;

	res = xrLocateViews(g_xr_state.m_session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, 
						g_xr_state.m_views.data());

	CHECK_XRRESULT(res, "xrLocateViews");
	if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
		(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
		return false;  // There is no valid tracking poses for the views.
	}

	CHECK(viewCountOutput == viewCapacityInput);
	CHECK(viewCountOutput == g_xr_state.m_configViews.size());
	CHECK(viewCountOutput == g_xr_state.m_swapchains.size());

	projectionLayerViews.resize(viewCountOutput);

	// For each locatable space that we want to visualize, render a 25cm cube.
	std::vector<Cube> cubes;

	for (XrSpace visualizedSpace : g_xr_state.m_visualizedSpaces) {
		XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
		res = xrLocateSpace(visualizedSpace, g_xr_state.m_appSpace, predictedDisplayTime, &spaceLocation);
		CHECK_XRRESULT(res, "xrLocateSpace");
		if (XR_UNQUALIFIED_SUCCESS(res)) {
			if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
				(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
				cubes.push_back(Cube{ spaceLocation.pose, {0.25f, 0.25f, 0.25f} });
			}
		}
		else {
			printf("Unable to locate a visualized reference space in app space: %d", res);
		}
	}

	// Render a 10cm cube scaled by grabAction for each hand. Note renderHand will only be
	// true when the application has focus.
	for (auto hand : { Side::LEFT, Side::RIGHT }) {
		XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
		res = xrLocateSpace(g_xr_state.m_input.handSpace[hand], g_xr_state.m_appSpace, predictedDisplayTime, &spaceLocation);
		CHECK_XRRESULT(res, "xrLocateSpace");
		if (XR_UNQUALIFIED_SUCCESS(res)) {
			if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
				(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
				float scale = 0.1f * g_xr_state.m_input.handScale[hand];
				cubes.push_back(Cube{ spaceLocation.pose, {scale, scale, scale} });
			}
		}
		else {
			// Tracking loss is expected when the hand is not active so only log a message
			// if the hand is active.
			if (g_xr_state.m_input.handActive[hand] == XR_TRUE) {
				const char* handName[] = { "left", "right" };
				printf("Unable to locate %s hand action space in app space: %d", handName[hand], res);
			}
		}
	}

	// Render view to the appropriate part of the swapchain image.
	for (uint32_t i = 0; i < viewCountOutput; i++) {
		// Each view has a separate swapchain which is acquired, rendered to, and released.
		const Swapchain viewSwapchain = g_xr_state.m_swapchains[i];

		XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };

		uint32_t swapchainImageIndex;
		CHECK_XRCMD(xrAcquireSwapchainImage(viewSwapchain.handle, &acquireInfo, &swapchainImageIndex));

		XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		waitInfo.timeout = XR_INFINITE_DURATION;
		CHECK_XRCMD(xrWaitSwapchainImage(viewSwapchain.handle, &waitInfo));

		projectionLayerViews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
		projectionLayerViews[i].pose = g_xr_state.m_views[i].pose;
		projectionLayerViews[i].fov = g_xr_state.m_views[i].fov;
		projectionLayerViews[i].subImage.swapchain = viewSwapchain.handle;
		projectionLayerViews[i].subImage.imageRect.offset = { 0, 0 };
		projectionLayerViews[i].subImage.imageRect.extent = { viewSwapchain.width, viewSwapchain.height };

		const XrSwapchainImageBaseHeader* const swapchainImage = g_xr_state.m_swapchain_images[viewSwapchain.handle][swapchainImageIndex];
		OpenGL_RenderView(projectionLayerViews[i], swapchainImage, g_xr_state.m_color_swapchain_format, cubes);

		XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		CHECK_XRCMD(xrReleaseSwapchainImage(viewSwapchain.handle, &releaseInfo));
	}

	layer.space = g_xr_state.m_appSpace;
	layer.viewCount = (uint32_t)projectionLayerViews.size();
	layer.views = projectionLayerViews.data();
	return true;
}



void render_frame()
{
	CHECK(g_xr_state.m_session != XR_NULL_HANDLE);

	XrFrameWaitInfo frameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frameState{ XR_TYPE_FRAME_STATE };
	CHECK_XRCMD(xrWaitFrame(g_xr_state.m_session, &frameWaitInfo, &frameState));

	XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
	CHECK_XRCMD(xrBeginFrame(g_xr_state.m_session, &frameBeginInfo));

	std::vector<XrCompositionLayerBaseHeader*> layers;
	XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	std::vector<XrCompositionLayerProjectionView> projectionLayerViews;
	if (frameState.shouldRender == XR_TRUE) {
		if (render_layer(frameState.predictedDisplayTime, projectionLayerViews, layer)) {
			layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
		}
	}

	XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO };
	frameEndInfo.displayTime = frameState.predictedDisplayTime;
	frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	frameEndInfo.layerCount = (uint32_t)layers.size();
	frameEndInfo.layers = layers.data();
	CHECK_XRCMD(xrEndFrame(g_xr_state.m_session, &frameEndInfo));
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
	create_visualized_spaces(do_openxr);
	create_app_space(do_openxr);
	create_swap_chains(do_openxr);

	bool requestRestart = false;
	do {
		while (!g_quitKeyPressed)
		{
			bool exitRenderLoop = false;
			poll_events(&exitRenderLoop, &requestRestart);
			if (exitRenderLoop) {
				break;
			}

			if (g_xr_state.m_sessionRunning) {
				poll_actions();
				render_frame();
			}
			else {
				// Throttle loop since xrWaitFrame won't be called.
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
			}
		}
	} while (!g_quitKeyPressed && requestRestart);



	return 0;
}
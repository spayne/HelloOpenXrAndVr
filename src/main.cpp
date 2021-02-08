#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include <openvr.h>
#include <string>

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


void init(bool do_openxr)
{
	if (do_openxr)
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
		strcpy(createInfo.applicationInfo.applicationName, "HelloXrAndVr");
		createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

		CHECK_XRCMD(xrCreateInstance(&createInfo, &g_xr_state.m_instance));

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

	init(do_openxr);

	return 0;
}
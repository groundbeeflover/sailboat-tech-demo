#include "boat.h"
#include "camera.h"
#include "course.h"
#include "feature_toggles.h"
#include "renderer.h"
#include "sky.h"
#include "water.h"
#include "waves.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui/imgui.h>
#include <stb/stb_image_write.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// Command-line options, mainly for reproducible screenshots:
//   Master_TechDemo --capture out.png [--frames 240] [--camera chase|deck|top|orbit] [--time 15.5] [--wind 60]
//                   [--set name=value ...]
// renders that many frames with a fixed time step, saves the image and exits.
// --set overrides any FeatureToggles field listed in applySetting(), e.g. --set shadows=0 --set pcfRadius=2,
// which makes on/off screenshot pairs for the report easy to reproduce.
struct LaunchOptions {
    std::string capturePath;
    int captureFrames { 240 };
    std::optional<CameraMode> camera;
    std::optional<float> timeOfDay;
    std::optional<float> windFromDeg;
    std::vector<std::string> settings; // "name=value"
};

static bool applySetting(FeatureToggles& t, const std::string& assignment)
{
    const size_t eq = assignment.find('=');
    if (eq == std::string::npos)
        return false;
    const std::string name = assignment.substr(0, eq);
    const std::string value = assignment.substr(eq + 1);

    const std::pair<const char*, bool*> bools[] = {
        { "pauseBoat", &t.pauseBoat }, { "constantSpeed", &t.constantSpeed }, { "windAffectsSpeed", &t.windAffectsSpeed },
        { "buoyancy", &t.buoyancy }, { "showCurve", &t.showCurve }, { "showControlPolygon", &t.showControlPolygon },
        { "waves", &t.waves }, { "waterDetailNormals", &t.waterDetailNormals }, { "waterWireframe", &t.waterWireframe },
        { "pbr", &t.pbr }, { "textures", &t.textures }, { "normalMapping", &t.normalMapping },
        { "environmentMap", &t.environmentMap }, { "skybox", &t.skybox }, { "fog", &t.fog },
        { "shadows", &t.shadows }, { "showShadowMap", &t.showShadowMap }, { "dayNightCycle", &t.dayNightCycle },

        // Track A toggles:

        // Track B toggles:

    };
    const std::pair<const char*, float*> floats[] = {
        { "windFromDeg", &t.windFromDeg }, { "windStrength", &t.windStrength }, { "boatSpeed", &t.boatSpeed },
        { "courseTension", &t.courseTension }, { "waveScale", &t.waveScale }, { "shadowBias", &t.shadowBias },
        { "shadowExtent", &t.shadowExtent }, { "timeOfDay", &t.timeOfDay }, { "exposure", &t.exposure },

        // Track A parameters:

        // Track B parameters:

    };
    const std::pair<const char*, int*> ints[] = {
        { "waveCount", &t.waveCount }, { "pcfRadius", &t.pcfRadius }, { "shadowResolution", &t.shadowResolution },

        // Track A parameters:

        // Track B parameters:

    };
    for (const auto& [key, field] : bools) {
        if (name == key) {
            *field = value == "1" || value == "true" || value == "on";
            return true;
        }
    }
    for (const auto& [key, field] : floats) {
        if (name == key) {
            *field = std::stof(value);
            return true;
        }
    }
    for (const auto& [key, field] : ints) {
        if (name == key) {
            *field = std::stoi(value);
            return true;
        }
    }
    return false;
}

class Application {
public:
    explicit Application(const LaunchOptions& options)
        : m_window("Sailboat Tech Demo", glm::ivec2(1280, 800), OpenGLVersion::GL41, true, options.capturePath.empty())
        , m_options(options)
    {
        if (options.camera)
            m_toggles.camera = *options.camera;
        if (options.timeOfDay)
            m_toggles.timeOfDay = *options.timeOfDay;
        if (options.windFromDeg)
            m_toggles.windFromDeg = *options.windFromDeg;
        for (const std::string& setting : options.settings) {
            if (!applySetting(m_toggles, setting))
                std::cerr << "Unknown setting: " << setting << std::endl;
        }

        m_window.registerKeyCallback([this](int key, int /* scancode */, int action, int /* mods */) {
            if (action == GLFW_PRESS)
                onKeyPressed(key);
        });
        m_window.registerMouseMoveCallback([this](const glm::vec2& cursor) {
            const bool dragging = m_window.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) && !ImGui::GetIO().WantCaptureMouse;
            m_cameras.onMouseMove(cursor, dragging && m_toggles.camera == CameraMode::Orbit);
        });
        m_window.registerScrollCallback([this](const glm::vec2& offset) {
            if (!ImGui::GetIO().WantCaptureMouse && m_toggles.camera == CameraMode::Orbit)
                m_cameras.onScroll(offset.y);
        });
    }

    void run()
    {
        const bool capturing = !m_options.capturePath.empty();
        double previous = glfwGetTime();
        int frameCount = 0;

        while (!m_window.shouldClose()) {
            m_window.updateInput();

            // Real elapsed time keeps the animation speed independent of the frame rate; the clamp
            // avoids a huge jump after a stall (e.g. dragging the window). Captures use a fixed step.
            const double now = glfwGetTime();
            const float dt = capturing ? 1.0f / 60.0f : static_cast<float>(std::min(now - previous, 0.1));
            previous = now;
            m_time += dt;
            m_frameTime += (dt - m_frameTime) * 0.05f;

            drawUi();
            update(dt);

            const glm::ivec2 framebufferSize = m_window.getFrameBufferSize();
            const CameraInputs cameraInputs {
                .boatPosition = m_boat.coursePosition(),
                .boatYaw = m_boat.yaw(),
                .deckCameraWorld = m_boat.deckCameraWorld(),
                .courseCenter = m_course.center(),
                .courseRadius = m_course.radius(),
                .aspect = static_cast<float>(framebufferSize.x) / static_cast<float>(std::max(framebufferSize.y, 1)),
                .dt = dt,
            };
            FrameContext frame {
                .camera = m_cameras.update(m_toggles.camera, cameraInputs),
                .lighting = m_lighting,
                .time = m_time,
                .framebufferSize = framebufferSize,
                // Halfway up the mast, so the light box holds the whole rig.
                .shadowCenter = m_boat.coursePosition() + glm::vec3(0.0f, 4.0f, 0.0f),
            };
            // ---- Track A: provide the spotlight here once a light source exists, e.g.
            //      frame.spot = m_island.lampLight();

            RenderLists lists;
            m_boat.root().collect(lists.opaque);
            m_course.root().collect(lists.opaque);
            m_course.appendLines(lists.lines, m_toggles, m_waves, m_time);
            if (m_toggles.camera == CameraMode::Top) // the boat is only a few pixels on the map, so mark it
                lists.lines.push_back({ .points = { m_boat.coursePosition() + glm::vec3(0.0f, 2.0f, 0.0f) }, .mode = LineMode::Points, .color = glm::vec3(3.0f, 0.2f, 0.1f), .pointSize = 14.0f });
            // ---- Track A: add your scene content here, e.g.
            //      m_island.root().collect(lists.opaque);
            //      lists.transparent.push_back([this](const FrameContext& f) { m_particles.draw(f); });

            m_renderer.render(frame, lists, m_water, m_waves, m_sky, m_toggles);

            // Screenshots are read before ImGui draws on top, so they show the scene only.
            if (m_screenshotRequested) {
                saveScreenshot(timestampedScreenshotPath());
                m_screenshotRequested = false;
            }
            if (capturing && ++frameCount >= m_options.captureFrames) {
                saveScreenshot(m_options.capturePath);
                break;
            }
            m_window.swapBuffers();
        }
    }

private:
    void update(float dt)
    {
        if (m_toggles.dayNightCycle)
            m_toggles.timeOfDay = std::fmod(m_toggles.timeOfDay + 24.0f * dt / m_toggles.dayLengthSeconds, 24.0f);
        m_lighting = computeLighting(m_toggles.timeOfDay);
        m_sky.update(m_lighting.sunDirection);

        m_waves.update(dt, glm::radians(m_toggles.windFromDeg), m_toggles.windStrength, m_toggles.waveCount, m_toggles.waveScale, m_toggles.waves);
        m_course.setTension(m_toggles.courseTension);
        m_course.update(m_time, m_waves);
        m_boat.update(dt, m_time, m_toggles, m_course.path(), m_waves);

        // ---- Track A systems update here (island, particles, flag, ...) ----

        // ---- Track B per-frame updates here (if any) ----
    }

    // The panel is one ImGui window with a collapsible section per feature. Each section is its own
    // method, so a new feature adds a method and a single call in the slot of its track (fewer merge conflicts).
    void drawUi()
    {
        ImGui::Begin("Sailboat tech demo");
        // printf-style varargs promote float to double anyway; the casts just make that explicit.
        ImGui::Text("%.0f fps (%.2f ms)", static_cast<double>(1.0f / std::max(m_frameTime, 1e-5f)), static_cast<double>(1000.0f * m_frameTime));

        viewpointUi();
        windUi();
        boatAndCourseUi();
        waterUi();
        shadingUi();
        shadowUi();
        skyUi();

        // ---- Track A feature panels (one call per feature) ----

        // ---- Track B feature panels (one call per feature) ----

        if (ImGui::Button("Save screenshot (P)"))
            m_screenshotRequested = true;
        ImGui::End();
    }

    void viewpointUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Viewpoint", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* names[] = { "Chase (1)", "Deck, attached to the hull (2)", "Top, orthographic (3)", "Orbit, drag + scroll (4)" };
            int camera = static_cast<int>(t.camera);
            if (ImGui::Combo("Camera", &camera, names, IM_ARRAYSIZE(names)))
                t.camera = static_cast<CameraMode>(camera);
        }
    }

    void windUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("From (deg)", &t.windFromDeg, 0.0f, 360.0f, "%.0f");
            ImGui::SliderFloat("Strength", &t.windStrength, 0.0f, 1.0f);
        }
    }

    void boatAndCourseUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Boat and course", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Pause boat (space)", &t.pauseBoat);
            ImGui::SliderFloat("Speed (m/s)", &t.boatSpeed, 0.0f, 15.0f);
            ImGui::Checkbox("Constant speed (arc length)", &t.constantSpeed);
            ImGui::Checkbox("Wind affects speed", &t.windAffectsSpeed);
            ImGui::Checkbox("Buoyancy (float on the waves)", &t.buoyancy);
            ImGui::Checkbox("Show Bezier curve (C)", &t.showCurve);
            ImGui::Checkbox("Show control polygon", &t.showControlPolygon);
            ImGui::SliderFloat("Curve tension", &t.courseTension, 0.0f, 2.0f);
            const Boat::Telemetry& b = m_boat.telemetry();
            ImGui::Text("speed %.2f m/s   u = %.3f   s = %.0f m", static_cast<double>(b.speed), static_cast<double>(b.courseParameter), static_cast<double>(b.distance));
            ImGui::Text("wind angle %.0f   boom %.0f   heel %.1f   rudder %.1f", static_cast<double>(b.windAngleDeg), static_cast<double>(b.boomDeg), static_cast<double>(b.heelDeg), static_cast<double>(b.rudderDeg));
        }
    }

    void waterUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Water")) {
            ImGui::Checkbox("Gerstner waves", &t.waves);
            ImGui::SliderInt("Wave count", &t.waveCount, 1, Waves::MaxWaves);
            ImGui::SliderFloat("Wave height", &t.waveScale, 0.0f, 2.0f);
            ImGui::Checkbox("Detail normal map", &t.waterDetailNormals);
            ImGui::Checkbox("Wireframe", &t.waterWireframe);
        }
    }

    void shadingUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Shading")) {
            ImGui::Checkbox("PBR (off = Blinn-Phong)", &t.pbr);
            ImGui::Checkbox("Material textures", &t.textures);
            ImGui::Checkbox("Normal mapping", &t.normalMapping);
            ImGui::Checkbox("Environment map", &t.environmentMap);
            ImGui::Checkbox("Skybox", &t.skybox);
            ImGui::Checkbox("Fog", &t.fog);
        }
    }

    void shadowUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Shadows")) {
            ImGui::Checkbox("Sun shadows (shadow map)", &t.shadows);
            const char* resolutions[] = { "1024", "2048", "4096" };
            int resolutionIndex = t.shadowResolution <= 1024 ? 0 : (t.shadowResolution <= 2048 ? 1 : 2);
            if (ImGui::Combo("Resolution", &resolutionIndex, resolutions, IM_ARRAYSIZE(resolutions)))
                t.shadowResolution = 1024 << resolutionIndex;
            ImGui::SliderInt("PCF radius", &t.pcfRadius, 0, 3);
            ImGui::SliderFloat("Depth bias", &t.shadowBias, 0.0f, 0.003f, "%.5f");
            ImGui::SliderFloat("Area half-size (m)", &t.shadowExtent, 6.0f, 40.0f, "%.0f");
            ImGui::Checkbox("Show shadow map", &t.showShadowMap);
        }
    }

    void skyUi()
    {
        FeatureToggles& t = m_toggles;
        if (ImGui::CollapsingHeader("Sky and light")) {
            ImGui::SliderFloat("Time of day", &t.timeOfDay, 0.0f, 24.0f, "%.1f h");
            ImGui::Checkbox("Day-night cycle", &t.dayNightCycle);
            ImGui::SliderFloat("Day length (s)", &t.dayLengthSeconds, 20.0f, 600.0f, "%.0f");
            ImGui::SliderFloat("Exposure", &t.exposure, 0.1f, 4.0f);
        }
    }

    // ---- Track A feature panel methods ----

    // ---- Track B feature panel methods ----

    void onKeyPressed(int key)
    {
        if (ImGui::GetIO().WantCaptureKeyboard)
            return;
        switch (key) {
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
            m_toggles.camera = static_cast<CameraMode>(key - GLFW_KEY_1);
            break;
        case GLFW_KEY_C:
            m_toggles.showCurve = !m_toggles.showCurve;
            break;
        case GLFW_KEY_SPACE:
            m_toggles.pauseBoat = !m_toggles.pauseBoat;
            break;
        case GLFW_KEY_P:
            m_screenshotRequested = true;
            break;
        default:
            break;
        }
    }

    static std::filesystem::path timestampedScreenshotPath()
    {
        const std::time_t now = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof(name), "shot_%Y%m%d_%H%M%S.png", std::localtime(&now));
        return std::filesystem::path(RESOURCE_ROOT "screenshots") / name;
    }

    void saveScreenshot(const std::filesystem::path& path)
    {
        const glm::ivec2 size = m_window.getFrameBufferSize();
        std::vector<unsigned char> pixels(static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
        // OpenGL's first row is the bottom of the image, PNG's is the top.
        stbi_flip_vertically_on_write(1);
        stbi_write_png(path.string().c_str(), size.x, size.y, 4, pixels.data(), size.x * 4);
        stbi_flip_vertically_on_write(0);
        std::cout << "Saved screenshot " << path.string() << std::endl;
    }

    // The window must be constructed first: it creates the OpenGL context everything else needs.
    Window m_window;
    LaunchOptions m_options;
    FeatureToggles m_toggles;

    Renderer m_renderer;
    Sky m_sky;
    Water m_water;
    Waves m_waves;
    Course m_course;
    Boat m_boat;
    Cameras m_cameras;
    Lighting m_lighting;

    // ---- Track A members (Island, ParticleSystem, ...) ----

    // ---- Track B members (if any) ----

    float m_time { 0.0f };
    float m_frameTime { 1.0f / 60.0f };
    bool m_screenshotRequested { false };
};

static LaunchOptions parseArguments(int argc, char** argv)
{
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--capture" && hasValue) {
            options.capturePath = argv[++i];
        } else if (arg == "--frames" && hasValue) {
            options.captureFrames = std::stoi(argv[++i]);
        } else if (arg == "--time" && hasValue) {
            options.timeOfDay = std::stof(argv[++i]);
        } else if (arg == "--wind" && hasValue) {
            options.windFromDeg = std::stof(argv[++i]);
        } else if (arg == "--set" && hasValue) {
            options.settings.emplace_back(argv[++i]);
        } else if (arg == "--camera" && hasValue) {
            const std::string name = argv[++i];
            if (name == "chase")
                options.camera = CameraMode::Chase;
            else if (name == "deck")
                options.camera = CameraMode::Deck;
            else if (name == "top")
                options.camera = CameraMode::Top;
            else if (name == "orbit")
                options.camera = CameraMode::Orbit;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << std::endl;
        }
    }
    return options;
}

int main(int argc, char** argv)
{
    try {
        Application app(parseArguments(argc, argv));
        app.run();
    } catch (const ShaderLoadingException& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}

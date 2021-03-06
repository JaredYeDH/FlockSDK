//
// Copyright (c) 2008-2017 Flock SDK developers & contributors. 
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Audio/Audio.h"
#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/EventProfiler.h"
#include "../Core/Platform.h"
#include "../Core/WorkQueue.h"
#include "../Engine/Console.h"
#include "../Engine/DebugHud.h"
#include "../Engine/Engine.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../Input/Input.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../IO/PackageFile.h"
#ifdef FLOCKSDK_IK
#include "../IK/IK.h"
#endif
#ifdef FLOCKSDK_NAVIGATION
#include "../Navigation/NavigationMesh.h"
#endif
#ifdef FLOCKSDK_NETWORK
#include "../Network/Network.h"
#endif
#include "../Database/Database.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/RaycastVehicle.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Localization.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include "../UI/UI.h"
#include "../2D/Urho2D.h"
#include "../Extensions/ProcSky/ProcSky.h" 

namespace FlockSDK
{

extern const char* logLevelPrefixes[];

Engine::Engine(Context* context) :
    Object(context),
    timeStep_(0.0f),
    timeStepSmoothing_(2),
    minFps_(10),
    maxFps_(200),
    maxInactiveFps_(60),
    pauseMinimized_(false),
    autoExit_(true),
    initialized_(false),
    exiting_(false),
    headless_(false),
    audioPaused_(false)
{
    // Register self as a subsystem
    context_->RegisterSubsystem(this);

    // Create subsystems which do not depend on engine initialization or startup parameters
    context_->RegisterSubsystem(new Time(context_));
    context_->RegisterSubsystem(new WorkQueue(context_));
#ifdef FLOCKSDK_PROFILING
    context_->RegisterSubsystem(new Profiler(context_));
#endif
    context_->RegisterSubsystem(new FileSystem(context_));
#ifdef FLOCKSDK_LOGGING
    context_->RegisterSubsystem(new Log(context_));
#endif
#ifdef FLOCKSDK_NETWORK
    context_->RegisterSubsystem(new Network(context_));
#endif
    context_->RegisterSubsystem(new ResourceCache(context_));
    context_->RegisterSubsystem(new Localization(context_));
    context_->RegisterSubsystem(new Database(context_));
    context_->RegisterSubsystem(new Input(context_));
    context_->RegisterSubsystem(new Audio(context_));
    context_->RegisterSubsystem(new UI(context_));

    // Register object factories for libraries which are not automatically registered along with subsystem creation
    RegisterSceneLibrary(context_);
    
#ifdef FLOCKSDK_IK
    RegisterIKLibrary(context_);
#endif

    RegisterPhysicsLibrary(context_);

#ifdef FLOCKSDK_NAVIGATION
    RegisterNavigationLibrary(context_);
#endif

    RegisterProcSkyLibrary(context_); 

    SubscribeToEvent(E_EXITREQUESTED, FLOCKSDK_HANDLER(Engine, HandleExitRequested));
}

Engine::~Engine()
{
}

bool Engine::Initialize(const VariantMap& parameters)
{
    if (initialized_)
        return true;

    FLOCKSDK_PROFILE(InitEngine);

    // Set headless mode
    headless_ = GetParameter(parameters, "Headless", false).GetBool();

    // Register the rest of the subsystems
    if (!headless_)
    {
        context_->RegisterSubsystem(new Graphics(context_));
        context_->RegisterSubsystem(new Renderer(context_));
    }
    else
    {
        // Register graphics library objects explicitly in headless mode to allow them to work without using actual GPU resources
        RegisterGraphicsLibrary(context_);
    }
    // 2D graphics library is dependent on 3D graphics library
    RegisterUrho2DLibrary(context_);

    // Start logging
    Log* log = GetSubsystem<Log>();
    if (log)
    {
        if (HasParameter(parameters, "LogLevel"))
            log->SetLevel(GetParameter(parameters, "LogLevel").GetInt());
        log->SetQuiet(GetParameter(parameters, "LogQuiet", false).GetBool());
        log->Open(GetParameter(parameters, "LogName", "Downpour.log").GetString());
    }

    // Set maximally accurate low res timer
    GetSubsystem<Time>()->SetTimerPeriod(1);

    // Configure max FPS
    if (GetParameter(parameters, "FrameLimiter", true) == false)
        SetMaxFps(0);

    // Set amount of worker threads according to the available physical CPU cores. Using also hyperthreaded cores results in
    // unpredictable extra synchronization overhead. Also reserve one core for the main thread
    unsigned numThreads = GetNumCPUCores() - 1;
    if (numThreads)
    {
        GetSubsystem<WorkQueue>()->CreateThreads(numThreads);

        FLOCKSDK_LOGINFOF("Created %u worker thread%s", numThreads, numThreads > 1 ? "s" : "");
    }

    // Add resource paths
    if (!InitializeResourceCache(parameters, false))
        return false;

    ResourceCache* cache = GetSubsystem<ResourceCache>();
    FileSystem* fileSystem = GetSubsystem<FileSystem>();

    Vector<String> resourcePrefixPaths = GetParameter(parameters, "ResourcePrefixPaths", String::EMPTY).GetString().Split(';', true);
    for (auto i = 0u; i < resourcePrefixPaths.Size(); ++i)
        resourcePrefixPaths[i] = AddTrailingSlash(
            IsAbsolutePath(resourcePrefixPaths[i]) ? resourcePrefixPaths[i] : fileSystem->GetProgramDir() + resourcePrefixPaths[i]);
    Vector<String> resourcePaths = GetParameter(parameters, "ResourcePaths", "pfiles").GetString().Split(';');
    Vector<String> resourcePackages = GetParameter(parameters, "ResourcePackages").GetString().Split(';');
    Vector<String> autoLoadPaths = GetParameter(parameters, "AutoloadPaths", "pfiles/early").GetString().Split(';');

    for (auto i = 0u; i < resourcePaths.Size(); ++i)
    {
        // If path is not absolute, prefer to add it as a package if possible
        if (!IsAbsolutePath(resourcePaths[i]))
        {
            unsigned j = 0;
            for (; j < resourcePrefixPaths.Size(); ++j)
            {
                String packageName = resourcePrefixPaths[j] + resourcePaths[i] + ".pak";
                if (fileSystem->FileExists(packageName))
                {
                    if (cache->AddPackageFile(packageName))
                        break;
                    else
                        return false;   // The root cause of the error should have already been logged
                }
                String pathName = resourcePrefixPaths[j] + resourcePaths[i];
                if (fileSystem->DirExists(pathName))
                {
                    if (cache->AddResourceDir(pathName))
                        break;
                    else
                        return false;
                }
            }
            if (j == resourcePrefixPaths.Size())
            {
                FLOCKSDK_LOGERRORF(
                    "Failed to add resource path '%s', check the documentation on how to set the 'resource prefix path'",
                    resourcePaths[i].CString());
                return false;
            }
        }
        else
        {
            String pathName = resourcePaths[i];
            if (fileSystem->DirExists(pathName))
                if (!cache->AddResourceDir(pathName))
                    return false;
        }
    }

    // Then add specified packages
    for (auto i = 0u; i < resourcePackages.Size(); ++i)
    {
        unsigned j = 0;
        for (; j < resourcePrefixPaths.Size(); ++j)
        {
            String packageName = resourcePrefixPaths[j] + resourcePackages[i];
            if (fileSystem->FileExists(packageName))
            {
                if (cache->AddPackageFile(packageName))
                    break;
                else
                    return false;
            }
        }
        if (j == resourcePrefixPaths.Size())
        {
            FLOCKSDK_LOGERRORF(
                "Failed to add resource package '%s', check the documentation on how to set the 'resource prefix path'",
                resourcePackages[i].CString());
            return false;
        }
    }

    // Add auto load folders. Prioritize these (if exist) before the default folders
    for (auto i = 0u; i < autoLoadPaths.Size(); ++i)
    {
        bool autoLoadPathExist = false;

        for (auto j = 0u; j < resourcePrefixPaths.Size(); ++j)
        {
            String autoLoadPath(autoLoadPaths[i]);
            if (!IsAbsolutePath(autoLoadPath))
                autoLoadPath = resourcePrefixPaths[j] + autoLoadPath;

            if (fileSystem->DirExists(autoLoadPath))
            {
                autoLoadPathExist = true;

                // Add all the subdirs (non-recursive) as resource directory
                Vector<String> subdirs;
                fileSystem->ScanDir(subdirs, autoLoadPath, "*", SCAN_DIRS, false);
                for (unsigned y = 0; y < subdirs.Size(); ++y)
                {
                    String dir = subdirs[y];
                    if (dir.StartsWith("."))
                        continue;

                    String autoResourceDir = autoLoadPath + "/" + dir;
                    if (!cache->AddResourceDir(autoResourceDir, 0))
                        return false;
                }

                // Add all the found package files (non-recursive)
                Vector<String> paks;
                fileSystem->ScanDir(paks, autoLoadPath, "*.pak", SCAN_FILES, false);
                for (unsigned y = 0; y < paks.Size(); ++y)
                {
                    String pak = paks[y];
                    if (pak.StartsWith("."))
                        continue;

                    String autoPackageName = autoLoadPath + "/" + pak;
                    if (!cache->AddPackageFile(autoPackageName, 0))
                        return false;
                }
            }
        }

        // The following debug message is confusing when user is not aware of the autoload feature
        // Especially because the autoload feature is enabled by default without user intervention
        // The following extra conditional check below is to suppress unnecessary debug log entry under such default situation
        // The cleaner approach is to not enable the autoload by default, i.e. do not use 'Autoload' as default value for 'AutoloadPaths' engine parameter
        // However, doing so will break the existing applications that rely on this
        if (!autoLoadPathExist && (autoLoadPaths.Size() > 1 || autoLoadPaths[0] != "pfiles/early"))
            FLOCKSDK_LOGDEBUGF(
                "Skipped autoload path '%s' as it does not exist, check the documentation on how to set the 'resource prefix path'",
                autoLoadPaths[i].CString());
    }

    // Initialize graphics & audio output
    if (!headless_)
    {
        Graphics* graphics = GetSubsystem<Graphics>();
        Renderer* renderer = GetSubsystem<Renderer>();

        if (HasParameter(parameters, "ExternalWindow"))
            graphics->SetExternalWindow(GetParameter(parameters, "ExternalWindow").GetVoidPtr());
        graphics->SetWindowTitle(GetParameter(parameters, "WindowTitle", "Downpour").GetString());
        graphics->SetWindowIcon(cache->GetResource<Image>(GetParameter(parameters, "WindowIcon", String::EMPTY).GetString()));
        graphics->SetFlushGPU(GetParameter(parameters, "FlushGPU", false).GetBool());
        graphics->SetOrientations(GetParameter(parameters, "Orientations", "LandscapeLeft LandscapeRight").GetString());

        if (HasParameter(parameters, "WindowPositionX") && HasParameter(parameters, "WindowPositionY"))
            graphics->SetWindowPosition(GetParameter(parameters, "WindowPositionX").GetInt(),
                GetParameter(parameters, "WindowPositionY").GetInt());

        if (HasParameter(parameters, "ForceGL2"))
            graphics->SetForceGL2(GetParameter(parameters, "ForceGL2").GetBool());

        if (!graphics->SetMode(
            GetParameter(parameters, "WindowWidth", 0).GetInt(),
            GetParameter(parameters, "WindowHeight", 0).GetInt(),
            GetParameter(parameters, "FullScreen", true).GetBool(),
            GetParameter(parameters, "Borderless", false).GetBool(),
            GetParameter(parameters, "WindowResizable", false).GetBool(),
            GetParameter(parameters, "HighDPI", false).GetBool(),
            GetParameter(parameters, "VSync", false).GetBool(),
            GetParameter(parameters, "TripleBuffer", false).GetBool(),
            GetParameter(parameters, "MultiSample", 1).GetInt(), 
            GetParameter(parameters, "Monitor", 0).GetInt(),
            GetParameter(parameters, "RefreshRate", 0).GetInt()
        ))
            return false;

        graphics->SetShaderCacheDir(GetParameter(parameters, "ShaderCacheDir", fileSystem->GetAppPreferencesDir("urho3d", "shadercache")).GetString());

        if (HasParameter(parameters, "DumpShaders"))
            graphics->BeginDumpShaders(GetParameter(parameters, "DumpShaders", String::EMPTY).GetString());
        if (HasParameter(parameters, "RenderPath"))
            renderer->SetDefaultRenderPath(cache->GetResource<XMLFile>(GetParameter(parameters, "RenderPath").GetString()));

        renderer->SetDrawShadows(GetParameter(parameters, "Shadows", true).GetBool());
        if (renderer->GetDrawShadows() && GetParameter(parameters, "LowQualityShadows", false).GetBool())
            renderer->SetShadowQuality(SHADOWQUALITY_SIMPLE_16BIT);
        renderer->SetMaterialQuality(GetParameter(parameters, "MaterialQuality", QUALITY_HIGH).GetInt());
        renderer->SetTextureQuality(GetParameter(parameters, "TextureQuality", QUALITY_HIGH).GetInt());
        renderer->SetTextureFilterMode((TextureFilterMode)GetParameter(parameters, "TextureFilterMode", FILTER_TRILINEAR).GetInt());
        renderer->SetTextureAnisotropy(GetParameter(parameters, "TextureAnisotropy", 4).GetInt());

        if (GetParameter(parameters, "Sound", true).GetBool())
        {
            GetSubsystem<Audio>()->SetMode(
                GetParameter(parameters, "SoundBuffer", 100).GetInt(),
                GetParameter(parameters, "SoundMixRate", 44100).GetInt(),
                GetParameter(parameters, "SoundStereo", true).GetBool(),
                GetParameter(parameters, "SoundInterpolation", true).GetBool()
            );
        }
    }

    // Init FPU state of main thread
    InitFPU();

// Initialize network
#ifdef FLOCKSDK_NETWORK
    if (HasParameter(parameters, "PackageCacheDir"))
        GetSubsystem<Network>()->SetPackageCacheDir(GetParameter(parameters, "PackageCacheDir").GetString());
#endif

#ifdef FLOCKSDK_PROFILING
    if (GetParameter(parameters, "EventProfiler", true).GetBool())
    {
        context_->RegisterSubsystem(new EventProfiler(context_));
        EventProfiler::SetActive(true);
    }
#endif
    frameTimer_.Reset();

    FLOCKSDK_LOGINFO("Initialized engine");
    initialized_ = true;
    return true;
}

bool Engine::InitializeResourceCache(const VariantMap& parameters, bool removeOld /*= true*/)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    FileSystem* fileSystem = GetSubsystem<FileSystem>();

    // Remove all resource paths and packages
    if (removeOld)
    {
        Vector<String> resourceDirs = cache->GetResourceDirs();
        Vector<SharedPtr<PackageFile>> packageFiles = cache->GetPackageFiles();
        for (auto i = 0u; i < resourceDirs.Size(); ++i)
            cache->RemoveResourceDir(resourceDirs[i]);
        for (auto i = 0u; i < packageFiles.Size(); ++i)
            cache->RemovePackageFile(packageFiles[i]);
    }

    // Add resource paths
    Vector<String> resourcePrefixPaths = GetParameter(parameters, "ResourcePrefixPaths", String::EMPTY).GetString().Split(';', true);
    for (auto i = 0u; i < resourcePrefixPaths.Size(); ++i)
        resourcePrefixPaths[i] = AddTrailingSlash(
            IsAbsolutePath(resourcePrefixPaths[i]) ? resourcePrefixPaths[i] : fileSystem->GetProgramDir() + resourcePrefixPaths[i]);
    Vector<String> resourcePaths = GetParameter(parameters, "ResourcePaths", "pfiles").GetString().Split(';');
    Vector<String> resourcePackages = GetParameter(parameters, "ResourcePackages").GetString().Split(';');
    Vector<String> autoLoadPaths = GetParameter(parameters, "AutoloadPaths", "pfiles/early").GetString().Split(';');

    for (auto i = 0u; i < resourcePaths.Size(); ++i)
    {
        // If path is not absolute, prefer to add it as a package if possible
        if (!IsAbsolutePath(resourcePaths[i]))
        {
            unsigned j = 0;
            for (; j < resourcePrefixPaths.Size(); ++j)
            {
                String packageName = resourcePrefixPaths[j] + resourcePaths[i] + ".pak";
                if (fileSystem->FileExists(packageName))
                {
                    if (cache->AddPackageFile(packageName))
                        break;
                    else
                        return false;   // The root cause of the error should have already been logged
                }
                String pathName = resourcePrefixPaths[j] + resourcePaths[i];
                if (fileSystem->DirExists(pathName))
                {
                    if (cache->AddResourceDir(pathName))
                        break;
                    else
                        return false;
                }
            }
            if (j == resourcePrefixPaths.Size())
            {
                FLOCKSDK_LOGERRORF(
                    "Failed to add resource path '%s', check the documentation on how to set the 'resource prefix path'",
                    resourcePaths[i].CString());
                return false;
            }
        }
        else
        {
            String pathName = resourcePaths[i];
            if (fileSystem->DirExists(pathName))
                if (!cache->AddResourceDir(pathName))
                    return false;
        }
    }

    // Then add specified packages
    for (auto i = 0u; i < resourcePackages.Size(); ++i)
    {
        unsigned j = 0;
        for (; j < resourcePrefixPaths.Size(); ++j)
        {
            String packageName = resourcePrefixPaths[j] + resourcePackages[i];
            if (fileSystem->FileExists(packageName))
            {
                if (cache->AddPackageFile(packageName))
                    break;
                else
                    return false;
            }
        }
        if (j == resourcePrefixPaths.Size())
        {
            FLOCKSDK_LOGERRORF(
                "Failed to add resource package '%s', check the documentation on how to set the 'resource prefix path'",
                resourcePackages[i].CString());
            return false;
        }
    }

    // Add auto load folders. Prioritize these (if exist) before the default folders
    for (auto i = 0u; i < autoLoadPaths.Size(); ++i)
    {
        bool autoLoadPathExist = false;

        for (auto j = 0u; j < resourcePrefixPaths.Size(); ++j)
        {
            String autoLoadPath(autoLoadPaths[i]);
            if (!IsAbsolutePath(autoLoadPath))
                autoLoadPath = resourcePrefixPaths[j] + autoLoadPath;

            if (fileSystem->DirExists(autoLoadPath))
            {
                autoLoadPathExist = true;

                // Add all the subdirs (non-recursive) as resource directory
                Vector<String> subdirs;
                fileSystem->ScanDir(subdirs, autoLoadPath, "*", SCAN_DIRS, false);
                for (unsigned y = 0; y < subdirs.Size(); ++y)
                {
                    String dir = subdirs[y];
                    if (dir.StartsWith("."))
                        continue;

                    String autoResourceDir = autoLoadPath + "/" + dir;
                    if (!cache->AddResourceDir(autoResourceDir, 0))
                        return false;
                }

                // Add all the found package files (non-recursive)
                Vector<String> paks;
                fileSystem->ScanDir(paks, autoLoadPath, "*.pak", SCAN_FILES, false);
                for (unsigned y = 0; y < paks.Size(); ++y)
                {
                    String pak = paks[y];
                    if (pak.StartsWith("."))
                        continue;

                    String autoPackageName = autoLoadPath + "/" + pak;
                    if (!cache->AddPackageFile(autoPackageName, 0))
                        return false;
                }
            }
        }

        // The following debug message is confusing when user is not aware of the autoload feature
        // Especially because the autoload feature is enabled by default without user intervention
        // The following extra conditional check below is to suppress unnecessary debug log entry under such default situation
        // The cleaner approach is to not enable the autoload by default, i.e. do not use 'Autoload' as default value for 'AutoloadPaths' engine parameter
        // However, doing so will break the existing applications that rely on this
        if (!autoLoadPathExist && (autoLoadPaths.Size() > 1 || autoLoadPaths[0] != "pfiles/early"))
            FLOCKSDK_LOGDEBUGF(
                "Skipped autoload path '%s' as it does not exist, check the documentation on how to set the 'resource prefix path'",
                autoLoadPaths[i].CString());
    }

    return true;
}

void Engine::RunFrame()
{
    assert(initialized_);

    // If not headless, and the graphics subsystem no longer has a window open, assume we should exit
    if (!headless_ && !GetSubsystem<Graphics>()->IsInitialized())
        exiting_ = true;

    if (exiting_)
        return;

    // Note: there is a minimal performance cost to looking up subsystems (uses a hashmap); if they would be looked up several
    // times per frame it would be better to cache the pointers
    Time* time = GetSubsystem<Time>();
    Input* input = GetSubsystem<Input>();
    Audio* audio = GetSubsystem<Audio>();

#ifdef FLOCKSDK_PROFILING
    if (EventProfiler::IsActive())
    {
        EventProfiler* eventProfiler = GetSubsystem<EventProfiler>();
        if (eventProfiler)
            eventProfiler->BeginFrame();
    }
#endif

    time->BeginFrame(timeStep_);

    // If pause when minimized -mode is in use, stop updates and audio as necessary
    if (pauseMinimized_ && input->IsMinimized())
    {
        if (audio->IsPlaying())
        {
            audio->Stop();
            audioPaused_ = true;
        }
    }
    else
    {
        // Only unpause when it was paused by the engine
        if (audioPaused_)
        {
            audio->Play();
            audioPaused_ = false;
        }

        Update();
    }

    Render();
    ApplyFrameLimit();

    time->EndFrame();
}

Console* Engine::CreateConsole()
{
    if (headless_ || !initialized_)
        return 0;

    // Return existing console if possible
    Console* console = GetSubsystem<Console>();
    if (!console)
    {
        console = new Console(context_);
        context_->RegisterSubsystem(console);
    }

    return console;
}

DebugHud* Engine::CreateDebugHud()
{
    if (headless_ || !initialized_)
        return 0;

    // Return existing debug HUD if possible
    DebugHud* debugHud = GetSubsystem<DebugHud>();
    if (!debugHud)
    {
        debugHud = new DebugHud(context_);
        context_->RegisterSubsystem(debugHud);
    }

    return debugHud;
}

void Engine::SetTimeStepSmoothing(int frames)
{
    timeStepSmoothing_ = (unsigned)Clamp(frames, 1, 20);
}

void Engine::SetMinFps(int fps)
{
    minFps_ = (unsigned)Max(fps, 0);
}

void Engine::SetMaxFps(int fps)
{
    maxFps_ = (unsigned)Max(fps, 0);
}

void Engine::SetMaxInactiveFps(int fps)
{
    maxInactiveFps_ = (unsigned)Max(fps, 0);
}

void Engine::SetPauseMinimized(bool enable)
{
    pauseMinimized_ = enable;
}

void Engine::SetAutoExit(bool enable)
{
    autoExit_ = enable;
}

void Engine::SetNextTimeStep(float seconds)
{
    timeStep_ = Max(seconds, 0.0f);
}

void Engine::Exit()
{
    DoExit();
}

void Engine::DumpProfiler()
{
#ifdef FLOCKSDK_LOGGING
    if (!Thread::IsMainThread())
        return;

    Profiler* profiler = GetSubsystem<Profiler>();
    if (profiler)
        FLOCKSDK_LOGRAW(profiler->PrintData(true, true) + "\n");
#endif
}

void Engine::DumpResources(bool dumpFileName)
{
#ifdef FLOCKSDK_LOGGING
    if (!Thread::IsMainThread())
        return;

    ResourceCache* cache = GetSubsystem<ResourceCache>();
    const HashMap<StringHash, ResourceGroup>& resourceGroups = cache->GetAllResources();
    if (dumpFileName)
    {
        FLOCKSDK_LOGRAW("Used resources:\n");
        for (HashMap<StringHash, ResourceGroup>::ConstIterator i = resourceGroups.Begin(); i != resourceGroups.End(); ++i)
        {
            const HashMap<StringHash, SharedPtr<Resource>>& resources = i->second_.resources_;
            if (dumpFileName)
            {
                for (HashMap<StringHash, SharedPtr<Resource>>::ConstIterator j = resources.Begin(); j != resources.End(); ++j)
                    FLOCKSDK_LOGRAW(j->second_->GetName() + "\n");
            }
        }
    }
    else
        FLOCKSDK_LOGRAW(cache->PrintMemoryUsage() + "\n");
#endif
}

void Engine::Update()
{
    FLOCKSDK_PROFILE(Update);

    // Logic update event
    using namespace Update;

    VariantMap& eventData = GetEventDataMap();
    eventData[P_TIMESTEP] = timeStep_;
    SendEvent(E_UPDATE, eventData);

    // Logic post-update event
    SendEvent(E_POSTUPDATE, eventData);

    // Rendering update event
    SendEvent(E_RENDERUPDATE, eventData);

    // Post-render update event
    SendEvent(E_POSTRENDERUPDATE, eventData);
}

void Engine::Render()
{
    if (headless_)
        return;

    FLOCKSDK_PROFILE(Render);

    // If device is lost, BeginFrame will fail and we skip rendering
    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics->BeginFrame())
        return;

    GetSubsystem<Renderer>()->Render();
    GetSubsystem<UI>()->Render();
    graphics->EndFrame();
}

void Engine::ApplyFrameLimit()
{
    if (!initialized_)
        return;

    unsigned maxFps = maxFps_;
    Input* input = GetSubsystem<Input>();
    if (input && !input->HasFocus())
        maxFps = Min(maxInactiveFps_, maxFps);

    long long elapsed = 0;
    if (maxFps)
    {
        FLOCKSDK_PROFILE(ApplyFrameLimit);

        long long targetMax = 1000000LL / maxFps;

        for (;;)
        {
            elapsed = frameTimer_.GetUSec(false);
            if (elapsed >= targetMax)
                break;

            // Sleep if 1 ms or more off the frame limiting goal
            if (targetMax - elapsed >= 1000LL)
            {
                unsigned sleepTime = (unsigned)((targetMax - elapsed) / 1000LL);
                Time::Sleep(sleepTime);
            }
        }
    }

    elapsed = frameTimer_.GetUSec(true);

    // If FPS lower than minimum, clamp elapsed time
    if (minFps_)
    {
        long long targetMin = 1000000LL / minFps_;
        if (elapsed > targetMin)
            elapsed = targetMin;
    }

    // Perform timestep smoothing
    timeStep_ = 0.0f;
    lastTimeSteps_.Push(elapsed / 1000000.0f);
    if (lastTimeSteps_.Size() > timeStepSmoothing_)
    {
        // If the smoothing configuration was changed, ensure correct amount of samples
        lastTimeSteps_.Erase(0, lastTimeSteps_.Size() - timeStepSmoothing_);
        for (auto i = 0u; i < lastTimeSteps_.Size(); ++i)
            timeStep_ += lastTimeSteps_[i];
        timeStep_ /= lastTimeSteps_.Size();
    }
    else
        timeStep_ = lastTimeSteps_.Back();
}

VariantMap Engine::ParseParameters(const Vector<String> &arguments)
{
    VariantMap ret;

    // Pre-initialize the parameters with environment variable values when they are set
    if (const char* paths = getenv("FLOCKSDK_PREFIX_PATH"))
        ret["ResourcePrefixPaths"] = paths;

    for (auto i = 0u; i < arguments.Size(); ++i)
    {
        if (arguments[i].Length() > 1 && arguments[i][0] == '-')
        {
            String argument = arguments[i].Substring(1).ToLower();
            String value = i + 1 < arguments.Size() ? arguments[i + 1] : String::EMPTY;

            if (argument == "headless")
                ret["Headless"] = true;
            else if (argument == "nolimit")
                ret["FrameLimiter"] = false;
            else if (argument == "flushgpu")
                ret["FlushGPU"] = true;
            else if (argument == "gl2")
                ret["ForceGL2"] = true;
            else if (argument == "landscape")
                ret["Orientations"] = "LandscapeLeft LandscapeRight " + ret["Orientations"].GetString();
            else if (argument == "portrait")
                ret["Orientations"] = "Portrait PortraitUpsideDown " + ret["Orientations"].GetString();
            else if (argument == "nosound")
                ret["Sound"] = false;
            else if (argument == "noip")
                ret["SoundInterpolation"] = false;
            else if (argument == "mono")
                ret["SoundStereo"] = false;
            else if (argument == "prepass")
                ret["RenderPath"] = "shaders/render/Prepass.xml";
            else if (argument == "deferred")
                ret["RenderPath"] = "shaders/render/Deferred.xml";
            else if (argument == "renderpath" && !value.Empty())
            {
                ret["RenderPath"] = value;
                ++i;
            }
            else if (argument == "noshadows")
                ret["Shadows"] = false;
            else if (argument == "lqshadows")
                ret["LowQualityShadows"] = true;
            else if (argument == "v")
                ret["VSync"] = true;
            else if (argument == "t")
                ret["TripleBuffer"] = true;
            else if (argument == "w")
                ret["FullScreen"] = false;
            else if (argument == "borderless")
                ret["Borderless"] = true;
            else if (argument == "s")
                ret["WindowResizable"] = true;
            else if (argument == "hd")
                ret["HighDPI"] = true;
            else if (argument == "q")
                ret["LogQuiet"] = true;
            else if (argument == "log" && !value.Empty())
            {
                unsigned logLevel = GetStringListIndex(value.CString(), logLevelPrefixes, M_MAX_UNSIGNED);
                if (logLevel != M_MAX_UNSIGNED)
                {
                    ret["LogLevel"] = logLevel;
                    ++i;
                }
            }
            else if (argument == "x" && !value.Empty())
            {
                ret["WindowWidth"] = ToInt(value);
                ++i;
            }
            else if (argument == "y" && !value.Empty())
            {
                ret["WindowHeight"] = ToInt(value);
                ++i;
            }
            else if (argument == "monitor" && !value.Empty()) {
                ret["Monitor"] = ToInt(value);
                ++i;
            }
            else if (argument == "hz" && !value.Empty()) {
                ret["RefreshRate"] = ToInt(value);
                ++i;
            }
            else if (argument == "m" && !value.Empty())
            {
                ret["MultiSample"] = ToInt(value);
                ++i;
            }
            else if (argument == "b" && !value.Empty())
            {
                ret["SoundBuffer"] = ToInt(value);
                ++i;
            }
            else if (argument == "r" && !value.Empty())
            {
                ret["SoundMixRate"] = ToInt(value);
                ++i;
            }
            else if (argument == "pp" && !value.Empty())
            {
                ret["ResourcePrefixPaths"] = value;
                ++i;
            }
            else if (argument == "p" && !value.Empty())
            {
                ret["ResourcePaths"] = value;
                ++i;
            }
            else if (argument == "pf" && !value.Empty())
            {
                ret["ResourcePackages"] = value;
                ++i;
            }
            else if (argument == "ap" && !value.Empty())
            {
                ret["AutoloadPaths"] = value;
                ++i;
            }
            else if (argument == "ds" && !value.Empty())
            {
                ret["DumpShaders"] = value;
                ++i;
            }
            else if (argument == "mq" && !value.Empty())
            {
                ret["MaterialQuality"] = ToInt(value);
                ++i;
            }
            else if (argument == "tq" && !value.Empty())
            {
                ret["TextureQuality"] = ToInt(value);
                ++i;
            }
            else if (argument == "tf" && !value.Empty())
            {
                ret["TextureFilterMode"] = ToInt(value);
                ++i;
            }
            else if (argument == "af" && !value.Empty())
            {
                ret["TextureFilterMode"] = FILTER_ANISOTROPIC;
                ret["TextureAnisotropy"] = ToInt(value);
                ++i;
            }
        }
    }

    return ret;
}

bool Engine::HasParameter(const VariantMap& parameters, const String &parameter)
{
    StringHash nameHash(parameter);
    return parameters.Find(nameHash) != parameters.End();
}

const Variant &Engine::GetParameter(const VariantMap& parameters, const String &parameter, const Variant &defaultValue)
{
    StringHash nameHash(parameter);
    VariantMap::ConstIterator i = parameters.Find(nameHash);
    return i != parameters.End() ? i->second_ : defaultValue;
}

void Engine::HandleExitRequested(StringHash eventType, VariantMap& eventData)
{
    if (autoExit_)
    {
        // Do not call Exit() here, as it contains mobile platform -specific tests to not exit.
        // If we do receive an exit request from the system on those platforms, we must comply
        DoExit();
    }
}

void Engine::DoExit()
{
    Graphics* graphics = GetSubsystem<Graphics>();
    if (graphics)
        graphics->Close();

    exiting_ = true;
}

}

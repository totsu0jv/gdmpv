#!/usr/bin/env python
import os
import sys

from methods import print_error, print_warning


libname = "godot-mpv"
projectdir = "demo"

localEnv = Environment(tools=["default"], PLATFORM="")

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

env.Append(CPPPATH=["src/"])

# Configure libmpv paths for different platforms
def configure_mpv_paths(env):
    platform = env["platform"]
    
    if platform == "linux":
        # Try common installation paths
        mpv_include_paths = [
            "/usr/include",
            "/usr/local/include",
        ]
        mpv_lib_paths = [
            "/usr/lib",
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib/i386-linux-gnu",
            "/usr/lib/aarch64-linux-gnu",
            "/usr/lib/arm-linux-gnueabihf",
            "/usr/local/lib",
        ]
        
        env.Append(CPPPATH=mpv_include_paths)
        env.Append(LIBPATH=mpv_lib_paths)
        env.Append(LIBS=["mpv"])
        
        print("Linux: Using system libmpv")
        
    elif platform == "windows":
        # Check for environment variables first (used in CI)
        mpv_include = os.environ.get("MPV_INCLUDE")
        mpv_lib = os.environ.get("MPV_LIB")
        
        if mpv_include and mpv_lib:
            print(f"Windows: Using MPV from environment variables")
            print(f"  Include: {mpv_include}")
            print(f"  Lib: {mpv_lib}")
            env.Append(CPPPATH=[mpv_include])
            env.Append(LIBPATH=[mpv_lib])
        else:
            # Try common installation paths
            possible_paths = [
                "C:/mpv-dev",
                "C:/libmpv",
                "C:/Program Files/mpv",
                os.path.expanduser("~/mpv-dev"),
            ]
            
            found = False
            for path in possible_paths:
                if os.path.exists(os.path.join(path, "include")):
                    print(f"Windows: Found libmpv at {path}")
                    env.Append(CPPPATH=[os.path.join(path, "include")])
                    env.Append(LIBPATH=[path])
                    found = True
                    break
            
            if not found:
                print_warning("""libmpv not found in standard locations.
Please download libmpv from:
https://sourceforge.net/projects/mpv-player-windows/files/libmpv/

Extract to C:/mpv-dev or set MPV_INCLUDE and MPV_LIB environment variables.
                """)
        
        env.Append(LIBS=["libmpv.dll.a"])
        
    elif platform == "macos":
        # Try Homebrew paths (both Intel and Apple Silicon)
        brew_paths = [
            "/opt/homebrew",  # Apple Silicon
            "/usr/local",     # Intel
        ]
        
        for brew_path in brew_paths:
            mpv_include = os.path.join(brew_path, "include")
            mpv_lib = os.path.join(brew_path, "lib")
            
            if os.path.exists(mpv_include):
                print(f"macOS: Using libmpv from {brew_path}")
                env.Append(CPPPATH=[mpv_include])
                env.Append(LIBPATH=[mpv_lib])
                break
        else:
            print_warning("""libmpv not found.
Please install via Homebrew:
    brew install mpv
            """)
        
        env.Append(LIBS=["mpv"])
        env.Append(LINKFLAGS=["-Wl,-rpath,@loader_path"])
        
    elif platform == "android":
        print_warning("Android support requires custom libmpv build. See documentation.")
        # Android would need a custom-built libmpv
        # You would need to build mpv for Android and add the paths here
        env.Append(LIBS=["mpv"])
        
    elif platform == "ios":
        print_warning("iOS support requires custom libmpv build. See documentation.")
        # iOS would need a custom-built libmpv
        env.Append(LIBS=["mpv"])
        
    elif platform == "web":
        print_warning("Web platform is not supported with native libmpv.")
        print_warning("Consider using HTML5 video element through JavaScript bridge instead.")
        # Web platform cannot use native libmpv
        # Would need to use Emscripten build of ffmpeg or HTML5 video

configure_mpv_paths(env)

sources = Glob("src/*.cpp")

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

suffix = env['suffix'].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

library = env.SharedLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=sources,
)

copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), library)

default_args = [library, copy]

# Copy MPV runtime dependencies on Windows
if env["platform"] == "windows":
    mpv_lib = os.environ.get("MPV_LIB")
    if mpv_lib:
        mpv_dll = os.path.join(mpv_lib, "libmpv-2.dll")
        if os.path.exists(mpv_dll):
            dll_copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), mpv_dll)
            default_args.append(dll_copy)
            print(f"Will copy libmpv-2.dll to demo/bin/windows/")

Default(*default_args)

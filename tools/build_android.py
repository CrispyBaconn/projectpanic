#!/usr/bin/env python3
"""Builds recompiled Panic Park for Android (APK) on Linux, WSL or Windows (no WSL needed).

    python3 tools/build_android.py            -> dist/android/PanicPark.apk
    python3 tools/build_android.py setup      only installs the tools (done automatically when missing)

The first time it downloads what it needs (about 3 GB), all in your user folder:
  Java 17 (Eclipse Temurin, only if no Java 17+ is installed) -> ~/.projectpanic/jdk
  Android command-line tools, SDK platform 35, build-tools, NDK  -> ~/android-sdk  (or ANDROID_HOME)
  SDL2 sources                                                   -> platform/SDL2-src
The Gradle project is created in ~/panicprk_android (on WSL keep it on the Linux disk: Gradle is slow on /mnt/c);
the sources stay in this folder and are compiled from here.
Variables: ANDROID_HOME, ABIS (default "arm64-v8a"; e.g. "arm64-v8a armeabi-v7a"), PROJ (project folder).
"""
import glob, json, os, platform, re, shutil, stat, subprocess, sys, tarfile, urllib.request, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIN = os.name == 'nt'
HOME = os.path.expanduser('~')
SDK = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or os.path.join(HOME, 'android-sdk')
PROJ = os.environ.get('PROJ') or os.path.join(HOME, 'panicprk_android')
JDK_DIR = os.path.join(HOME, '.projectpanic', 'jdk')
NDK_VER = '27.2.12479018'
CMDTOOLS = '11076708'
SDLV = '2.30.9'
ABIS = os.environ.get('ABIS', 'arm64-v8a').split()
APPID = 'com.recomp.panicpark'

def log(s): print(s, flush=True)

def download(url, dest):
    log(f'downloading {url}')
    req = urllib.request.Request(url, headers={'User-Agent': 'ProjectPanicBuilder'})
    with urllib.request.urlopen(req, timeout=120) as r, open(dest + '.part', 'wb') as f:
        size = int(r.headers.get('Content-Length') or 0); got = 0; last = -1
        while True:
            b = r.read(1 << 20)
            if not b: break
            f.write(b); got += len(b)
            if size and got * 10 // size != last: last = got * 10 // size; log(f'  {got >> 20} / {size >> 20} MB')
    os.replace(dest + '.part', dest)

def unzip(z, dest):
    """zipfile loses the exec bits: put them back (Linux) from the archive's Unix mode."""
    with zipfile.ZipFile(z) as f:
        for i in f.infolist():
            p = f.extract(i, dest)
            mode = i.external_attr >> 16
            if not WIN and mode & 0o111: os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

# ------------------------------------------------------------------------------------------------ Java
def java_major(java):
    try:
        out = subprocess.run([java, '-version'], capture_output=True, text=True).stderr
        m = re.search(r'version "(\d+)(?:\.(\d+))?', out)
        if not m: return 0
        v = int(m.group(1)); return int(m.group(2) or 0) if v == 1 else v
    except Exception: return 0

def find_java_home():
    """A JDK 17+ (Gradle 8 needs it): JAVA_HOME, then java in PATH, then the one we downloaded."""
    exe = 'java.exe' if WIN else 'java'
    cands = []
    if os.environ.get('JAVA_HOME'): cands.append(os.environ['JAVA_HOME'])
    j = shutil.which('java')
    if j: cands.append(os.path.dirname(os.path.dirname(os.path.realpath(j))))
    cands += sorted(glob.glob(os.path.join(JDK_DIR, '*')), reverse=True)
    for h in cands:
        java = os.path.join(h, 'bin', exe)
        if os.path.isfile(java) and java_major(java) >= 17 and os.path.isfile(os.path.join(h, 'bin', 'javac.exe' if WIN else 'javac')):
            return h
    return None

def get_java():
    h = find_java_home()
    if h: return h
    osn = 'windows' if WIN else ('mac' if sys.platform == 'darwin' else 'linux')
    arch = 'aarch64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
    url = f'https://api.adoptium.net/v3/binary/latest/17/ga/{osn}/{arch}/jdk/hotspot/normal/eclipse'
    os.makedirs(JDK_DIR, exist_ok=True)
    arc = os.path.join(JDK_DIR, 'jdk.zip' if WIN else 'jdk.tar.gz')
    download(url, arc)
    log('extracting Java...')
    if WIN: unzip(arc, JDK_DIR)
    else:
        with tarfile.open(arc) as t:
            try: t.extractall(JDK_DIR, filter='tar')
            except TypeError: t.extractall(JDK_DIR)
    os.remove(arc)
    h = find_java_home()
    if not h: sys.exit('ERROR: Java could not be installed')
    return h

# ------------------------------------------------------------------------------------------------ Android SDK
def sdkmanager():
    return os.path.join(SDK, 'cmdline-tools', 'latest', 'bin', 'sdkmanager.bat' if WIN else 'sdkmanager')

def setup(env):
    if not os.path.isfile(sdkmanager()):
        z = os.path.join(SDK, 'cmdtools.zip'); os.makedirs(SDK, exist_ok=True)
        download(f'https://dl.google.com/android/repository/commandlinetools-{"win" if WIN else "linux"}-{CMDTOOLS}_latest.zip', z)
        tmp = os.path.join(SDK, 'cmdtools_tmp'); shutil.rmtree(tmp, ignore_errors=True)
        unzip(z, tmp); os.remove(z)
        dst = os.path.join(SDK, 'cmdline-tools', 'latest'); shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.move(os.path.join(tmp, 'cmdline-tools'), dst)
        shutil.rmtree(tmp, ignore_errors=True)
    sm = sdkmanager()
    log('accepting the SDK licenses...')
    subprocess.run([sm, '--licenses'], input='y\n' * 100, text=True, env=env, capture_output=True, shell=WIN)
    log('installing the SDK packages (this takes a while)...')
    r = subprocess.run([sm, 'platform-tools', 'platforms;android-35', 'build-tools;35.0.0', f'ndk;{NDK_VER}'],
                       input='y\n' * 20, text=True, env=env, shell=WIN)
    if r.returncode: sys.exit('ERROR: sdkmanager failed')
    log('SETUP_OK')

def get_sdl():
    src = os.path.join(ROOT, 'platform', 'SDL2-src')
    if os.path.isdir(os.path.join(src, 'android-project')): return src
    deps = os.path.join(ROOT, 'deps'); os.makedirs(deps, exist_ok=True)
    tgz = os.path.join(deps, 'SDL2-src.tar.gz')
    download(f'https://github.com/libsdl-org/SDL/releases/download/release-{SDLV}/SDL2-{SDLV}.tar.gz', tgz)
    with tarfile.open(tgz) as t:
        try: t.extractall(deps, filter='data')
        except TypeError: t.extractall(deps)
    shutil.rmtree(src, ignore_errors=True); shutil.move(os.path.join(deps, f'SDL2-{SDLV}'), src)
    return src

# ------------------------------------------------------------------------------------------------ project
def edit(path, fn):
    s = open(path, encoding='utf-8').read(); n = fn(s)
    if n != s: open(path, 'w', encoding='utf-8', newline='').write(n)

def fwd(p): return p.replace('\\', '/')

def prepare(sdl):
    if not os.path.isfile(os.path.join(PROJ, 'gradlew')):
        log(f'creating the project in {PROJ}')
        shutil.copytree(os.path.join(sdl, 'android-project'), PROJ, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.gradle', 'build'))
        g = os.path.join(PROJ, 'gradlew')
        if not WIN: os.chmod(g, os.stat(g).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    # SDL inside the project: a link on Linux, a copy on Windows (links there need extra rights)
    jsdl = os.path.join(PROJ, 'app', 'jni', 'SDL')
    if os.path.islink(jsdl): os.unlink(jsdl)
    if WIN:
        if not os.path.isfile(os.path.join(jsdl, 'Android.mk')):
            shutil.rmtree(jsdl, ignore_errors=True); shutil.copytree(sdl, jsdl, ignore=shutil.ignore_patterns('android-project', 'test', 'VisualC*', 'Xcode*'))
    else:
        shutil.rmtree(jsdl, ignore_errors=True); os.symlink(sdl, jsdl)
    open(os.path.join(PROJ, 'local.properties'), 'w').write(f'sdk.dir={fwd(SDK)}\n')
    # launcher icon (build_icons/android, made by tools/make_icons.py)
    for d in glob.glob(os.path.join(ROOT, 'build_icons', 'android', 'mipmap-*')):
        out = os.path.join(PROJ, 'app', 'src', 'main', 'res', os.path.basename(d)); os.makedirs(out, exist_ok=True)
        shutil.copy(os.path.join(d, 'ic_launcher.png'), out)
    # app/build.gradle: app identifier, ABI, NDK
    def gradle(s):
        if 'applicationId' not in s: s = s.replace('minSdkVersion 21', f'applicationId "{APPID}"\n        minSdkVersion 21', 1)
        if 'ndkVersion' not in s: s = s.replace('compileSdkVersion 35', f'compileSdkVersion 35\n    ndkVersion "{NDK_VER}"', 1)
        return re.sub(r'abiFilters .*', 'abiFilters ' + ', '.join(f"'{a}'" for a in ABIS), s)
    edit(os.path.join(PROJ, 'app', 'build.gradle'), gradle)
    # our activity (asks for all files access for the public PanicPark folder) and permissions
    jd = os.path.join(PROJ, 'app', 'src', 'main', 'java', 'com', 'recomp', 'panicpark'); os.makedirs(jd, exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'platform', 'android', 'PanicParkActivity.java'), jd)
    def manifest(s):
        s = s.replace('android:name="SDLActivity"', 'android:name="com.recomp.panicpark.PanicParkActivity"')
        if 'MANAGE_EXTERNAL_STORAGE' not in s:
            v = '<uses-permission android:name="android.permission.VIBRATE" />'
            s = s.replace(v, v + ''.join(f'\n    <uses-permission android:name="android.permission.{p}" />'
                                         for p in ('READ_EXTERNAL_STORAGE', 'WRITE_EXTERNAL_STORAGE', 'MANAGE_EXTERNAL_STORAGE')), 1)
        if 'requestLegacyExternalStorage' not in s:
            s = s.replace('<application android:label="@string/app_name"',
                          '<application android:label="@string/app_name" android:requestLegacyExternalStorage="true"', 1)
        return s
    edit(os.path.join(PROJ, 'app', 'src', 'main', 'AndroidManifest.xml'), manifest)
    edit(os.path.join(PROJ, 'app', 'src', 'main', 'res', 'values', 'strings.xml'),       # display name
         lambda s: re.sub(r'<string name="app_name">.*?</string>', '<string name="app_name">Panic Park</string>', s))
    # jni: SDL + our "main" module (built from the sources in this folder)
    open(os.path.join(PROJ, 'app', 'jni', 'Application.mk'), 'w').write(
        f'APP_ABI := {" ".join(ABIS)}\nAPP_PLATFORM := android-21\nAPP_STL := c++_shared\nAPP_OPTIM := release\n')
    src = fwd(ROOT)
    if ' ' in src: log('WARNING: the project path contains spaces: ndk-build may fail. Move the folder to a path without spaces.')
    gen = sorted(fwd(os.path.relpath(f, ROOT)) for f in glob.glob(os.path.join(ROOT, 'gen_full', 'gen_*.cpp')))
    gen.append('gen_full/dispatch.cpp')
    open(os.path.join(PROJ, 'app', 'jni', 'src', 'Android.mk'), 'w').write(f'''LOCAL_PATH := {src}
include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_C_INCLUDES := {src} {src}/gen_full {src}/platform/SDL2-src/include
LOCAL_SRC_FILES := {' '.join(gen)} platform/platform_stub.cpp
LOCAL_CPPFLAGS := -std=c++17 -O2 -w -DUSE_SDL -DS23_POLL_TICK -fno-strict-aliasing
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid
include $(BUILD_SHARED_LIBRARY)
''')

README = '''Recompiled Panic Park - Android
1. Install PanicPark.apk (allow "apps from unknown sources").
2. Start it.
3. On the first launch grant "All files access" (the screen opens by itself), then go back to the app:
   the PanicPark folder is created in phone storage (next to Download, DCIM...).
4. Copy into PanicPark/: panicprk_main.elf, main_r4650_BE_4MB.bin, the panicprkrom/ folder and the lang/ and ui/ folders (menu background and logo, optional)
   (with the phone's file manager or from a PC over USB). Optional: dist/windows/assets as assets/.
   Without permission the private folder Android/data/com.recomp.panicpark/files/ works too.
Controls: drag the finger right/left = movement; short tap = Start/confirm;
top-left corner = coin; Back button = exit. A Bluetooth controller works like on PC.
'''

def main():
    os.chdir(ROOT)
    subprocess.run([sys.executable, os.path.join('tools', 'make_icons.py')])         # app icons (if you have icon.png)
    if not os.path.isdir(os.path.join(ROOT, 'gen_full')) and 'setup' not in sys.argv[1:]:
        sys.exit('ERROR: gen_full/ missing: generate the code first (mips2cpp.py, see README)')
    if '--prepare' in sys.argv[1:]:                     # only writes the Gradle project (for checking)
        prepare(get_sdl()); log('PREPARE_OK'); return
    jh = get_java(); log(f'Java: {jh}')
    env = dict(os.environ, JAVA_HOME=jh, ANDROID_HOME=SDK, ANDROID_SDK_ROOT=SDK)
    env['PATH'] = os.path.join(jh, 'bin') + os.pathsep + env.get('PATH', '')
    if 'setup' in sys.argv[1:] or not os.path.isdir(os.path.join(SDK, 'ndk', NDK_VER)): setup(env)
    if 'setup' in sys.argv[1:]: return
    prepare(get_sdl())
    gw = os.path.join(PROJ, 'gradlew.bat' if WIN else 'gradlew')
    # --info shows the ndk-build lines (one per compiled file), which the builder uses for the percentage
    r = subprocess.run([gw, '--no-daemon', '--console=plain', '--info', 'assembleDebug'], cwd=PROJ, env=env, shell=WIN)
    if r.returncode: sys.exit('ERROR: the Gradle build failed')
    out = os.path.join(ROOT, 'dist', 'android'); os.makedirs(out, exist_ok=True)
    shutil.copy(os.path.join(PROJ, 'app', 'build', 'outputs', 'apk', 'debug', 'app-debug.apk'), os.path.join(out, 'PanicPark.apk'))
    open(os.path.join(out, 'README.txt'), 'w', newline='\r\n' if WIN else '\n').write(README)
    log('BUILD_OK -> dist/android/PanicPark.apk')

if __name__ == '__main__':
    main()

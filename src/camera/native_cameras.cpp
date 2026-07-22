#include "camera/native_cameras.h"

#include "core/logging.h"

// ============================================================================
// Windows: DirectShow COM. Enumera la categoría de dispositivos de entrada de
// vídeo y lee FriendlyName/DevicePath de cada moniker. No llama a nada que abra
// el pin de captura, así que no dispara la negociación de formato que rompe a
// los drivers defectuosos. El orden de los monikers coincide con el índice que
// usa el backend CAP_DSHOW de OpenCV.
// ============================================================================
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dshow.h>

namespace pci::camera {

namespace {

std::string wideToUtf8(const wchar_t* wide) {
    if (wide == nullptr) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::string readProperty(IPropertyBag* bag, const wchar_t* name) {
    VARIANT var;
    VariantInit(&var);
    std::string value;
    if (SUCCEEDED(bag->Read(name, &var, nullptr)) && var.vt == VT_BSTR) {
        value = wideToUtf8(var.bstrVal);
    }
    VariantClear(&var);
    return value;
}

}  // namespace

std::vector<NativeCamera> enumerateNativeCameras() {
    std::vector<NativeCamera> cameras;

    // COM puede estar ya inicializado (por Qt) en otro modelo de apartment;
    // RPC_E_CHANGED_MODE es tolerable, la enumeración no exige uno concreto.
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool ownsCom = SUCCEEDED(initHr);  // S_FALSE/errores: no des-inicializar

    ICreateDevEnum* devEnum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, reinterpret_cast<void**>(&devEnum));
    if (SUCCEEDED(hr) && devEnum != nullptr) {
        IEnumMoniker* enumMoniker = nullptr;
        hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
        // S_FALSE (con enumMoniker nulo) significa "no hay cámaras".
        if (hr == S_OK && enumMoniker != nullptr) {
            IMoniker* moniker = nullptr;
            int index = 0;
            while (enumMoniker->Next(1, &moniker, nullptr) == S_OK) {
                IPropertyBag* bag = nullptr;
                if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                                     reinterpret_cast<void**>(&bag)))) {
                    NativeCamera cam;
                    cam.index = index;
                    cam.friendlyName = readProperty(bag, L"FriendlyName");
                    if (cam.friendlyName.empty()) {
                        cam.friendlyName = readProperty(bag, L"Description");
                    }
                    if (cam.friendlyName.empty()) {
                        cam.friendlyName = "Cámara " + std::to_string(index);
                    }
                    cam.devicePath = readProperty(bag, L"DevicePath");
                    cameras.push_back(std::move(cam));
                    bag->Release();
                }
                moniker->Release();
                ++index;
            }
            enumMoniker->Release();
        }
        devEnum->Release();
    } else {
        core::logWarning("DirectShow: no se pudo crear el enumerador de dispositivos");
    }

    if (ownsCom) {
        CoUninitialize();
    }
    return cameras;
}

}  // namespace pci::camera

// ============================================================================
// Linux: V4L2. Recorre /dev/video* y consulta VIDIOC_QUERYCAP. Abrir el nodo en
// O_RDONLY|O_NONBLOCK solo para preguntar sus capacidades NO inicia streaming ni
// negocia formato, así que es seguro. El número del nodo coincide con el índice
// del backend CAP_V4L2 de OpenCV.
// ============================================================================
#elif defined(__linux__)

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>

namespace pci::camera {

std::vector<NativeCamera> enumerateNativeCameras() {
    std::vector<NativeCamera> cameras;
    std::error_code ec;

    for (int i = 0; i < 64; ++i) {
        const std::string path = "/dev/video" + std::to_string(i);
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        v4l2_capability cap{};
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
            (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) != 0) {
            NativeCamera cam;
            cam.index = i;
            cam.friendlyName = reinterpret_cast<const char*>(cap.card);
            if (cam.friendlyName.empty()) {
                cam.friendlyName = "Cámara " + std::to_string(i);
            }
            cam.devicePath = path;
            cameras.push_back(std::move(cam));
        }
        ::close(fd);
    }
    return cameras;
}

}  // namespace pci::camera

// ============================================================================
// Otras plataformas (microcontroladores, etc.): sin enumeración nativa.
// ============================================================================
#else

namespace pci::camera {

std::vector<NativeCamera> enumerateNativeCameras() {
    return {};
}

}  // namespace pci::camera

#endif

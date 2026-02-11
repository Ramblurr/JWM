#include <jni.h>

#include <array>
#include <cstring>
#include <memory>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "Log.hh"
#include "WindowWayland.hh"
#include "impl/Library.hh"
#include "impl/RefCounted.hh"

namespace {
    constexpr int kRasterBufferCount = 3;

    static int _createShmFile(size_t size) {
        char name[] = "/tmp/jwm-wayland-raster-XXXXXX";
        int fd = mkstemp(name);
        if (fd < 0) {
            return -1;
        }
        unlink(name);
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    }
}

namespace jwm {
    class LayerRasterWayland: public RefCounted {
    public:
        struct RasterBuffer {
            wl_buffer* _wlBuffer = nullptr;
            uint8_t* _pixels = nullptr;
            size_t _byteCount = 0;
            int _fd = -1;
            bool _isBusy = false;
        };

        static void onBufferRelease(void* data, wl_buffer* wlBuffer) {
            LayerRasterWayland* instance = static_cast<LayerRasterWayland*>(data);
            for (RasterBuffer& buffer : instance->_buffers) {
                if (buffer._wlBuffer == wlBuffer) {
                    buffer._isBusy = false;
                    return;
                }
            }
        }

        LayerRasterWayland() = default;

        ~LayerRasterWayland() override {
            close();
        }

        void attach(WindowWayland* window) {
            if (_window != nullptr) {
                jwm::unref(&_window);
            }
            _window = jwm::ref(window);
        }

        void reconfigure() {
        }

        void resize(int width, int height) {
            if (width <= 0 || height <= 0) {
                _destroyBuffers();
                _destroyStaging();
                _width = 0;
                _height = 0;
                _rowBytes = 0;
                _byteCount = 0;
                return;
            }

            _destroyBuffers();
            _destroyStaging();

            _width = width;
            _height = height;
            _rowBytes = static_cast<size_t>(_width) * 4;
            _byteCount = _rowBytes * static_cast<size_t>(_height);

            _stagingPixels.reset(new uint8_t[_byteCount]);
            std::memset(_stagingPixels.get(), 0, _byteCount);

            if (!_createBuffers()) {
                _destroyBuffers();
                _destroyStaging();
                _width = 0;
                _height = 0;
                _rowBytes = 0;
                _byteCount = 0;
            }
        }

        const void* getPixelsPtr() const {
            return _stagingPixels.get();
        }

        int getRowBytes() const {
            return static_cast<int>(_rowBytes);
        }

        void swapBuffers() {
            if (_window == nullptr || _stagingPixels == nullptr || _byteCount == 0) {
                return;
            }
            if (!_window->isReadyForRasterPresent()) {
                return;
            }

            RasterBuffer* freeBuffer = nullptr;
            for (RasterBuffer& buffer : _buffers) {
                if (buffer._wlBuffer != nullptr && !buffer._isBusy) {
                    freeBuffer = &buffer;
                    break;
                }
            }
            if (freeBuffer == nullptr) {
                return;
            }

            std::memcpy(freeBuffer->_pixels, _stagingPixels.get(), _byteCount);
            if (_window->presentBuffer(freeBuffer->_wlBuffer, _width, _height)) {
                freeBuffer->_isBusy = true;
            }
        }

        void close() {
            _destroyBuffers();
            _destroyStaging();
            _width = 0;
            _height = 0;
            _rowBytes = 0;
            _byteCount = 0;

            if (_window != nullptr) {
                jwm::unref(&_window);
            }
        }

        bool _createBuffers() {
            if (_window == nullptr || _byteCount == 0) {
                return false;
            }

            wl_shm* shm = _window->getWindowManager().getShm();
            if (shm == nullptr) {
                return false;
            }

            static wl_buffer_listener kBufferListener {
                &LayerRasterWayland::onBufferRelease
            };

            for (int idx = 0; idx < kRasterBufferCount; ++idx) {
                int fd = _createShmFile(_byteCount);
                if (fd < 0) {
                    JWM_LOG("Wayland: failed to create raster shm file");
                    return false;
                }

                void* mapped = mmap(nullptr, _byteCount, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (mapped == MAP_FAILED) {
                    JWM_LOG("Wayland: mmap failed for raster buffer");
                    ::close(fd);
                    return false;
                }
                std::memset(mapped, 0, _byteCount);

                wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(_byteCount));
                if (pool == nullptr) {
                    JWM_LOG("Wayland: wl_shm_create_pool failed for raster buffer");
                    munmap(mapped, _byteCount);
                    ::close(fd);
                    return false;
                }

                wl_buffer* wlBuffer = wl_shm_pool_create_buffer(
                    pool,
                    0,
                    _width,
                    _height,
                    static_cast<int32_t>(_rowBytes),
                    WL_SHM_FORMAT_XRGB8888
                );
                wl_shm_pool_destroy(pool);
                if (wlBuffer == nullptr) {
                    JWM_LOG("Wayland: wl_shm_pool_create_buffer failed for raster buffer");
                    munmap(mapped, _byteCount);
                    ::close(fd);
                    return false;
                }

                if (wl_buffer_add_listener(wlBuffer, &kBufferListener, this) != 0) {
                    JWM_LOG("Wayland: wl_buffer_add_listener failed for raster buffer");
                    wl_buffer_destroy(wlBuffer);
                    munmap(mapped, _byteCount);
                    ::close(fd);
                    return false;
                }

                RasterBuffer& buffer = _buffers[idx];
                buffer._wlBuffer = wlBuffer;
                buffer._pixels = static_cast<uint8_t*>(mapped);
                buffer._byteCount = _byteCount;
                buffer._fd = fd;
                buffer._isBusy = false;
            }

            return true;
        }

        void _destroyBuffers() {
            for (RasterBuffer& buffer : _buffers) {
                if (buffer._wlBuffer != nullptr) {
                    wl_buffer_destroy(buffer._wlBuffer);
                    buffer._wlBuffer = nullptr;
                }
                if (buffer._pixels != nullptr) {
                    munmap(buffer._pixels, buffer._byteCount);
                    buffer._pixels = nullptr;
                }
                if (buffer._fd >= 0) {
                    ::close(buffer._fd);
                    buffer._fd = -1;
                }
                buffer._byteCount = 0;
                buffer._isBusy = false;
            }
        }

        void _destroyStaging() {
            _stagingPixels.reset();
        }

        WindowWayland* _window = nullptr;
        int _width = 0;
        int _height = 0;
        size_t _rowBytes = 0;
        size_t _byteCount = 0;
        std::unique_ptr<uint8_t[]> _stagingPixels;
        std::array<RasterBuffer, kRasterBufferCount> _buffers;
    };
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nMake
        (JNIEnv* env, jclass cls) {
    jwm::LayerRasterWayland* instance = new jwm::LayerRasterWayland();
    return reinterpret_cast<jlong>(instance);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nAttach
        (JNIEnv* env, jobject obj, jobject windowObj) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    jwm::WindowWayland* window = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, windowObj));
    instance->attach(window);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nReconfigure
        (JNIEnv* env, jobject obj) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->reconfigure();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nResize
        (JNIEnv* env, jobject obj, jint width, jint height) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->resize(width, height);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nSwapBuffers
        (JNIEnv* env, jobject obj) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->swapBuffers();
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nGetPixelsPtr
        (JNIEnv* env, jobject obj) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    return reinterpret_cast<jlong>(instance->getPixelsPtr());
}

extern "C" JNIEXPORT jint JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nGetRowBytes
        (JNIEnv* env, jobject obj) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    return static_cast<jint>(instance->getRowBytes());
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nClose
        (JNIEnv* env, jobject obj) {
    jwm::LayerRasterWayland* instance = reinterpret_cast<jwm::LayerRasterWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->close();
}

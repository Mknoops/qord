#include "WasapiLoopback.hpp"

#include <windows.h>

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace Qord {
namespace Audio {

namespace {

constexpr int SampleRate = 48000;
constexpr int Channels = 2;

std::string hresultText(const char *what, HRESULT hr)
{
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
    return buffer;
}

// ActivateAudioInterfaceAsync calls back on a worker thread, and wants an
// agile handler (IAgileObject): this one only stores the result and sets
// an event.
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    ActivationHandler() { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ActivationHandler()
    {
        if (client)
            client->Release();
        if (done)
            CloseHandle(done);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
        } else if (riid == __uuidof(IAgileObject)) {
            *out = static_cast<IAgileObject *>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --refs;
        if (left == 0)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *operation) override
    {
        HRESULT activateResult = E_FAIL;
        IUnknown *unknown = nullptr;
        result = operation->GetActivateResult(&activateResult, &unknown);
        if (SUCCEEDED(result))
            result = activateResult;
        if (SUCCEEDED(result) && unknown)
            result = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void **>(&client));
        if (unknown)
            unknown->Release();
        SetEvent(done);
        return S_OK;
    }

    HANDLE done = nullptr;
    HRESULT result = E_FAIL;
    IAudioClient *client = nullptr;

private:
    std::atomic<ULONG> refs { 1 };
};

} // namespace

struct WasapiLoopback::Private
{
    IAudioClient *client = nullptr;
    IAudioCaptureClient *capture = nullptr;
    HANDLE ready = nullptr;
    std::thread thread;
    std::atomic<bool> running { false };
    Callback callback;
    bool com = false; // on the thread that called start()

    void release()
    {
        if (capture)
            capture->Release();
        capture = nullptr;
        if (client)
            client->Release();
        client = nullptr;
        if (ready)
            CloseHandle(ready);
        ready = nullptr;
    }

    void run()
    {
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        std::vector<int16_t> silence;
        while (running) {
            if (WaitForSingleObject(ready, 100) != WAIT_OBJECT_0)
                continue;
            UINT32 packet = 0;
            while (running && SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    silence.assign(size_t(frames) * Channels, 0);
                    callback(silence.data(), frames);
                } else {
                    callback(reinterpret_cast<const int16_t *>(data), frames);
                }
                capture->ReleaseBuffer(frames);
            }
        }
        if (com)
            CoUninitialize();
    }
};

WasapiLoopback::WasapiLoopback() : d(std::make_unique<Private>()) {}

WasapiLoopback::~WasapiLoopback()
{
    stop();
}

bool WasapiLoopback::start(uint32_t processId, bool includeTree, Callback callback, std::string *error)
{
    stop();
    d->com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = message;
        d->release();
        if (d->com)
            CoUninitialize();
        d->com = false;
        return false;
    };

    AUDIOCLIENT_ACTIVATION_PARAMS params = {};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = processId;
    params.ProcessLoopbackParams.ProcessLoopbackMode = includeTree ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
                                                                   : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation = {};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

    auto *handler = new ActivationHandler;
    IActivateAudioInterfaceAsyncOperation *operation = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                             &activation, handler, &operation);
    if (FAILED(hr)) {
        handler->Release();
        // E_NOTIMPL / not found on Windows before 10 2004
        return fail(hresultText("Process loopback", hr));
    }
    WaitForSingleObject(handler->done, 5000);
    hr = handler->result;
    d->client = handler->client;
    handler->client = nullptr;
    handler->Release();
    if (operation)
        operation->Release();
    if (FAILED(hr) || !d->client)
        return fail(hresultText("Process loopback activation", hr));

    // Process loopback converts to whatever format we ask for
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = Channels;
    format.nSamplesPerSec = SampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    constexpr REFERENCE_TIME Buffer = 200000; // 20 ms, in 100 ns units
    hr = d->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                               AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                       AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                               Buffer, 0, &format, nullptr);
    if (FAILED(hr))
        return fail(hresultText("IAudioClient::Initialize", hr));
    d->ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!d->ready || FAILED(hr = d->client->SetEventHandle(d->ready)))
        return fail(hresultText("SetEventHandle", hr));
    hr = d->client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&d->capture));
    if (FAILED(hr))
        return fail(hresultText("GetService(IAudioCaptureClient)", hr));
    if (FAILED(hr = d->client->Start()))
        return fail(hresultText("IAudioClient::Start", hr));

    d->callback = std::move(callback);
    d->running = true;
    d->thread = std::thread([this]() { d->run(); });
    return true;
}

void WasapiLoopback::stop()
{
    d->running = false;
    if (d->thread.joinable())
        d->thread.join();
    if (d->client)
        d->client->Stop();
    d->release();
    if (d->com)
        CoUninitialize();
    d->com = false;
}

} // namespace Audio
} // namespace Qord

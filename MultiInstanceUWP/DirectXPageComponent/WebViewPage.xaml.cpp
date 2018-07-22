﻿//
// WebViewPage.xaml.cpp
// Implementation of the WebViewPage class
//

#include "pch.h"
#include "WebViewPage.xaml.h"
#include "common/DirectXHelper.h"
#include <string> 
#include <sstream> 
#include <algorithm>

using namespace DirectXPageComponent;

using namespace Concurrency;
using namespace Platform;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::UI::Xaml::Navigation;

// The Blank Page item template is documented at https://go.microsoft.com/fwlink/?LinkId=234238

WebViewPage::WebViewPage()
{
	InitializeComponent();
    m_deviceResources = std::make_shared<DX::DeviceResources>();
    m_transform = ref new BitmapTransform();
    m_webView = ref new WebView(WebViewExecutionMode::SeparateThread);
    m_webView->Source = ref new Windows::Foundation::Uri(L"https://www.microsoft.com");
    m_requestedWebViewWidth = 512;
    m_requestedWebViewHeight = 512;
    m_textureWidth = 512;
    m_textureHeight = 512;
    m_webView->Width = m_requestedWebViewWidth;
    m_webView->Height = m_requestedWebViewHeight;
    m_webView->Visibility = Windows::UI::Xaml::Visibility::Visible;
    m_webView->NavigationCompleted += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Xaml::Controls::WebView ^, Windows::UI::Xaml::Controls::WebViewNavigationCompletedEventArgs ^>(this, &WebViewPage::OnWebContentLoaded);
    mainGrid->Children->Append(m_webView);
}

void WebViewPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e)
{
    if (m_appServiceListener == nullptr)
    {
        m_appServiceListener = ref new AppServiceListener(L"WebView");
        auto connectTask = m_appServiceListener->ConnectToAppService(APPSERVICE_ID, Windows::ApplicationModel::Package::Current->Id->FamilyName);
        connectTask.then([this](AppServiceConnectionStatus response)
        {
            if (response == AppServiceConnectionStatus::Success)
            {
                auto listenerTask = m_appServiceListener->RegisterListener(this).then([this](AppServiceResponse^ response)
                {
                    if (response->Status == AppServiceResponseStatus::Success)
                    {
                        OutputDebugString(L"WebViewPage is connected to the App Service");
                    }
                });
            }
        });
    }
}

void WebViewPage::OnWebContentLoaded(Windows::UI::Xaml::Controls::WebView ^ webview, Windows::UI::Xaml::Controls::WebViewNavigationCompletedEventArgs ^ args)
{
    auto status = args->WebErrorStatus;
    auto success = args->IsSuccess;
    m_webView->Visibility = Windows::UI::Xaml::Visibility::Visible;

    OutputDebugString(L"OnWebContentLoaded");
    CreateDirectxTextures(ref new ValueSet());
    UpdateWebView();
}

void WebViewPage::UpdateWebView()
{
    m_timer.Tick([&]()
    {
        UpdateWebViewBitmap(m_requestedWebViewWidth, m_requestedWebViewHeight);
    });
}

task<void> WebViewPage::UpdateWebViewBitmap(unsigned int width, unsigned int height)
{
    InMemoryRandomAccessStream^ stream = ref new InMemoryRandomAccessStream();

    // capture the WebView
    return create_task(m_webView->CapturePreviewToStreamAsync(stream))
        .then([this, width, height, stream]()
    {
        return create_task(BitmapDecoder::CreateAsync(stream));
    }).then([width, height, this](BitmapDecoder^ decoder)
    {
        // Convert to a bitmap
        m_transform->ScaledHeight = height;
        m_transform->ScaledWidth = width;
        return create_task(decoder->GetPixelDataAsync(
            BitmapPixelFormat::Bgra8,
            BitmapAlphaMode::Straight,
            m_transform,
            ExifOrientationMode::RespectExifOrientation,
            ColorManagementMode::DoNotColorManage))
            .then([width, height, this](PixelDataProvider^ pixelDataProvider)
        {
            auto pixelData = pixelDataProvider->DetachPixelData();
            UpdateDirectxTextures(pixelData->Data, width, height);
        });
    }).then([this]()
    {
        UpdateWebView();
        std::wstringstream w;
        w << L" FPS:" << m_timer.GetFramesPerSecond() << std::endl;
        OutputDebugString(w.str().c_str());
    });
}

void WebViewPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
#if 0
    auto scripts = ref new Platform::Collections::Vector<Platform::String^>();
    Platform::String^ ScrollToTopString = L"window.scrollTo(0, 0); ";
    scripts->Append(ScrollToTopString);
    m_webView->InvokeScriptAsync("eval", scripts);
#endif

    if (m_appServiceListener == nullptr || !m_appServiceListener->IsConnected())
    {
        OutputDebugString(L"Not connected to AppService");
        return;
    }

    auto message = ref new ValueSet();
    message->Clear(); // using empty message for now
    message->Insert(L"Hello", L"Hello");

    m_appServiceListener->SendAppServiceMessage(L"DirectXPage", message).then([this](AppServiceResponse^ response)
    {
        auto responseMessage = response->Message;

        if (response->Status == AppServiceResponseStatus::Success)
        {
        }
        else
        {
        }
    });
}

ValueSet^ WebViewPage::OnRequestReceived(AppServiceConnection^ sender, AppServiceRequestReceivedEventArgs^ args)
{
    return ref new ValueSet();
}

void WebViewPage::CreateDirectxTextures(ValueSet^ info)
{
    m_stagingTexture.Reset();
    m_quadTexture.Reset();

    ID3D11Texture2D *pTexture = NULL;

    DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->OpenSharedResourceByName(L"DirectXPageHandleName", DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), (LPVOID*)&pTexture)
    );

    m_quadTexture = pTexture;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = m_textureWidth;
    desc.Height = m_textureHeight;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    pTexture = NULL;

    DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateTexture2D(&desc, nullptr, &pTexture)
    );

    m_stagingTexture = pTexture;
}

void WebViewPage::UpdateDirectxTextures(const void *buffer, int width, int height)
{
    if (m_quadTexture.Get() == nullptr || m_stagingTexture.Get() == nullptr)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    const auto context = m_deviceResources->GetD3DDeviceContext();

    DX::ThrowIfFailed(
        context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)
    );

    byte* data1 = (byte*)mapped.pData;
    byte* data2 = (byte*)buffer;
    unsigned int length = width * 4;
    for (int i = 0; i < height; ++i)
    {
        memcpy((void*)data1, (void*)data2, length);
        data1 += mapped.RowPitch;
        data2 += length;
    }

    context->Unmap(m_stagingTexture.Get(), 0);
    context->CopyResource(m_quadTexture.Get(), m_stagingTexture.Get());
}

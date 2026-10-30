#include "core.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
namespace {
ComPtr<ID3D11Device> sharedD3D;
ComPtr<ID2D1Device> sharedD2D;
ComPtr<IDXGIFactory2> sharedDXGI;
std::shared_ptr<orbit::IconAtlas> sharedIcons;
bool createSharedDevice(){
 if(sharedD3D&&sharedD2D&&sharedDXGI)return true;
 sharedDXGI.Reset();sharedD2D.Reset();sharedD3D.Reset();
 D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
 HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,3,D3D11_SDK_VERSION,&sharedD3D,nullptr,nullptr);
 if(FAILED(hr))hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,3,D3D11_SDK_VERSION,&sharedD3D,nullptr,nullptr);
 if(FAILED(hr))return false;
 ComPtr<IDXGIDevice1>device;sharedD3D.As(&device);device->SetMaximumFrameLatency(1);
 if(FAILED(D2D1CreateDevice(device.Get(),nullptr,&sharedD2D)))return false;
 ComPtr<IDXGIAdapter>adapter;device->GetAdapter(&adapter);
 return SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&sharedDXGI)));
}
}
std::shared_ptr<orbit::IconAtlas> orbit::systemIcons(){
 if(!createSharedDevice())return {};
 if(!sharedIcons){
  wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
  auto atlas=std::make_shared<IconAtlas>();
  if(!atlas->initialize(sharedD2D.Get(),fs::path(path).parent_path()/L"assets"/L"icons"))return {};
  sharedIcons=std::move(atlas);
 }
 return sharedIcons;
}
bool Paint::init(HWND window,ID2D1Factory*,IDWriteFactory*dwrite,IDWriteFontCollection*collection,float Dpi){
 hwnd=window;dw=dwrite;fonts=collection;dpi=Dpi;
 icons=orbit::systemIcons();if(!icons)return false;
 if(FAILED(sharedD2D->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context)))return false;
 context.As(&rt);
 RECT r;GetClientRect(hwnd,&r);DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=std::max(1L,r.right);desc.Height=std::max(1L,r.bottom);desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 if(FAILED(sharedDXGI->CreateSwapChainForHwnd(sharedD3D.Get(),hwnd,&desc,nullptr,nullptr,&swapChain))){rt.Reset();return false;}
 sharedDXGI->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
 rt->CreateSolidColorBrush(color(0xffffff),&brush);resize();rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);return true;
}
void Paint::begin(bool lightTheme){light=lightTheme;rt->BeginDraw();rt->SetTransform(D2D1::Matrix3x2F::Identity());}
void Paint::release(){if(context)context->SetTarget(nullptr);backBuffer.Reset();brush.Reset();rt.Reset();context.Reset();swapChain.Reset();icons.reset();}
float Paint::measure(std::wstring_view s,float size,bool mono){auto key=std::make_pair((int)(size*10)+(mono?10000:0),400);auto it=formats.find(key);if(it==formats.end())return (float)s.size()*size*.6f;ComPtr<IDWriteTextLayout>layout;if(FAILED(dw->CreateTextLayout(s.data(),(UINT32)s.size(),it->second.Get(),100000,30,&layout)))return 0;DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);return metrics.widthIncludingTrailingWhitespace;}
void Paint::end(){auto hr=rt->EndDraw();if(SUCCEEDED(hr)&&swapChain){RECT dirty;GetClientRect(hwnd,&dirty);DXGI_PRESENT_PARAMETERS params{1,&dirty,nullptr,nullptr};hr=swapChain->Present1(0,0,&params);}if(hr==D2DERR_RECREATE_TARGET||hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET){rt.Reset();brush.Reset();backBuffer.Reset();context.Reset();swapChain.Reset();sharedIcons.reset();icons.reset();sharedD2D.Reset();sharedD3D.Reset();sharedDXGI.Reset();InvalidateRect(hwnd,nullptr,FALSE);}}
void Paint::resize(){if(!rt||!swapChain)return;RECT r;GetClientRect(hwnd,&r);if(r.right<=0||r.bottom<=0)return;context->SetTarget(nullptr);backBuffer.Reset();auto hr=swapChain->ResizeBuffers(0,r.right,r.bottom,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))return;ComPtr<IDXGISurface>surface;swapChain->GetBuffer(0,IID_PPV_ARGS(&surface));auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),dpi,dpi);if(SUCCEEDED(context->CreateBitmapFromDxgiSurface(surface.Get(),&props,&backBuffer)))context->SetTarget(backBuffer.Get());context->SetDpi(dpi,dpi);}
void Paint::fill(D2D1_RECT_F r,unsigned c,float a){brush->SetColor(color(c,a));rt->FillRectangle(r,brush.Get());}
void Paint::round(D2D1_RECT_F r,unsigned c,float radius){brush->SetColor(color(c));rt->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get());}
void Paint::outline(D2D1_RECT_F r,unsigned c,float radius){brush->SetColor(color(c));rt->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get(),1);}
void Paint::line(float x,float y,float xx,float yy,unsigned c,float thickness){brush->SetColor(color(c));rt->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(xx,yy),brush.Get(),thickness);}
void Paint::dot(float x,float y,float radius,unsigned c){brush->SetColor(color(c));rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),radius,radius),brush.Get());}
void Paint::text(std::wstring_view s,D2D1_RECT_F r,unsigned c,float size,int weight,int alignment,bool mono){if(s.empty()||width(r)<=0||height(r)<=0)return;auto key=std::make_pair((int)(size*10)+(mono?10000:0),weight);auto i=formats.find(key);if(i==formats.end()){ComPtr<IDWriteTextFormat>f;dw->CreateTextFormat(mono?L"JetBrains Mono":L"DM Sans",fonts.Get(),(DWRITE_FONT_WEIGHT)weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f);if(!f)return;f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};ComPtr<IDWriteInlineObject>ellipsis;dw->CreateEllipsisTrimmingSign(f.Get(),&ellipsis);f->SetTrimming(&trimming,ellipsis.Get());i=formats.emplace(key,std::move(f)).first;}auto f=i->second.Get();f->SetTextAlignment((DWRITE_TEXT_ALIGNMENT)alignment);brush->SetColor(color(c));rt->DrawText(s.data(),(UINT32)s.size(),f,r,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP|D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);}
void Paint::logo(float x,float y,float size,unsigned c){auto old=D2D1::Matrix3x2F::Identity();rt->GetTransform(&old);auto mid=D2D1::Point2F(x+size/2,y+size/2);brush->SetColor(color(c));rt->DrawEllipse(D2D1::Ellipse(mid,size*.28f,size*.28f),brush.Get(),size*.065f);rt->SetTransform(D2D1::Matrix3x2F::Rotation(-40,mid)*old);rt->DrawEllipse(D2D1::Ellipse(mid,size*.56f,size*.18f),brush.Get(),size*.05f);rt->SetTransform(old);dot(x+size*.88f,y+size*.12f,size*.09f,c);}
void Paint::icon(Icon icon, float x, float y, float size, unsigned tint) {
 if (!icons) return;
 if (auto* bitmap = icons->bitmap(icon, size, dpi, light, tint))
  rt->DrawBitmap(bitmap, rect(x, y, size, size), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

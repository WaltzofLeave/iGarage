#include "core.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
namespace {
ComPtr<ID3D11Device> sharedD3D;
ComPtr<ID2D1Device> sharedD2D;
ComPtr<IDXGIFactory2> sharedDXGI;
bool createSharedDevice(){
 if(sharedD3D)return true;
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
bool Paint::init(HWND window,ID2D1Factory*,IDWriteFactory*dwrite,IDWriteFontCollection*collection,float Dpi){
 hwnd=window;dw=dwrite;fonts=collection;dpi=Dpi;
 if(!createSharedDevice())return false;
 if(FAILED(sharedD2D->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context)))return false;
 context.As(&rt);
 RECT r;GetClientRect(hwnd,&r);DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=std::max(1L,r.right);desc.Height=std::max(1L,r.bottom);desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 if(FAILED(sharedDXGI->CreateSwapChainForHwnd(sharedD3D.Get(),hwnd,&desc,nullptr,nullptr,&swapChain))){rt.Reset();return false;}
 sharedDXGI->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
 rt->CreateSolidColorBrush(color(0xffffff),&brush);resize();rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);return true;
}
void Paint::begin(){rt->BeginDraw();rt->SetTransform(D2D1::Matrix3x2F::Identity());}
void Paint::release(){if(context)context->SetTarget(nullptr);backBuffer.Reset();brush.Reset();rt.Reset();context.Reset();swapChain.Reset();}
float Paint::measure(std::wstring_view s,float size,bool mono){auto key=std::make_pair((int)(size*10)+(mono?10000:0),400);auto it=formats.find(key);if(it==formats.end())return (float)s.size()*size*.6f;ComPtr<IDWriteTextLayout>layout;if(FAILED(dw->CreateTextLayout(s.data(),(UINT32)s.size(),it->second.Get(),100000,30,&layout)))return 0;DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);return metrics.widthIncludingTrailingWhitespace;}
void Paint::end(){auto hr=rt->EndDraw();if(SUCCEEDED(hr)&&swapChain){RECT dirty;GetClientRect(hwnd,&dirty);DXGI_PRESENT_PARAMETERS params{1,&dirty,nullptr,nullptr};hr=swapChain->Present1(0,0,&params);}if(hr==D2DERR_RECREATE_TARGET||hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET){rt.Reset();brush.Reset();backBuffer.Reset();context.Reset();swapChain.Reset();sharedD2D.Reset();sharedD3D.Reset();sharedDXGI.Reset();InvalidateRect(hwnd,nullptr,FALSE);}}
void Paint::resize(){if(!rt||!swapChain)return;RECT r;GetClientRect(hwnd,&r);if(r.right<=0||r.bottom<=0)return;context->SetTarget(nullptr);backBuffer.Reset();auto hr=swapChain->ResizeBuffers(0,r.right,r.bottom,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))return;ComPtr<IDXGISurface>surface;swapChain->GetBuffer(0,IID_PPV_ARGS(&surface));auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),dpi,dpi);if(SUCCEEDED(context->CreateBitmapFromDxgiSurface(surface.Get(),&props,&backBuffer)))context->SetTarget(backBuffer.Get());context->SetDpi(dpi,dpi);}
void Paint::fill(D2D1_RECT_F r,unsigned c,float a){brush->SetColor(color(c,a));rt->FillRectangle(r,brush.Get());}
void Paint::round(D2D1_RECT_F r,unsigned c,float radius){brush->SetColor(color(c));rt->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get());}
void Paint::outline(D2D1_RECT_F r,unsigned c,float radius){brush->SetColor(color(c));rt->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get(),1);}
void Paint::line(float x,float y,float xx,float yy,unsigned c,float thickness){brush->SetColor(color(c));rt->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(xx,yy),brush.Get(),thickness);}
void Paint::dot(float x,float y,float radius,unsigned c){brush->SetColor(color(c));rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),radius,radius),brush.Get());}
void Paint::text(std::wstring_view s,D2D1_RECT_F r,unsigned c,float size,int weight,int alignment,bool mono){if(s.empty()||width(r)<=0||height(r)<=0)return;auto key=std::make_pair((int)(size*10)+(mono?10000:0),weight);auto i=formats.find(key);if(i==formats.end()){ComPtr<IDWriteTextFormat>f;dw->CreateTextFormat(mono?L"JetBrains Mono":L"DM Sans",fonts.Get(),(DWRITE_FONT_WEIGHT)weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f);if(!f)return;f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};ComPtr<IDWriteInlineObject>ellipsis;dw->CreateEllipsisTrimmingSign(f.Get(),&ellipsis);f->SetTrimming(&trimming,ellipsis.Get());i=formats.emplace(key,std::move(f)).first;}auto f=i->second.Get();f->SetTextAlignment((DWRITE_TEXT_ALIGNMENT)alignment);brush->SetColor(color(c));rt->DrawText(s.data(),(UINT32)s.size(),f,r,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP|D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);}
void Paint::logo(float x,float y,float size,unsigned c){auto old=D2D1::Matrix3x2F::Identity();rt->GetTransform(&old);auto mid=D2D1::Point2F(x+size/2,y+size/2);brush->SetColor(color(c));rt->DrawEllipse(D2D1::Ellipse(mid,size*.28f,size*.28f),brush.Get(),size*.065f);rt->SetTransform(D2D1::Matrix3x2F::Rotation(-40,mid)*old);rt->DrawEllipse(D2D1::Ellipse(mid,size*.56f,size*.18f),brush.Get(),size*.05f);rt->SetTransform(old);dot(x+size*.88f,y+size*.12f,size*.09f,c);}
void Paint::icon(Icon i,float x,float y,float s,unsigned c){
 auto p=[&](float a,float b){return D2D1::Point2F(x+a*s/24,y+b*s/24);};auto ln=[&](float a,float b,float d,float e){line(p(a,b).x,p(a,b).y,p(d,e).x,p(d,e).y,c,1.4f);};auto circle=[&](float a,float b,float r){brush->SetColor(color(c));rt->DrawEllipse(D2D1::Ellipse(p(a,b),r*s/24,r*s/24),brush.Get(),1.4f);};auto polygon=[&](std::initializer_list<D2D1_POINT_2F> points,bool filled){ComPtr<ID2D1Factory>factory;rt->GetFactory(&factory);ComPtr<ID2D1PathGeometry>g;factory->CreatePathGeometry(&g);ComPtr<ID2D1GeometrySink>sink;g->Open(&sink);auto a=points.begin();sink->BeginFigure(p(a->x,a->y),filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);for(++a;a!=points.end();++a)sink->AddLine(p(a->x,a->y));sink->EndFigure(filled?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);sink->Close();brush->SetColor(color(c));if(filled)rt->FillGeometry(g.Get(),brush.Get());else rt->DrawGeometry(g.Get(),brush.Get(),1.4f);};
 switch(i){
 case Icon::Folder:polygon({{3,7},{3,19},{21,19},{21,7},{12,7},{10,5},{3,5},{3,7}},false);break;
 case Icon::Search:circle(10.5f,10.5f,6.5f);ln(16,16,21,21);break;
 case Icon::Branch:circle(6,5,2);circle(6,19,2);circle(18,5,2);ln(6,7,6,17);polygon({{18,7},{18,10},{6,14},{6,17}},false);break;
 case Icon::Play:polygon({{7,4},{21,12},{7,20}},true);break;
 case Icon::Terminal:ln(4,6,10,12);ln(10,12,4,18);ln(13,18,20,18);break;
 case Icon::Chevron:ln(9,5,16,12);ln(16,12,9,19);break;
 case Icon::Down:ln(6,9,12,15);ln(12,15,18,9);break;
 case Icon::Close:ln(6,6,18,18);ln(6,18,18,6);break;
 case Icon::Plus:ln(12,4,12,20);ln(4,12,20,12);break;
 case Icon::More:for(int j=0;j<3;j++)dot(x+s*(5+j*7)/24,y+s/2,s/24,c);break;
 case Icon::File:polygon({{5,3},{14,3},{19,8},{19,21},{5,21},{5,3}},false);ln(14,3,14,9);ln(14,9,19,9);break;
 case Icon::Check:ln(5,12,9,16);ln(9,16,19,6);break;
 case Icon::Stop:round(rect(x+s*.25f,y+s*.25f,s*.5f,s*.5f),c,1);break;
 case Icon::Refresh:polygon({{20,10},{18,5},{13,3},{7,4},{3,9},{3,15},{7,20},{13,21},{19,18}},false);ln(20,3,20,10);ln(20,10,13,10);break;
 case Icon::Structure:for(auto q:std::vector<D2D1_POINT_2F>{{4,4},{4,15},{14,15}}){outline(rect(x+q.x*s/24,y+q.y*s/24,s*6/24,s*5/24),c,1);}ln(7,9,7,15);ln(7,12,17,12);ln(17,12,17,15);break;
 case Icon::Layout:outline(rect(x+s*.125f,y+s*.125f,s*.75f,s*.75f),c,2);ln(8,3,8,21);ln(8,16,21,16);break;
 case Icon::Book:polygon({{3,4},{10,4},{12,6},{14,4},{21,4},{21,19},{14,19},{12,21},{10,19},{3,19},{3,4}},false);ln(12,6,12,21);break;
 case Icon::Trash:ln(3,6,21,6);polygon({{9,6},{9,3},{15,3},{15,6}},false);polygon({{5,6},{6,21},{18,21},{19,6}},false);ln(10,10,10,17);ln(14,10,14,17);break;
 case Icon::Flask:ln(9,3,15,3);polygon({{10,3},{10,10},{4,20},{20,20},{14,10},{14,3}},false);ln(7,15,17,15);break;
 case Icon::Clock:circle(12,12,9);ln(12,7,12,12);ln(12,12,15,14);break;
 case Icon::Help:circle(12,12,9);text(L"?",rect(x,y,s,s),c,s*.7f,500,2);break;
 case Icon::Settings:circle(12,12,4);circle(12,12,9);for(int j=0;j<8;j++){float a=j*3.1415926f/4;ln(12+8*cosf(a),12+8*sinf(a),12+11*cosf(a),12+11*sinf(a));}break;
 case Icon::Maximize:ln(8,3,3,3);ln(3,3,3,8);ln(16,3,21,3);ln(21,3,21,8);ln(3,16,3,21);ln(3,21,8,21);ln(21,16,21,21);ln(21,21,16,21);break;
 case Icon::Minimize:ln(5,12,19,12);break;
 case Icon::Restore:outline(rect(x+s*.25f,y+s*.3f,s*.45f,s*.45f),c,0);ln(9,5,19,5);ln(19,5,19,15);break;
 case Icon::Upload:case Icon::Download:ln(12,3,12,16);if(i==Icon::Download){ln(7,11,12,16);ln(12,16,17,11);}else{ln(7,8,12,3);ln(12,3,17,8);}polygon({{4,16},{4,21},{20,21},{20,16}},false);break;
 default:circle(12,12,8);break;
 }
}

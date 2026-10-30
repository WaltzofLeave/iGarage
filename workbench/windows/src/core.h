#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <memory>
#include <future>
#include <cmath>
#include "../vendor/json.hpp"
#include "../vendor/scintilla/include/Scintilla.h"
#include "../vendor/lexilla/include/SciLexer.h"
#include "editor/document.h"
namespace fs=std::filesystem;
using json=nlohmann::json;
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
inline std::wstring wide(std::string_view s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n);return r;}
inline std::string utf8(std::wstring_view s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;}
inline std::string readFile(const fs::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("无法读取文件："+utf8(p.wstring()));std::string s((std::istreambuf_iterator<char>(f)),{});if(s.starts_with("\xef\xbb\xbf"))s.erase(0,3);return s;}
inline void atomicWrite(const fs::path&p,std::string_view s){fs::create_directories(p.parent_path());auto tmp=p;tmp+=L".orbit-tmp";{std::ofstream f(tmp,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("无法保存文件");f.write(s.data(),s.size());f.flush();if(!f)throw std::runtime_error("磁盘写入失败");}if(!MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法替换目标文件");}
inline std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==s.npos?"":s.substr(a,b-a+1);}
inline std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)tolower(c);});return s;}
inline std::string baseName(const std::string&p){auto i=p.find_last_of('/');return i==p.npos?p:p.substr(i+1);}
inline std::string extension(const std::string&p){auto b=baseName(p);auto i=b.find_last_of('.');return i==b.npos?"":lower(b.substr(i+1));}
inline bool validPath(const std::string&p){if(p.empty()||p.size()>240||p.front()=='/'||p.find_first_of("\\:*?\"<>|\r\n")!=p.npos)return false;std::stringstream ss(p);std::string s;while(std::getline(ss,s,'/')){if(s.empty()||s=="."||s==".."||s.ends_with(' ')||s.ends_with('.'))return false;auto n=lower(s.substr(0,s.find('.')));if(n=="con"||n=="prn"||n=="aux"||n=="nul"||n=="com1"||n=="lpt1")return false;}return !p.ends_with('/');}
inline std::wstring quoteArg(std::wstring_view a){std::wstring r=L"\"";size_t slash=0;for(wchar_t c:a){if(c==L'\\'){slash++;continue;}if(c==L'\"')r.append(slash*2+1,L'\\');else r.append(slash,L'\\');slash=0;r+=c;}r.append(slash*2,L'\\');return r+L'\"';}
inline bool inside(const fs::path&root,const fs::path&file){auto r=fs::weakly_canonical(root),p=fs::weakly_canonical(file);auto i=r.begin(),j=p.begin();for(;i!=r.end();++i,++j)if(j==p.end()||_wcsicmp(i->c_str(),j->c_str())!=0)return false;return true;}
inline D2D1_RECT_F rect(float x,float y,float w,float h){return D2D1::RectF(x,y,x+w,y+h);}
inline bool contains(const D2D1_RECT_F&r,float x,float y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
inline float width(const D2D1_RECT_F&r){return r.right-r.left;}inline float height(const D2D1_RECT_F&r){return r.bottom-r.top;}
inline D2D1_COLOR_F color(unsigned rgb,float alpha=1){return D2D1::ColorF(rgb,alpha);}
inline COLORREF rgb(unsigned c){return RGB((c>>16)&255,(c>>8)&255,c&255);}
struct Theme{
  unsigned bg=0x1e1f22,panel=0x25262a,top=0x292a2e,hover=0x34353a,line=0x35363b,text=0xc7c8cc,muted=0x7f838d,accent=0xe6ad85,green=0x88bd91,red=0xe58080,blue=0x89b2e1,purple=0xbd9fdf,tabs=0x222327,selected=0x39414b;
  static Theme light(){Theme t;t.bg=0xfaf9f7;t.panel=0xf0efec;t.top=0xe7e6e3;t.hover=0xe1dfdb;t.line=0xd9d7d3;t.text=0x41454c;t.muted=0x8a8b8d;t.accent=0xb87542;t.green=0x4a805c;t.red=0xbd5259;t.blue=0x426b9c;t.purple=0x8860a9;t.tabs=0xeae9e6;t.selected=0xe0e4ea;return t;}
};
struct Settings{int font=13,tab=4;bool light=false,wrap=false;std::string stdinText;std::wstring interpreter=L"python";};
struct Layout{
 float w=1440,h=900,title=57,menu=34,leftRail=48,rightRail=37,sidebar=238,structure=208,bottom=226,status=27;
 D2D1_RECT_F titleRect{},menuRect{},side{},editor{},tabs{},crumb{},code{},bottomRect{},bottomTabs{},structureRect{},statusRect{};
 void update(float W,float H,bool showSide,bool showStructure,bool showBottom,bool focus){w=W;h=H;title=W<680?50:W>=1600?60:57;menu=W<680?31:W>=1600?36:34;leftRail=W<680?37:48;rightRail=W<680?30:37;float sideW=showSide&&!focus?std::min(sidebar,std::max(175.f,W*.28f)):0;float structW=showStructure&&!focus&&W>950?structure:0;float x=leftRail+sideW+(sideW?1:0),right=W-rightRail-structW;float top=title+menu;float bot=showBottom?std::min(bottom,std::max(110.f,H*.65f)):0;float tabH=W<680?37:W>=1600?44:42;titleRect=rect(0,0,W,title);menuRect=rect(0,title,W,menu);side=rect(leftRail,top,sideW,H-top-status);editor=rect(x,top,std::max(100.f,right-x),H-top-status);tabs=rect(x,top,width(editor),tabH);crumb=rect(x,top+tabH,width(editor),32);code=rect(x,crumb.bottom,width(editor),std::max(40.f,H-status-bot-crumb.bottom));bottomRect=rect(x,H-status-bot,width(editor),bot);bottomTabs=rect(x,bottomRect.top,width(editor),38);structureRect=rect(right,top,structW,H-top-status);statusRect=rect(0,H-status,W,status);}
};
class WorkQueue{
 std::mutex mutex;std::condition_variable cv;std::deque<std::function<void()>> tasks;bool stop=false;std::thread thread;
public:
 WorkQueue():thread([this]{for(;;){std::function<void()>f;{std::unique_lock l(mutex);cv.wait(l,[this]{return stop||!tasks.empty();});if(stop&&tasks.empty())return;f=std::move(tasks.front());tasks.pop_front();}try{f();}catch(...){}}}){}
 ~WorkQueue(){{std::lock_guard l(mutex);stop=true;}cv.notify_one();if(thread.joinable())thread.join();}
 void push(std::function<void()>f){{std::lock_guard l(mutex);tasks.push_back(std::move(f));}cv.notify_one();}
 void drain(){auto p=std::make_shared<std::promise<void>>();auto f=p->get_future();push([p]{p->set_value();});f.wait();}
};
struct SearchHit{std::string path,text;int line=0;};
struct FileEvent{std::string path,text,error;uint64_t revision=0;bool load=false;};
constexpr UINT WM_ORBIT_FILE=WM_APP+1,WM_ORBIT_OUTPUT=WM_APP+2,WM_ORBIT_DONE=WM_APP+3,WM_ORBIT_SEARCH=WM_APP+4;
struct RunResult{DWORD code=0;double seconds=0;std::string error;};
struct SearchResult{int generation=0;std::vector<SearchHit>hits;};
enum class Icon{Folder,Search,Branch,Play,Terminal,Settings,Chevron,Down,Close,Plus,More,File,Check,Stop,Refresh,Layout,Structure,Download,Upload,Help,Maximize,Trash,Flask,Clock,Book,Sun,Save,Minimize,Restore};
struct Hit{D2D1_RECT_F box;std::string action,data;int index=0;std::wstring tip;};
struct Paint{
 ComPtr<ID2D1RenderTarget>rt;ComPtr<ID2D1DeviceContext>context;ComPtr<IDXGISwapChain1>swapChain;ComPtr<ID2D1Bitmap1>backBuffer;ComPtr<ID2D1SolidColorBrush>brush;ComPtr<IDWriteFactory>dw;ComPtr<IDWriteFontCollection>fonts;std::map<std::pair<int,int>,ComPtr<IDWriteTextFormat>>formats;
 HWND hwnd=nullptr;float dpi=96;
 float measure(std::wstring_view s,float size=9,bool mono=true);
 bool init(HWND window,ID2D1Factory*d2d,IDWriteFactory*dwrite,IDWriteFontCollection*collection,float Dpi);
 void begin();void end();void resize();void release();void fill(D2D1_RECT_F r,unsigned c,float a=1);void round(D2D1_RECT_F r,unsigned c,float radius=4);void line(float x,float y,float xx,float yy,unsigned c,float thickness=1);void dot(float x,float y,float radius,unsigned c);void outline(D2D1_RECT_F r,unsigned c,float radius=4);void text(std::wstring_view s,D2D1_RECT_F r,unsigned c,float size=12,int weight=400,int alignment=0,bool mono=false);void icon(Icon i,float x,float y,float size,unsigned c);void logo(float x,float y,float size,unsigned c);
};

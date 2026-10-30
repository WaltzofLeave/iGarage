#include "app.h"
#include "../vendor/scintilla/include/ILexer.h"
#include "../vendor/lexilla/include/Lexilla.h"
#include <uxtheme.h>
#include <iomanip>
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"uxtheme.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
#pragma comment(lib,"imm32.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"psapi.lib")
static App* appInstance=nullptr;
static LRESULT CALLBACK DimmerProc(HWND h,UINT m,WPARAM w,LPARAM l){auto a=(App*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);}if(m==WM_ERASEBKGND)return 1;if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,(HBRUSH)GetStockObject(BLACK_BRUSH));EndPaint(h,&ps);return 0;}if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_LBUTTONUP&&a){a->closePopup();return 0;}return DefWindowProcW(h,m,w,l);}
static constexpr wchar_t mainClass[]=L"Orbit.Native.Window",popupClass[]=L"Orbit.Native.Popup",menuClass[]=L"Orbit.Native.Menu";
bool App::init(HINSTANCE h){
 instance=h;appInstance=this;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);exeDir=fs::path(path).parent_path();dataDir=exeDir/L"data";fs::create_directories(dataDir);root=exeDir/L"workspace";
 root=exeDir/L"orbit-python";loadSession();theme=settings.light?Theme::light():Theme{};
 if(!fs::exists(root)){fs::create_directories(root);fs::path sample=exeDir/L"sample-project";if(fs::exists(sample))fs::copy(sample,root,fs::copy_options::recursive|fs::copy_options::skip_existing);}
 if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf())))return false;
 if(!orbit::systemIcons())return false;
 if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))return false;
 ComPtr<IDWriteFactory5>f5;if(SUCCEEDED(dw.As(&f5))){ComPtr<IDWriteFontSetBuilder1>b;if(SUCCEEDED(f5->CreateFontSetBuilder(&b))){for(auto name:{L"DMSans.ttf",L"JetBrainsMono.ttf"}){ComPtr<IDWriteFontFile>file;auto fontPath=exeDir/L"assets"/name;if(SUCCEEDED(dw->CreateFontFileReference(fontPath.c_str(),nullptr,&file)))b->AddFontFile(file.Get());}ComPtr<IDWriteFontSet>set;ComPtr<IDWriteFontCollection1>col;if(SUCCEEDED(b->CreateFontSet(&set))&&SUCCEEDED(f5->CreateFontCollectionFromFontSet(set.Get(),&col)))col.As(&fontCollection);}}
 Scintilla_RegisterClasses(h);INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&ic);
 WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_DBLCLKS;wc.lpfnWndProc=WndProc;wc.hInstance=h;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=(HICON)LoadImageW(h,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE);wc.hIconSm=wc.hIcon;wc.lpszClassName=mainClass;RegisterClassExW(&wc);wc.lpfnWndProc=PopupProc;wc.style=CS_DROPSHADOW;wc.lpszClassName=popupClass;RegisterClassExW(&wc);wc.lpfnWndProc=MenuProc;wc.lpszClassName=menuClass;RegisterClassExW(&wc);wc.lpfnWndProc=DimmerProc;wc.style=0;wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"Orbit.Native.Dimmer";RegisterClassExW(&wc);
 UINT screenDpi=GetDpiForSystem();int W=MulDiv(1400,screenDpi,96),H=MulDiv(900,screenDpi,96);RECT wa;SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0);W=std::min(W,(int)(wa.right-wa.left-40));H=std::min(H,(int)(wa.bottom-wa.top-40));
 window=CreateWindowExW(WS_EX_APPWINDOW,mainClass,L"Orbit IDE — Native Python Workspace",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,(wa.right-W)/2,(wa.bottom-H)/2,W,H,nullptr,nullptr,h,this);if(!window)return false;
 DWORD border=rgb(theme.top);DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));MARGINS margins{1,1,1,1};DwmExtendFrameIntoClientArea(window,&margins);
 SetWindowPos(window,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return true;
}
void App::setEditFont(HWND h,float size,bool mono){HFONT f=CreateFontW(-px(size),0,0,0,400,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,mono?L"Consolas":L"Microsoft YaHei UI");ownedFonts.push_back(f);SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);}
void App::setCue(HWND h,const wchar_t*cue){SendMessageW(h,EM_SETCUEBANNER,TRUE,(LPARAM)cue);}
void App::createControls(){
 dpi=(float)GetDpiForWindow(window);scale=dpi/96;
 editor=CreateWindowExW(0,L"Scintilla",L"代码编辑器",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,100,100,window,(HMENU)100,instance,nullptr);
 output=CreateWindowExW(0,L"Scintilla",L"运行输出",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,100,100,window,(HMENU)101,instance,nullptr);
 termOutput=CreateWindowExW(0,L"Scintilla",L"项目终端输出",WS_CHILD|WS_TABSTOP,0,0,100,100,window,(HMENU)102,instance,nullptr);
 searchEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,100,24,window,(HMENU)103,instance,nullptr);setCue(searchEdit,L"在项目中搜索…");setEditFont(searchEdit,11);
 termEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,100,24,window,(HMENU)104,instance,nullptr);setEditFont(termEdit,11,true);
 SetWindowSubclass(editor,EditSubclass,1,(DWORD_PTR)this);SetWindowSubclass(searchEdit,EditSubclass,2,(DWORD_PTR)this);SetWindowSubclass(termEdit,EditSubclass,3,(DWORD_PTR)this);
 configureEditor(editor,true);configureEditor(output,false);configureEditor(termOutput,false);terminalAppend("Orbit Shell 1.0 · 输入 help 查看工作区命令。\n");
 tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,instance,nullptr);TOOLINFOW ti{sizeof(ti)};ti.uFlags=TTF_SUBCLASS|TTF_IDISHWND;ti.hwnd=window;ti.uId=(UINT_PTR)window;ti.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&ti);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,550);SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,350);
 scanProject(root,true);updateLayout();
 appendOutput("工作区已准备就绪。\n在这里，把你的想法变成代码。\n\n点击 ▶ 运行，或按 Shift + F10。\nPython 将使用本地解释器运行。\n");
 DragAcceptFiles(window,TRUE);
}
void App::configureEditor(HWND h,bool code){
 auto S=[&](UINT m,uptr_t w=0,sptr_t l=0){return SendMessageW(h,m,w,l);};S(SCI_SETCODEPAGE,SC_CP_UTF8);S(SCI_SETTECHNOLOGY,SC_TECHNOLOGY_DIRECT_WRITE_1);S(SCI_STYLESETFONT,STYLE_DEFAULT,(sptr_t)"JetBrains Mono");S(SCI_STYLESETSIZEFRACTIONAL,STYLE_DEFAULT,(code?settings.font:11)*75);S(SCI_STYLESETFORE,STYLE_DEFAULT,rgb(theme.text));S(SCI_STYLESETBACK,STYLE_DEFAULT,rgb(code?theme.bg:theme.tabs));S(SCI_STYLECLEARALL);S(SCI_SETMARGINLEFT,0,px(code?26:0));S(SCI_SETMARGINRIGHT,0,px(12));S(SCI_SETMARGINWIDTHN,0,code?px(43):0);S(SCI_SETMARGINWIDTHN,1,code?px(13):0);S(SCI_SETMARGINWIDTHN,2,0);S(SCI_SETMARGINTYPEN,0,SC_MARGIN_NUMBER);S(SCI_STYLESETFORE,STYLE_LINENUMBER,rgb(settings.light?0x969ba4:0x636976));S(SCI_STYLESETSIZEFRACTIONAL,STYLE_LINENUMBER,825);S(SCI_STYLESETBACK,STYLE_LINENUMBER,rgb(theme.bg));S(SCI_SETMARGINBACKN,0,rgb(theme.bg));S(SCI_SETMARGINBACKN,1,rgb(theme.bg));S(SCI_SETMARGINTYPEN,1,SC_MARGIN_SYMBOL);S(SCI_SETMARGINMASKN,1,SC_MASK_FOLDERS);S(SCI_SETMARGINSENSITIVEN,1,TRUE);
 S(SCI_SETFOLDMARGINCOLOUR,TRUE,rgb(theme.bg));S(SCI_SETFOLDMARGINHICOLOUR,TRUE,rgb(theme.bg));for(int i=SC_MARKNUM_FOLDEREND;i<=SC_MARKNUM_FOLDEROPEN;i++){S(SCI_MARKERDEFINE,i,SC_MARK_EMPTY);S(SCI_MARKERSETFORE,i,rgb(theme.muted));S(SCI_MARKERSETBACK,i,rgb(theme.bg));}if(code&&!orbit::configureFoldIcons(h,dpi,settings.light)){S(SCI_MARKERDEFINE,SC_MARKNUM_FOLDER,SC_MARK_PLUS);S(SCI_MARKERDEFINE,SC_MARKNUM_FOLDEROPEN,SC_MARK_MINUS);}
 S(SCI_SETVSCROLLBAR,FALSE);S(SCI_SETHSCROLLBAR,FALSE);S(SCI_SETSCROLLWIDTH,1);S(SCI_SETSCROLLWIDTHTRACKING,TRUE);S(SCI_SETENDATLASTLINE,TRUE);S(SCI_SETCARETFORE,rgb(theme.accent));S(SCI_SETCARETWIDTH,2);S(SCI_SETCARETLINEVISIBLE,code);S(SCI_SETCARETLINEVISIBLEALWAYS,code);S(SCI_SETCARETLINEBACK,rgb(settings.light?0xeeece8:0x292c33));S(SCI_SETSELFORE,TRUE,rgb(settings.light?0x303846:0xe1e6ef));S(SCI_SETSELBACK,TRUE,rgb(settings.light?0xd7e0ee:0x354460));S(SCI_SETSELECTIONLAYER,SC_LAYER_UNDER_TEXT);S(SCI_SETCARETLINEHIGHLIGHTSUBLINE,FALSE);
 S(SCI_SETEXTRAASCENT,0);S(SCI_SETEXTRADESCENT,0);
 // Keep the code font size while reducing interline padding; output spacing stays independent.
 int lineHeight=(int)S(SCI_TEXTHEIGHT,0),desired=px((code?settings.font:11)+(code?7:9));
 S(SCI_SETEXTRAASCENT,px(2));S(SCI_SETEXTRADESCENT,std::max(0,desired-lineHeight-px(2)));S(SCI_SETWRAPMODE,code?(settings.wrap?SC_WRAP_WORD:SC_WRAP_NONE):SC_WRAP_WORD);S(SCI_SETTABWIDTH,settings.tab);S(SCI_SETINDENT,settings.tab);S(SCI_SETUSETABS,FALSE);S(SCI_SETINDENTATIONGUIDES,SC_IV_NONE);S(SCI_SETBACKSPACEUNINDENTS,TRUE);S(SCI_SETTABINDENTS,TRUE);S(SCI_SETMULTIPLESELECTION,TRUE);S(SCI_SETADDITIONALSELECTIONTYPING,TRUE);S(SCI_SETADDITIONALCARETSBLINK,FALSE);S(SCI_SETPHASESDRAW,SC_PHASES_MULTIPLE);S(SCI_SETLAYOUTCACHE,SC_CACHE_PAGE);S(SCI_SETIMEINTERACTION,SC_IME_INLINE);S(SCI_SETMOUSEDWELLTIME,500);S(SCI_SETACCESSIBILITY,SC_ACCESSIBILITY_ENABLED);S(SCI_SETREADONLY,!code);
 if(code){S(SCI_AUTOCSETIGNORECASE,TRUE);S(SCI_AUTOCSETMAXHEIGHT,10);S(SCI_AUTOCSETMAXWIDTH,55);S(SCI_AUTOCSETCHOOSESINGLE,FALSE);S(SCI_AUTOCSETDROPRESTOFWORD,TRUE);S(SCI_SETELEMENTCOLOUR,SC_ELEMENT_LIST,rgb(theme.text)|0xff000000u);S(SCI_SETELEMENTCOLOUR,SC_ELEMENT_LIST_BACK,rgb(theme.panel)|0xff000000u);S(SCI_SETELEMENTCOLOUR,SC_ELEMENT_LIST_SELECTED_BACK,rgb(theme.selected)|0xff000000u);S(SCI_SETMODEVENTMASK,SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT);}
}
void App::styleEditor(){
 if(active.empty()||!documents.contains(active))return;
 auto& document=documents.at(active);
 auto e=extension(active);
 const orbit::EditorAppearance desired{settings.font,settings.tab,static_cast<int>(dpi),settings.wrap,settings.light,e};
 if(document.appearance && *document.appearance==desired && currentEditorAppearance && *currentEditorAppearance==desired)return;
 const bool needsLexer=!document.appearance || document.appearance->language!=e;
 configureEditor(editor,true);
 document.appearance=desired;
 currentEditorAppearance=desired;
const char*lex=e=="py"?"python":e=="js"||e=="cpp"||e=="h"?"cpp":e=="json"?"json":e=="md"?"markdown":e=="html"?"hypertext":e=="css"?"css":e=="toml"?"toml":e=="yaml"||e=="yml"?"yaml":"null";if(needsLexer)sci(SCI_SETILEXER,0,(sptr_t)CreateLexer(lex));
 auto style=[&](int n,unsigned c,bool italic=false){sci(SCI_STYLESETFORE,n,rgb(c));sci(SCI_STYLESETITALIC,n,italic);};
 auto key=settings.light?0x925b91:0xcd96c8,str=settings.light?0x69834c:0xa6be8b,def=settings.light?0x8d6737:0xe7be83,comment=settings.light?0x84918a:0x737f83,number=settings.light?0x9e704b:0xd4aa7d;
 if(e=="py"){sci(SCI_SETKEYWORDS,0,(sptr_t)"and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield");sci(SCI_SETKEYWORDS,1,(sptr_t)"abs all any bool bytes callable chr dict enumerate filter float format frozenset getattr hasattr hash help hex id input int isinstance issubclass iter len list map max min next object open ord pow print range repr reversed round set setattr slice sorted str sum super tuple type vars zip self cls");style(SCE_P_WORD,key);style(SCE_P_WORD2,theme.blue);for(int n:{SCE_P_STRING,SCE_P_CHARACTER,SCE_P_TRIPLE,SCE_P_TRIPLEDOUBLE,SCE_P_FSTRING,SCE_P_FCHARACTER,SCE_P_FTRIPLE,SCE_P_FTRIPLEDOUBLE})style(n,str);style(SCE_P_COMMENTLINE,comment,true);style(SCE_P_COMMENTBLOCK,comment,true);style(SCE_P_NUMBER,number);style(SCE_P_CLASSNAME,def);style(SCE_P_DEFNAME,def);style(SCE_P_DECORATOR,theme.purple);style(SCE_P_OPERATOR,theme.text);sci(SCI_SETPROPERTY,(uptr_t)"fold",(sptr_t)"1");sci(SCI_SETPROPERTY,(uptr_t)"fold.quotes.python",(sptr_t)"1");}
 else if(e=="js"||e=="cpp"||e=="h"){sci(SCI_SETKEYWORDS,0,(sptr_t)"const let var function return if else for while class new import from export default async await true false null undefined int void auto public private include namespace using");style(SCE_C_WORD,key);style(SCE_C_STRING,str);style(SCE_C_CHARACTER,str);style(SCE_C_COMMENT,comment,true);style(SCE_C_COMMENTLINE,comment,true);style(SCE_C_NUMBER,number);}
 else if(e=="md"){for(int n=SCE_MARKDOWN_HEADER1;n<=SCE_MARKDOWN_HEADER6;n++)style(n,def);style(SCE_MARKDOWN_STRONG1,def);style(SCE_MARKDOWN_LINK,theme.blue);style(SCE_MARKDOWN_CODE,str);style(SCE_MARKDOWN_CODEBK,str);}
 else if(e=="json"){style(SCE_JSON_STRING,str);style(SCE_JSON_PROPERTYNAME,theme.blue);style(SCE_JSON_NUMBER,number);style(SCE_JSON_KEYWORD,key);}
 sci(SCI_STYLESETBACK,STYLE_BRACELIGHT,rgb(settings.light?0xead7c6:0x4e4335));sci(SCI_STYLESETFORE,STYLE_BRACELIGHT,rgb(theme.accent));sci(SCI_SETIDLESTYLING,SC_IDLESTYLING_AFTERVISIBLE);
}
void App::updateLayout(){if(!window)return;RECT r;GetClientRect(window,&r);layout.update(dip(r.right),dip(r.bottom),sideVisible,structureVisible,bottomVisible,focusMode);ensureTabVisible();positionControls();invalidate();}
void App::positionControls(){
 auto place=[&](HWND h,D2D1_RECT_F r,bool visible){if(!h)return;if(visible){SetWindowPos(h,nullptr,px(r.left),px(r.top),std::max(1,px(width(r))),std::max(1,px(height(r))),SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);}else ShowWindow(h,SW_HIDE);};
 auto code=layout.code;code.top+=14;code.right-=9;code.bottom-=settings.wrap?0:8;place(editor,code,!active.empty());
 float b=layout.bottomTabs.bottom;bool bottom=bottomVisible&&height(layout.bottomRect)>0;
 place(output,rect(layout.editor.left+52,b+39,width(layout.editor)-67,std::max(1.f,layout.bottomRect.bottom-b-47)),bottom&&panel=="run");
 place(termOutput,rect(layout.editor.left+15,b+34,width(layout.editor)-30,std::max(1.f,layout.bottomRect.bottom-b-72)),bottom&&panel=="terminal");
 place(termEdit,rect(layout.editor.left+130,layout.bottomRect.bottom-31,width(layout.editor)-145,22),bottom&&panel=="terminal");
 bool search=tool=="search"&&width(layout.side)>0;place(searchEdit,rect(layout.side.left+36,layout.side.top+53,width(layout.side)-64,20),search);
 if(popup)resizePopup();
}
void App::invalidate(){if(window)InvalidateRect(window,nullptr,FALSE);}
void App::toast(std::string s){message=s;toastText=std::move(s);toastUntil=Clock::now()+std::chrono::seconds(4);SetTimer(window,3,4000,nullptr);invalidate();}
std::wstring App::editText(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
void App::applySettings(){theme=settings.light?Theme::light():Theme{};styleEditor();configureEditor(output,false);configureEditor(termOutput,false);DWORD border=rgb(theme.top);DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));for(auto h:{searchEdit,termEdit,popupEdit,popupEdit2})if(h)InvalidateRect(h,nullptr,TRUE);saveSession();updateLayout();if(popup)InvalidateRect(popup,nullptr,FALSE);}
LRESULT CALLBACK App::WndProc(HWND h,UINT m,WPARAM w,LPARAM l){App*a=(App*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);a->window=h;}return a?a->proc(h,m,w,l):DefWindowProcW(h,m,w,l);}
LRESULT App::proc(HWND h,UINT m,WPARAM w,LPARAM l){
 switch(m){
 case WM_CREATE:createControls();return 0;
 case WM_NCCALCSIZE:if(w){if(IsZoomed(h)){auto p=(NCCALCSIZE_PARAMS*)l;MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi);p->rgrc[0]=mi.rcWork;}return 0;}break;
 case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);int edge=px(5);if(!IsZoomed(h)){bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}float x=dip(p.x),y=dip(p.y);if(y<layout.title){for(auto&hit:hits)if(contains(hit.box,x,y))return HTCLIENT;return HTCAPTION;}return HTCLIENT;}
 case WM_GETMINMAXINFO:{auto mm=(MINMAXINFO*)l;mm->ptMinTrackSize={px(640),px(440)};return 0;}
 case WM_SIZE:if(w!=SIZE_MINIMIZED){if(paint.rt)paint.resize();updateLayout();}return 0;
 case WM_DPICHANGED:{dpi=(float)HIWORD(w);scale=dpi/96;auto r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);paint.dpi=dpi;paint.resize();for(auto c:{searchEdit,termEdit})setEditFont(c,11,c==termEdit);styleEditor();configureEditor(output,false);configureEditor(termOutput,false);updateLayout();return 0;}
 case WM_SHOWWINDOW:if(w)SetTimer(h,5,50,nullptr);break;case WM_ACTIVATE:if(LOWORD(w)!=WA_INACTIVE)SetTimer(h,5,50,nullptr);break;case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);if(!paint.rt)paint.init(h,d2d.Get(),dw.Get(),fontCollection.Get(),dpi);if(paint.rt){paint.begin(settings.light);render();paint.end();}EndPaint(h,&ps);return 0;}
 case WM_MOUSEMOVE:{float x=dip(GET_X_LPARAM(l)),y=dip(GET_Y_LPARAM(l));if(dragMode){float delta=(dragMode==1?x:y)-dragStart;if(dragMode==1)layout.sidebar=std::clamp(dragSize+delta,175.f,410.f);else if(dragMode==2)layout.bottom=std::clamp(dragSize-delta,110.f,layout.h*.65f);else if(dragMode==3){int total=(int)sci(SCI_GETLINECOUNT);float track=height(layout.code)-12;sci(SCI_SETFIRSTVISIBLELINE,std::max(0,(int)(dragSize+delta*std::max(1,total)/track)));}else if(dragMode==4){int max=std::max(0,(int)sci(SCI_GETSCROLLWIDTH)-px(width(layout.code)-75));sci(SCI_SETXOFFSET,std::max(0,(int)(dragSize+(x-dragStart)*max/std::max(1.f,width(layout.code)-70))));}if(dragMode<=2)updateLayout();else invalidate();return 0;}int next=-1;for(int i=(int)hits.size()-1;i>=0;i--)if(contains(hits[i].box,x,y)){next=i;break;}if(next!=hover){hover=next;tipText=next>=0?hits[next].tip:L"";invalidate();}TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);return 0;}
 case WM_MOUSELEAVE:hover=-1;invalidate();return 0;
 case WM_SETCURSOR:if(LOWORD(l)==HTCLIENT&&hover>=0&&hover<(int)hits.size()){auto&a=hits[hover].action;SetCursor(LoadCursor(nullptr,a=="resize-side"?IDC_SIZEWE:a=="resize-bottom"?IDC_SIZENS:IDC_HAND));return TRUE;}break;
 case WM_LBUTTONDOWN:{SetFocus(h);closeMenu();float x=dip(GET_X_LPARAM(l)),y=dip(GET_Y_LPARAM(l));for(auto it=hits.rbegin();it!=hits.rend();++it){auto&hit=*it;if(contains(hit.box,x,y)&&(hit.action=="resize-side"||hit.action=="resize-bottom"||hit.action=="scroll-code"||hit.action=="scroll-horizontal")){dragMode=hit.action=="resize-side"?1:hit.action=="resize-bottom"?2:hit.action=="scroll-code"?3:4;dragStart=dragMode==1||dragMode==4?x:y;dragSize=dragMode==1?layout.sidebar:dragMode==2?layout.bottom:dragMode==3?(float)sci(SCI_GETFIRSTVISIBLELINE):(float)sci(SCI_GETXOFFSET);SetCapture(h);return 0;}}return 0;}
 case WM_LBUTTONUP:if(dragMode){dragMode=0;ReleaseCapture();saveSession();return 0;}dispatchClick(dip(GET_X_LPARAM(l)),dip(GET_Y_LPARAM(l)));return 0;
 case WM_CAPTURECHANGED:dragMode=0;return 0;
 case WM_RBUTTONUP:{float x=dip(GET_X_LPARAM(l)),y=dip(GET_Y_LPARAM(l));for(auto&hit:hits)if(hit.action=="open-file"&&contains(hit.box,x,y)){showMenu(contains(layout.tabs,x,y)?"tab-context":"file-context",x,y,hit.data);return 0;}return 0;}
 case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);int d=GET_WHEEL_DELTA_WPARAM(w)>0?-3:3;if(contains(layout.side,dip(p.x),dip(p.y))){treeScroll=std::clamp(treeScroll+d,0,std::max(0,(int)treeRows.size()-5));invalidate();}else if(contains(layout.structureRect,dip(p.x),dip(p.y))){structureScroll=std::clamp(structureScroll+d,0,std::max(0,(int)symbols.size()-3));invalidate();}return 0;}
 case WM_COMMAND:if(HIWORD(w)==EN_CHANGE){if((HWND)l==searchEdit){SetTimer(h,2,200,nullptr);}return 0;}break;
 case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:{auto dc=(HDC)w;if((HWND)l==dimmer){SetBkColor(dc,RGB(0,0,0));return (LRESULT)GetStockObject(BLACK_BRUSH);}SetTextColor(dc,rgb(theme.text));SetBkColor(dc,rgb(theme.bg));static HBRUSH dark=CreateSolidBrush(RGB(30,31,34)),light=CreateSolidBrush(RGB(250,249,247));return (LRESULT)(settings.light?light:dark);}
 case WM_NOTIFY:{auto n=(NMHDR*)l;if(n->hwndFrom==editor){changed((SCNotification*)l);return 0;}if(n->code==TTN_GETDISPINFOW){auto t=(NMTTDISPINFOW*)l;t->lpszText=tipText.data();return 0;}break;}
 case WM_TIMER:if(w==6){flushRunOutput();}else if(w==1){KillTimer(h,1);saveActive();rebuildSymbols();}else if(w==2){KillTimer(h,2);findProject();}else if(w==3){KillTimer(h,3);toastText.clear();invalidate();}else if(w==4){KillTimer(h,4);complete();}else if(w==5){KillTimer(h,5);RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);}return 0;
 case WM_ORBIT_FILE:fileEvent((FileEvent*)l);return 0;
 case WM_ORBIT_OUTPUT:{std::unique_ptr<std::string>s((std::string*)l);appendOutput(*s);return 0;}
 case WM_ORBIT_DONE:runDone((RunResult*)l);return 0;
 case WM_ORBIT_SEARCH:searchResult((SearchResult*)l);return 0;
 case WM_DROPFILES:{auto drop=(HDROP)w;UINT n=DragQueryFileW(drop,0xffffffff,nullptr,0);for(UINT i=0;i<n;i++){wchar_t p[32768];DragQueryFileW(drop,i,p,32768);if(fs::is_directory(p))scanProject(p);else importFile(p);}DragFinish(drop);return 0;}
 case WM_KEYDOWN:if(key((UINT)w,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_MENU)&0x8000)!=0))return 0;break;
 case WM_CLOSE:finishClose();return 0;
 case WM_DESTROY:quickPick.close();alive=false;searchCancel++;KillTimer(h,1);KillTimer(h,2);KillTimer(h,6);outputBuffer.cancel();if(running)stopRun();if(runThread.joinable())runThread.join();io.drain();searchWorker.drain();for(auto&[p,d]:documents)if(d.pointer)sci(SCI_RELEASEDOCUMENT,0,d.pointer);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(h,m,w,l);
}
LRESULT CALLBACK App::EditSubclass(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){auto a=(App*)data;if(id==20&&m==WM_LBUTTONUP){a->closePopup();return 0;}if(m==WM_KEYDOWN){bool ctrl=GetKeyState(VK_CONTROL)&0x8000,shift=GetKeyState(VK_SHIFT)&0x8000,alt=GetKeyState(VK_MENU)&0x8000;if(a->popup&&id>=10){if(w==VK_ESCAPE){a->closePopup();return 0;}if(a->popupMode=="palette"||a->popupMode=="files"){if(w==VK_DOWN||w==VK_UP){int n=(int)a->paletteItems.size();if(n)a->selected=(a->selected+(w==VK_DOWN?1:-1)+n)%n;InvalidateRect(a->popup,nullptr,FALSE);return 0;}}if(w==VK_TAB&&a->popupEdit2){SetFocus(h==a->popupEdit?a->popupEdit2:a->popupEdit);return 0;}if(w==VK_RETURN&&(!((GetWindowLongPtrW(h,GWL_STYLE)&ES_MULTILINE))||ctrl)){a->popupSubmit();return 0;}}
 if(id==3){if(w==VK_RETURN){auto cmd=utf8(a->editText(h));SetWindowTextW(h,L"");a->terminalCommand(cmd);return 0;}if(w==VK_UP||w==VK_DOWN){a->terminalHistoryIndex=std::clamp(a->terminalHistoryIndex+(w==VK_UP?-1:1),0,(int)a->terminalHistory.size());SetWindowTextW(h,a->terminalHistoryIndex<(int)a->terminalHistory.size()?wide(a->terminalHistory[a->terminalHistoryIndex]).c_str():L"");SendMessageW(h,EM_SETSEL,(WPARAM)-1,-1);return 0;}}
 if(id==2&&w==VK_RETURN){if(!a->searchHits.empty())a->openFile(a->searchHits[0].path,a->searchHits[0].line);return 0;}
 if(!a->popup&&a->key((UINT)w,ctrl,shift,alt))return 0;
 }
 if(m==WM_CHAR&&id==1&&(w==')'||w==']'||w=='}'||w=='\"'||w=='\'')){auto pos=a->sci(SCI_GETCURRENTPOS);if(a->sci(SCI_GETCHARAT,pos)==(int)w){a->sci(SCI_CHARRIGHT);return 0;}}
 if(m==WM_NCDESTROY)RemoveWindowSubclass(h,EditSubclass,id);return DefSubclassProc(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR command,int show){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);App app;
 if(!app.init(h)){MessageBoxW(nullptr,L"无法初始化 Direct2D / Scintilla / SVG 图标。请检查系统版本和安装文件。",L"Orbit IDE",MB_ICONERROR);return 1;}
 std::wstring cmd=command;if(cmd.find(L"--icon-test")!=cmd.npos){int code=app.iconTest(app.exeDir/L"icon-tests.json");DestroyWindow(app.window);CoUninitialize();return code;}if(cmd.find(L"--performance-test")!=cmd.npos){int code=app.performanceTest(app.exeDir/L"performance-tests.json");DestroyWindow(app.window);CoUninitialize();return code;}if(cmd.find(L"--workbench-test")!=cmd.npos){int code=app.workbenchTest(app.exeDir/L"workbench-tests.json");DestroyWindow(app.window);CoUninitialize();return code;}if(cmd.find(L"--self-test")!=cmd.npos){int code=app.selfTest(app.exeDir/L"native-self-test.json");DestroyWindow(app.window);CoUninitialize();return code;}
 if(cmd.find(L"--benchmark")!=cmd.npos){app.benchmark(app.exeDir/L"native-benchmark.json");DestroyWindow(app.window);CoUninitialize();return 0;}
 if(cmd.find(L"--integration-test")!=cmd.npos){int code=app.integrationTest(app.exeDir/L"native-integration-test.json");DestroyWindow(app.window);CoUninitialize();return code;}
 ShowWindow(app.window,show);ShowWindow(app.window,SW_SHOW);UpdateWindow(app.window);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN||msg.message==WM_SYSKEYDOWN){bool ctrl=GetKeyState(VK_CONTROL)&0x8000,shift=GetKeyState(VK_SHIFT)&0x8000,alt=GetKeyState(VK_MENU)&0x8000;if(!app.hasTransientPopup()&&app.key((UINT)msg.wParam,ctrl,shift,alt))continue;}TranslateMessage(&msg);DispatchMessageW(&msg);}for(auto f:app.ownedFonts)DeleteObject(f);CoUninitialize();return (int)msg.wParam;
}

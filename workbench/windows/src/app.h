#pragma once
#include "core.h"
#include "navigation/history.h"
#include "ui/quick_pick.h"
#include "runtime/console_view.h"
#include "workspace/project_tree.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <windowsx.h>
struct MenuItem{std::wstring label,key;std::string action,data;bool separator=false;};
struct PaletteItem{std::wstring label,detail;std::string action,data;};
class App{
public:
 int workbenchTest(const fs::path& report);
 int performanceTest(const fs::path& report);
 int iconTest(const fs::path& report);
 bool hasTransientPopup() const { return popup || quickPick.isOpen(); }

private:
 // The host owns these subsystems; UI code dispatches actions instead of mutating them.
 orbit::NavigationHistory navigation;
 orbit::QuickPick quickPick;
 std::optional<orbit::NavigationLocation> pendingNavigation;
 bool historyReplay = false;
 bool activatingDocument = false;
 bool projectTreeDirty = true;
 std::map<std::string, orbit::EditorViewState> restoredViews;
 std::optional<orbit::EditorAppearance> currentEditorAppearance;
 orbit::NavigationLocation captureEditorLocation();
 void rememberEditorView();
 void restoreEditorView(const orbit::EditorViewState& view);
 void navigateHistory(bool forward);
 void reopenClosedTab();
 void showRecentFiles();
 void showFileOutline();
 void releaseClosedDocument(const std::string& path);
 bool workbenchAction(const std::string& id, const std::string& data);
 bool workbenchKey(UINT key, bool ctrl, bool shift, bool alt);
 orbit::RunOutputBuffer outputBuffer;
 orbit::ConsoleView outputConsole;
 orbit::ConsoleView terminalConsole;
 orbit::ProjectTreeIndex projectIndex;
 bool projectIndexDirty = true;
 int checkpointGeneration = 0;
 void flushRunOutput();
 void clearConsole(bool terminal);
public:
 HINSTANCE instance{};HWND window{},editor{},output{},termOutput{},searchEdit{},termEdit{},popup{},popupEdit{},popupEdit2{},dimmer{},menuWindow{},tooltip{};
 ComPtr<ID2D1Factory>d2d;ComPtr<IDWriteFactory>dw;ComPtr<IDWriteFontCollection>fontCollection;Paint paint,popupPaint,menuPaint;
 Theme theme;Settings settings;Layout layout;float dpi=96,scale=1;fs::path exeDir,root,dataDir;std::map<std::string,Document>documents;std::vector<std::string>tabs;std::string active,tool="project",panel="run",message="欢迎使用 Orbit。保持专注，创造更多。";
 std::set<std::string>expanded{"","src","tests"},completionWords;std::vector<TreeRow>treeRows;std::vector<Symbol>symbols;std::vector<SearchHit>searchHits;std::vector<Hit>hits,popupHits,menuHits;std::vector<MenuItem>menuItems;std::vector<PaletteItem>paletteItems;
 std::vector<std::pair<fs::path,json>>checkpoints;std::vector<std::string>terminalHistory;int terminalHistoryIndex=0;
 bool sideVisible=true,structureVisible=true,bottomVisible=true,focusMode=false,suppress=false,running=false,closing=false,expandedBottom=false,unsaved=false;
 int treeScroll=0,structureScroll=0,hover=-1,menuHover=-1,popupHover=-1,selected=0,dragMode=0,searchGeneration=0,scanGeneration=0,problems=0;float dragStart=0,dragSize=0,tabScroll=0;std::string lastError,runLabel="main",popupMode,popupData,popupError,menuName,toastText;std::wstring tipText;Clock::time_point toastUntil{},started=Clock::now();
 WorkQueue io,searchWorker;std::atomic<bool>alive{true},saveFailed{false};std::atomic<int>searchCancel{0};std::thread runThread;std::mutex processMutex;HANDLE processJob=nullptr;std::atomic<bool>stopRequested{false};std::vector<HFONT>ownedFonts;
 LRESULT sci(unsigned msg,uptr_t w=0,sptr_t l=0){return SendMessageW(editor,msg,w,l);}int px(float n)const{return (int)std::lround(n*scale);}float dip(int n)const{return n/scale;}
 bool init(HINSTANCE h);void createControls();void configureEditor(HWND h,bool code);void styleEditor();void updateLayout();void positionControls();void render();void renderTitle();void renderSidebar();void renderTabs();void renderStructure();void renderBottom();void renderStatus();void renderPopup();void renderMenu();
 void hit(D2D1_RECT_F box,std::string action,std::string data="",int index=0,std::wstring tip=L"");void button(D2D1_RECT_F box,Icon icon,std::string action,std::wstring tip=L"",unsigned c=0,std::string data="");void textButton(std::wstring text,D2D1_RECT_F box,std::string action,std::string data="",unsigned c=0,float size=11);void fileGlyph(Paint&p,const std::string&path,float x,float y,float size=12);void invalidate();void toast(std::string text);void setTool(std::string name);void setPanel(std::string name);void action(const std::string&id,const std::string&data="",int index=0);void dispatchClick(float x,float y);bool key(UINT key,bool ctrl,bool shift,bool alt);
 void scanProject(const fs::path&path,bool restore=false);void rebuildTree();void openFile(const std::string&path,int line=-1);void activateDoc(const std::string&path,int line=-1);void closeTab(const std::string&path);std::string currentText();void changed(SCNotification*n);void saveActive(bool notify=false);void saveAll();void saveSession();void loadSession();void rebuildSymbols();void findProject();void searchResult(SearchResult*r);void fileEvent(FileEvent*e);void gotoLine(int line);void complete();void toggleComment();
 void ensureTabVisible();
 void showPopup(std::string mode,std::string data="");void closePopup();void popupSubmit();void paletteFilter();void paletteChoose(int index);void resizePopup();std::wstring editText(HWND h);void setEditFont(HWND h,float size,bool mono=false);void setCue(HWND h,const wchar_t*cue);void popupClick(float x,float y);void showMenu(std::string name,float x,float y,std::string data="");void closeMenu();void menuClick(float x,float y);void menuSelect(int n);void openFilesDialog();void openFolderDialog();void exportProject();void downloadFile();void importFile(const fs::path&p);void checkpoint(std::string name);void listCheckpoints();void restoreCheckpoint(int i);void atomicProjectExport(const fs::path&dest,std::string label);
 void run(bool tests=false,std::string path="",std::string inlineCode="");void stopRun();void appendOutput(std::string_view text,bool error=false);void runDone(RunResult*r);void terminalCommand(std::string command);void terminalAppend(std::string text);void copyOutput();void findInFile(bool replace,bool next=true);void newFile(const std::string&p);void renameFile(const std::string&oldPath,const std::string&newPath);void deleteFile(const std::string&p);void applySettings();void finishClose();int selfTest(const fs::path&report);int integrationTest(const fs::path&report);void benchmark(const fs::path&report);
 LRESULT proc(HWND h,UINT msg,WPARAM w,LPARAM l);LRESULT popupProc(HWND h,UINT msg,WPARAM w,LPARAM l);LRESULT menuProc(HWND h,UINT msg,WPARAM w,LPARAM l);static LRESULT CALLBACK WndProc(HWND,UINT,WPARAM,LPARAM);static LRESULT CALLBACK PopupProc(HWND,UINT,WPARAM,LPARAM);static LRESULT CALLBACK MenuProc(HWND,UINT,WPARAM,LPARAM);static LRESULT CALLBACK EditSubclass(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
};

#include "app.h"
#include "editor/text_format.h"
#include <cctype>
#include <iomanip>
static void sendFile(App*a,FileEvent*e){if(!a->alive||!PostMessageW(a->window,WM_ORBIT_FILE,0,(LPARAM)e))delete e;}
void App::loadSession() {
    try {
        const auto session = json::parse(readFile(dataDir / L"session.json"));
        const auto directory = session.value("root", "");
        if (!directory.empty() && fs::is_directory(fs::u8path(directory))) root = fs::u8path(directory);
        const auto config = session.value("settings", json::object());
        settings.font = std::clamp(config.value("font", 13), 11, 20);
        settings.tab = config.value("tab", 4);
        if (settings.tab != 2 && settings.tab != 4 && settings.tab != 8) settings.tab = 4;
        settings.wrap = config.value("wrap", false);
        settings.light = config.value("light", false);
        settings.stdinText = config.value("stdin", "");
        settings.interpreter = wide(config.value("interpreter", "python"));
        layout.sidebar = std::clamp(session.value("sidebar", 238.f), 175.f, 410.f);
        layout.bottom = std::clamp(session.value("bottom", 226.f), 110.f, 550.f);
        tabs = session.value("tabs", std::vector<std::string>{});
        active = session.value("active", "");
        sideVisible = session.value("sideVisible", true);
        structureVisible = session.value("structureVisible", true);
        bottomVisible = session.value("bottomVisible", true);
        auto recent = session.value("recentFiles", std::vector<std::string>{});
        std::erase_if(recent, [](const auto& path) { return !validPath(path); });
        navigation.restoreRecent(recent);
        if (session.contains("editorViews") && session["editorViews"].is_object()) {
            for (const auto& [path, value] : session["editorViews"].items()) {
                if (restoredViews.size() >= orbit::NavigationHistory::recentLimit) break;
                if (!validPath(path)) continue;
                restoredViews[path] = {value.value("caret", std::int64_t(0)), value.value("anchor", std::int64_t(0)),
                    value.value("firstLine", 0), value.value("xOffset", 0)};
            }
        }
    } catch (...) {
        // A missing or malformed session falls back to the built-in sample project.
    }
}

void App::saveSession() {
    if (!window) return;
    try {
        rememberEditorView();
        json views = json::object();
        for (const auto& path : navigation.recent()) {
            if (!documents.contains(path)) continue;
            const auto& view = documents.at(path).view;
            views[path] = {{"caret", view.caret}, {"anchor", view.anchor},
                {"firstLine", view.firstVisibleLine}, {"xOffset", view.horizontalOffset}};
        }
        const json session = {{"root", utf8(root.wstring())}, {"tabs", tabs}, {"active", active},
            {"sidebar", layout.sidebar}, {"bottom", layout.bottom}, {"sideVisible", sideVisible},
            {"structureVisible", structureVisible}, {"bottomVisible", bottomVisible},
            {"recentFiles", navigation.recent()}, {"editorViews", views},
            {"settings", {{"font", settings.font}, {"tab", settings.tab}, {"wrap", settings.wrap},
                {"light", settings.light}, {"stdin", settings.stdinText}, {"interpreter", utf8(settings.interpreter)}}}};
        atomicWrite(dataDir / L"session.json", session.dump(2));
    } catch (...) {
        message = "设置保存失败，请检查目录写入权限。";
    }
}
void App::scanProject(const fs::path&path,bool restore){
 if(running){toast("请先停止正在运行的程序再切换项目。");return;}saveAll();int generation=++scanGeneration;message="正在读取项目…";auto requested=path;invalidate();
 io.push([this,requested,restore,generation]{auto e=new FileEvent;e->path="@scan";e->revision=generation;try{if(!fs::is_directory(requested))throw std::runtime_error("项目目录不存在");json data;data["root"]=utf8(fs::absolute(requested).wstring());data["restore"]=restore;std::vector<std::string>paths;std::error_code ec;for(fs::recursive_directory_iterator i(requested,fs::directory_options::skip_permission_denied,ec),end;i!=end;i.increment(ec)){if(ec){ec.clear();continue;}if(i->is_symlink(ec)){if(i->is_directory(ec))i.disable_recursion_pending();continue;}auto name=utf8(i->path().filename().wstring());if(i->is_directory(ec)){if(name.starts_with('.')||name=="node_modules"||name=="__pycache__"||name=="vendor"||name=="build")i.disable_recursion_pending();continue;}auto rel=utf8(fs::relative(i->path(),requested).generic_wstring());auto ext=extension(rel);if(validPath(rel)&&(ext=="py"||ext=="js"||ext=="json"||ext=="md"||ext=="txt"||ext=="html"||ext=="css"||ext=="toml"||ext=="yaml"||ext=="yml"||ext=="csv"||ext=="cpp"||ext=="h"||name==".gitignore"||name=="CMakeLists.txt"))paths.push_back(rel);if(paths.size()>=20000)break;}data["paths"]=paths;e->text=data.dump();}catch(const std::exception&ex){e->error=ex.what();}sendFile(this,e);});
}
void App::rebuildTree() {
    if (projectIndexDirty || projectIndex.pathCount() != documents.size()) {
        std::vector<std::string> paths;
        paths.reserve(documents.size());
        for (const auto& [path, document] : documents) paths.push_back(path);
        projectIndex.rebuild(paths);
        projectIndexDirty = false;
    }
    treeRows = projectIndex.visibleRows(expanded, utf8(root.filename().wstring()));
    projectTreeDirty = false;
    treeScroll = std::min(treeScroll, std::max(0, static_cast<int>(treeRows.size()) - 5));
    invalidate();
}
void App::ensureTabVisible(){float x=0,total=0,left=0,right=0;for(auto&p:tabs){float w=std::max(137.f,std::min(210.f,70.f+(float)wide(baseName(p)).size()*7));if(p==active){left=x;right=x+w;}x+=w;}total=x;float available=std::max(100.f,width(layout.editor)-32);if(left<tabScroll)tabScroll=left;if(right>tabScroll+available)tabScroll=right-available;tabScroll=std::clamp(tabScroll,0.f,std::max(0.f,total-available));}
void App::openFile(const std::string& path, int line) {
    if (!documents.contains(path)) return;
    if (pendingNavigation && pendingNavigation->path != path) {
        pendingNavigation.reset();
        historyReplay = false;
    }
    if (path != active) {
        rememberEditorView();
        if (!historyReplay) navigation.depart(captureEditorLocation());
        saveActive();
    }
    auto& document = documents.at(path);
    if (std::find(tabs.begin(), tabs.end(), path) == tabs.end()) {
        const auto index = pendingNavigation && pendingNavigation->tabIndex >= 0 ?
            std::min(tabs.size(), static_cast<std::size_t>(pendingNavigation->tabIndex)) : tabs.size();
        tabs.insert(tabs.begin() + index, path);
    }
    active = path;
    for (auto slash = path.find('/'); slash != path.npos; slash = path.find('/', slash + 1)) {
        if (expanded.insert(path.substr(0, slash)).second) projectTreeDirty = true;
    }
    ensureTabVisible();
    if (document.loaded) { activateDoc(path, line); return; }
    if (document.pending) return;
    document.pending = true;
    message = "正在打开 " + path + "…";
    suppress = true;
    sci(SCI_SETDOCPOINTER, 0, 0);
    sci(SCI_SETREADONLY, TRUE);
    suppress = false;
    if (projectTreeDirty) rebuildTree();
    updateLayout();
    const auto file = root / fs::u8path(path);
    io.push([this, path, file, line] {
        auto* event = new FileEvent;
        event->path = path;
        event->load = true;
        event->revision = line + 1;
        try { event->text = readFile(file); }
        catch (const std::exception& error) { event->error = error.what(); }
        sendFile(this, event);
    });
}

void App::activateDoc(const std::string& path, int line) {
    if (!documents.contains(path)) return;
    auto& document = documents.at(path);
    suppress = true;
    if (!document.pointer) {
        document.pointer = sci(SCI_CREATEDOCUMENT, 0,
            document.cache.size() > 16 * 1024 * 1024 ? SC_DOCUMENTOPTION_TEXT_LARGE : 0);
        sci(SCI_SETDOCPOINTER, 0, document.pointer);
        sci(SCI_SETREADONLY, FALSE);
        sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(document.cache.c_str()));
        const auto endings = orbit::detectLineEnding(document.cache);
        sci(SCI_SETEOLMODE, endings == orbit::LineEnding::CrLf ? SC_EOL_CRLF :
            endings == orbit::LineEnding::Cr ? SC_EOL_CR : SC_EOL_LF);
        sci(SCI_EMPTYUNDOBUFFER);
        sci(SCI_SETSAVEPOINT);
    } else {
        sci(SCI_SETDOCPOINTER, 0, document.pointer);
        sci(SCI_SETREADONLY, FALSE);
    }
    suppress = false;
    closeLanguageHints();
    active = path;
    startLanguage();
    syncLanguageDocument();
    updateDiagnosticIndicators();
    activatingDocument = true;
    styleEditor();
    if (projectTreeDirty) rebuildTree();
    rebuildSymbols();
    positionControls();
    const bool restoringHistory = historyReplay && pendingNavigation && pendingNavigation->path == path;
    if (pendingNavigation && pendingNavigation->path == path) {
        restoreEditorView(pendingNavigation->view);
        pendingNavigation.reset();
    } else {
        restoreEditorView(document.view);
    }
    if (line >= 0) gotoLine(line);
    activatingDocument = false;
    historyReplay = false;
    navigation.touchRecent(path);
    if (!restoringHistory) navigation.visit(captureEditorLocation());
    rememberEditorView();
    message = document.dirty ? "正在保存…" : "所有更改已保存";
    SetFocus(editor);
    saveSession();
    invalidate();
}
std::string App::currentText(){auto n=(size_t)sci(SCI_GETLENGTH);std::string s(n+1,0);sci(SCI_GETTEXT,n+1,(sptr_t)s.data());s.resize(n);return s;}
void App::closeTab(const std::string& path) {
    const auto found = std::find(tabs.begin(), tabs.end(), path);
    if (found == tabs.end()) return;
    const std::size_t index = found - tabs.begin();
    const bool isActive = path == active;
    if (isActive) {
        rememberEditorView();
        navigation.depart(captureEditorLocation());
        saveActive();
    }
    navigation.rememberClosed({path, documents.at(path).view, static_cast<int>(index)});
    tabs.erase(found);
    if (isActive) {
        active.clear();
        if (!tabs.empty()) openFile(tabs[std::min(index, tabs.size() - 1)]);
        else {
            symbols.clear();
            completionWords.clear();
            ShowWindow(editor, SW_HIDE);
        }
    }
    language.close(path);
    releaseClosedDocument(path);
    saveSession();
    invalidate();
}
void App::changed(SCNotification*n){
 if(suppress)return;
 if(n->nmhdr.code==SCN_MODIFIED&&(n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))&&!active.empty()&&documents.contains(active)){
 auto&d=documents[active];d.revision++;d.dirty=true;unsaved=true;SetTimer(window,1,600,nullptr);RECT r{0,px(layout.title),px(layout.w),px(layout.crumb.bottom)};InvalidateRect(window,&r,FALSE);
 }
 if(n->nmhdr.code==SCN_UPDATEUI){RECT status{0,px(layout.statusRect.top),px(layout.w),px(layout.h)};InvalidateRect(window,&status,FALSE);RECT bar{px(layout.code.right-10),px(layout.code.top),px(layout.code.right),px(layout.code.bottom)};InvalidateRect(window,&bar,FALSE);RECT hbar{px(layout.code.left),px(layout.code.bottom-8),px(layout.code.right),px(layout.code.bottom)};InvalidateRect(window,&hbar,FALSE);auto pos=sci(SCI_GETCURRENTPOS);auto c=sci(SCI_GETCHARAT,pos-1);if(c=='('||c==')'||c=='['||c==']'||c=='{'||c=='}'){auto match=sci(SCI_BRACEMATCH,pos-1,0);if(match>=0)sci(SCI_BRACEHIGHLIGHT,pos-1,match);else sci(SCI_BRACEBADLIGHT,pos-1);}else sci(SCI_BRACEHIGHLIGHT,(uptr_t)-1,-1);}
 if(n->nmhdr.code==SCN_MARGINCLICK){sci(SCI_TOGGLEFOLD,sci(SCI_LINEFROMPOSITION,n->position));invalidate();}
 if(n->nmhdr.code==SCN_CHARADDED){
  auto pos=sci(SCI_GETCURRENTPOS);int ch=n->ch;auto line=sci(SCI_LINEFROMPOSITION,pos);
  if(ch=='\n'&&line>0){auto indent=sci(SCI_GETLINEINDENTATION,line-1);auto end=sci(SCI_GETLINEENDPOSITION,line-1);auto start=sci(SCI_POSITIONFROMLINE,line-1);while(end>start&&(sci(SCI_GETCHARAT,end-1)==' '||sci(SCI_GETCHARAT,end-1)=='\r'))end--;if(extension(active)=="py"&&sci(SCI_GETCHARAT,end-1)==':')indent+=settings.tab;sci(SCI_SETLINEINDENTATION,line,indent);sci(SCI_GOTOPOS,sci(SCI_GETLINEINDENTPOSITION,line));}
  char closingChar=ch=='('?')':ch=='['?']':ch=='{'?'}':ch=='\"'?'\"':ch=='\''?'\'':0;
  if(closingChar){int next=(int)sci(SCI_GETCHARAT,pos);int prev=(int)sci(SCI_GETCHARAT,pos-2);if((next==0||isspace((unsigned char)next)||next==')'||next==']'||next=='}')&&(!(ch=='\"'||ch=='\'')||!isalnum((unsigned char)prev))){char s[2]{closingChar,0};sci(SCI_INSERTTEXT,pos,(sptr_t)s);}}
  if(ch>0&&ch<128&&(isalnum(ch)||ch=='_'))SetTimer(window,4,55,nullptr);
 }
 languageChanged(n);
}
void App::saveActive(bool notify){KillTimer(window,1);if(active.empty()||!documents.contains(active))return;auto&d=documents[active];if(!d.loaded)return;if(!d.dirty){if(notify)toast("所有更改已保存");return;}d.cache=currentText();uint64_t revision=d.revision;std::string path=active,snapshot=d.cache;auto dest=root/fs::u8path(path);io.push([this,path,dest,revision,snapshot=std::move(snapshot),notify]{auto e=new FileEvent;e->path=path;e->revision=revision;try{atomicWrite(dest,snapshot);}catch(const std::exception&ex){e->error=ex.what();saveFailed=true;}if(notify)e->text="notify";sendFile(this,e);});}
void App::saveAll(){saveActive();for(auto&[p,d]:documents){if(p==active||!d.loaded||!d.dirty)continue;std::string text;if(d.pointer){auto old=sci(SCI_GETDOCPOINTER);suppress=true;sci(SCI_SETDOCPOINTER,0,d.pointer);text=currentText();sci(SCI_SETDOCPOINTER,0,old);suppress=false;d.cache=text;}else text=d.cache;auto dest=root/fs::u8path(p);auto rev=d.revision;io.push([this,p,dest,rev,text=std::move(text)]{auto e=new FileEvent;e->path=p;e->revision=rev;try{atomicWrite(dest,text);}catch(const std::exception&ex){e->error=ex.what();saveFailed=true;}sendFile(this,e);});}}
void App::fileEvent(FileEvent*event){std::unique_ptr<FileEvent>e(event);if(!e->error.empty()){if(documents.contains(e->path))documents[e->path].pending=false;toast(e->error);return;}if(e->path=="@scan"){
 if(e->revision!=(uint64_t)scanGeneration)return;
 closeLanguageHints();for(const auto&path:tabs)language.close(path);
 auto data=json::parse(e->text);auto newRoot=fs::u8path(data["root"].get<std::string>());bool restore=data.value("restore",false);suppress=true;sci(SCI_SETDOCPOINTER,0,0);for(auto&[p,d]:documents)if(d.pointer)sci(SCI_RELEASEDOCUMENT,0,d.pointer);documents.clear();suppress=false;const bool sameProject=lower(utf8(root.wstring()))==lower(utf8(newRoot.wstring()));if(!restore||!sameProject){navigation.clear();restoredViews.clear();}else navigation.resetHistory();pendingNavigation.reset();historyReplay=false;root=newRoot;projectIndexDirty=true;startLanguage();for(auto&item:data["paths"]){std::string p=item.get<std::string>();Document d;d.path=p;if(restoredViews.contains(p))d.view=restoredViews.at(p);documents.emplace(p,std::move(d));}restoredViews.clear();if(!restore){tabs={};active.clear();}tabs.erase(std::remove_if(tabs.begin(),tabs.end(),[this](auto&p){return !documents.contains(p);}),tabs.end());if(tabs.empty()){for(auto p:{"src/main.py","src/utils.py","README.md"})if(documents.contains(p))tabs.push_back(p);}if(!documents.contains(active))active=tabs.empty()?documents.empty()?"":documents.begin()->first:tabs.front();expanded={"","src","tests"};treeScroll=0;searchHits.clear();SetWindowTextW(searchEdit,L"");rebuildTree();listCheckpoints();if(!active.empty())openFile(active);else{symbols.clear();positionControls();}message="项目已载入 · "+std::to_string(documents.size())+" 个文件";saveSession();invalidate();return;}
 if(e->path=="@checkpoint-list"){
    if(e->revision!=static_cast<std::uint64_t>(checkpointGeneration))return;
    const auto items=json::parse(e->text);
    checkpoints.clear();
    for(const auto& item:items)checkpoints.emplace_back(fs::u8path(item["path"].get<std::string>()),item["metadata"]);
    invalidate();return;
 }
 if(e->path=="@toast"){toast(e->text);return;}if(e->path=="@checkpoint"){listCheckpoints();toast(e->text);return;}
 if(e->path=="@rescan"){toast(e->text);scanProject(root,true);return;}
 if(!documents.contains(e->path))return;auto&d=documents[e->path];if(e->load){d.outlineRevision=std::numeric_limits<std::uint64_t>::max();d.cache=std::move(e->text);d.loaded=true;d.pending=false;if(active==e->path)activateDoc(e->path,(int)e->revision-1);else releaseClosedDocument(e->path);}else{d.savedRevision=e->revision;if(d.revision==e->revision){d.dirty=false;if(active==e->path)sci(SCI_SETSAVEPOINT);}unsaved=false;for(auto&[p,doc]:documents)if(doc.dirty)unsaved=true;if(e->text=="notify")toast("已保存到本地磁盘");releaseClosedDocument(e->path);invalidate();}}
void App::rebuildSymbols() {
    symbols.clear();
    completionWords.clear();
    if (active.empty() || !documents.contains(active) || !documents.at(active).loaded) {
        invalidate();
        return;
    }
    auto& document = documents.at(active);
    // Check the budget before copying the editor's text into an analysis snapshot.
    if (sci(SCI_GETLENGTH) > 2 * 1024 * 1024) {
        message = "大文件模式：结构分析与自动补全已暂停。";
        invalidate();
        return;
    }
    if (document.outlineRevision != document.revision) {
        document.outline = orbit::analyzeDocument(currentText(), extension(active));
        document.outlineRevision = document.revision;
    }
    symbols = document.outline.symbols;
    completionWords = document.outline.completionWords;
    structureScroll = std::min(structureScroll, std::max(0, static_cast<int>(symbols.size()) - 1));
    invalidate();
}

void App::gotoLine(int line) {
    if (!activatingDocument) navigation.depart(captureEditorLocation());
    line = std::clamp(line, 0, std::max(0, static_cast<int>(sci(SCI_GETLINECOUNT)) - 1));
    sci(SCI_ENSUREVISIBLEENFORCEPOLICY, line);
    sci(SCI_GOTOLINE, line);
    sci(SCI_SCROLLCARET);
    rememberEditorView();
    if (!activatingDocument) navigation.visit(captureEditorLocation());
    SetFocus(editor);
    invalidate();
}
void App::complete(){if(extension(active)=="py"||extension(active)=="pyi"||extension(active)=="pyw"){requestLanguageCompletion();return;}if(active.empty()||sci(SCI_AUTOCACTIVE))return;auto pos=sci(SCI_GETCURRENTPOS),start=sci(SCI_WORDSTARTPOSITION,pos,TRUE);int len=(int)(pos-start);if(len<2||len>60)return;char prefix[80]{};Sci_TextRangeFull tr{{start,pos},prefix};sci(SCI_GETTEXTRANGEFULL,0,(sptr_t)&tr);std::string words;for(auto&w:completionWords)if(w!=prefix&&lower(w).starts_with(lower(prefix))){if(!words.empty())words+=' ';words+=w;if(words.size()>3000)break;}if(!words.empty())sci(SCI_AUTOCSHOW,len,(sptr_t)words.c_str());}
void App::toggleComment(){if(active.empty())return;auto from=sci(SCI_GETSELECTIONSTART),to=sci(SCI_GETSELECTIONEND);int first=(int)sci(SCI_LINEFROMPOSITION,from),last=(int)sci(SCI_LINEFROMPOSITION,to);if(to==sci(SCI_POSITIONFROMLINE,last)&&last>first)last--;std::string prefix=extension(active)=="py"?"#":"//";bool remove=true;for(int line=first;line<=last;line++){auto pos=sci(SCI_GETLINEINDENTPOSITION,line);for(size_t n=0;n<prefix.size();n++)if(sci(SCI_GETCHARAT,pos+n)!=prefix[n])remove=false;}sci(SCI_BEGINUNDOACTION);for(int line=first;line<=last;line++){auto pos=sci(SCI_GETLINEINDENTPOSITION,line);if(remove){size_t n=prefix.size();if(sci(SCI_GETCHARAT,pos+n)==' ')n++;sci(SCI_DELETERANGE,pos,n);}else{sci(SCI_INSERTTEXT,pos,(sptr_t)(prefix+" ").c_str());}}sci(SCI_ENDUNDOACTION);}
void App::findProject(){std::string query=utf8(editText(searchEdit));searchGeneration++;searchCancel=searchGeneration;int generation=searchGeneration;treeScroll=0;searchHits.clear();if(query.empty()){invalidate();return;}std::map<std::string,std::string>snapshots;if(!active.empty()&&documents.contains(active)&&documents[active].loaded)snapshots[active]=currentText();std::vector<std::string>paths;for(auto&[p,d]:documents)paths.push_back(p);auto dir=root;auto needle=lower(query);searchWorker.push([this,generation,dir,paths=std::move(paths),snapshots=std::move(snapshots),needle]{auto r=new SearchResult;r->generation=generation;for(auto&p:paths){if(searchCancel!=generation||!alive){delete r;return;}try{std::string text;if(snapshots.contains(p))text=snapshots.at(p);else{auto path=dir/fs::u8path(p);if(fs::file_size(path)>16*1024*1024)continue;text=readFile(path);}std::stringstream ss(text);std::string line;int n=0;while(std::getline(ss,line)){if(lower(line).find(needle)!=line.npos)r->hits.push_back({p,line.substr(0,1000),n});n++;if(r->hits.size()>=150)break;}}catch(...){}if(r->hits.size()>=150)break;}if(!alive||!PostMessageW(window,WM_ORBIT_SEARCH,0,(LPARAM)r))delete r;});invalidate();}
void App::searchResult(SearchResult*result){std::unique_ptr<SearchResult>r(result);if(r->generation!=searchGeneration)return;searchHits=std::move(r->hits);invalidate();}
void App::newFile(const std::string&p){if(!validPath(p)||p.starts_with(".orbit/"))throw std::runtime_error("请输入有效的相对路径，例如 src/hello.py");for(auto&[existing,d]:documents)if(lower(existing)==lower(p))throw std::runtime_error("该文件已存在");auto path=root/fs::u8path(p);if(!inside(root,path))throw std::runtime_error("文件必须位于项目目录内");std::string initial=extension(p)=="py"?"# Start something wonderful.\n\n":"";atomicWrite(path,initial);Document d;d.path=p;d.cache=initial;d.loaded=true;documents.emplace(p,std::move(d));projectIndexDirty=true;projectTreeDirty=true;setTool("project");openFile(p);toast("已创建 "+p);}
void App::renameFile(const std::string&old,const std::string&next){if(!validPath(next)||next.starts_with(".orbit/"))throw std::runtime_error("文件路径无效");if(next==old)return;if(fs::exists(root/fs::u8path(next)))throw std::runtime_error("该文件已存在");saveAll();io.drain();auto src=root/fs::u8path(old),dest=root/fs::u8path(next);if(!inside(root,src)||!inside(root,dest))throw std::runtime_error("路径超出项目范围");fs::create_directories(dest.parent_path());fs::rename(src,dest);language.close(old);auto node=documents.extract(old);node.key()=next;node.mapped().path=next;documents.insert(std::move(node));for(auto&p:tabs)if(p==old)p=next;projectIndexDirty=true;navigation.renamePath(old,next);if(active==old){active=next;documents.at(next).appearance.reset();openFile(next);}rebuildTree();saveSession();toast("文件已重命名");}
void App::deleteFile(const std::string& path) {
    const auto source = root / fs::u8path(path);
    if (!inside(root, source)) throw std::runtime_error("路径超出项目范围");
    saveFailed = false;
    saveAll();
    io.drain();
    if (saveFailed) throw std::runtime_error("文件未能保存，已取消删除。");
    const auto found = documents.find(path);
    if (found == documents.end()) return;
    // The queue has saved this revision. Its UI acknowledgment may still be pending;
    // closing the tab must not enqueue another write after moving the file to trash.
    found->second.dirty = false;
    found->second.savedRevision = found->second.revision;
    const auto backup = root / L".orbit" / L"trash" /
        (std::to_wstring(GetTickCount64()) + L"-" + source.filename().wstring());
    fs::create_directories(backup.parent_path());
    fs::rename(source, backup);
    closeTab(path);
    language.close(path);
    releaseClosedDocument(path);
    documents.erase(path);
    navigation.forgetPath(path);
    projectIndexDirty = true;
    unsaved = std::any_of(documents.begin(), documents.end(), [](const auto& entry) { return entry.second.dirty; });
    rebuildTree();
    saveSession();
    toast("文件已移入项目的 .orbit/trash，可手动找回。");
}
void App::importFile(const fs::path&p){try{if(p.extension()==L".json"&&p.filename().wstring().ends_with(L".orbit.json")){auto j=json::parse(readFile(p));if(j.value("format","")!="orbit-project-v1"||!j.contains("files"))throw std::runtime_error("项目 JSON 格式不正确");saveAll();io.drain();for(auto&[path,value]:j["files"].items())if(validPath(path)&&!path.starts_with(".orbit/")&&value.is_string()){auto dest=root/fs::u8path(path);if(inside(root,dest))atomicWrite(dest,value.get<std::string>());}scanProject(root,true);toast("项目文件已导入");}else{auto path=utf8(p.filename().wstring());if(!validPath(path))throw std::runtime_error("文件名无效");if(documents.contains(path))throw std::runtime_error("同名文件已存在，请先重命名");auto data=readFile(p);if(data.size()>32*1024*1024)throw std::runtime_error("导入文件上限为 32 MB；大文件可直接打开所在文件夹");atomicWrite(root/p.filename(),data);Document d;d.path=path;d.cache=data;d.loaded=true;documents.emplace(path,std::move(d));projectIndexDirty=true;projectTreeDirty=true;openFile(path);toast("已导入 "+path);}}catch(const std::exception&ex){toast(ex.what());}}
void App::openFilesDialog(){ComPtr<IFileOpenDialog>d;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d))))return;d->SetTitle(L"导入代码文件或 Orbit 项目");d->SetOptions(FOS_ALLOWMULTISELECT|FOS_FILEMUSTEXIST|FOS_FORCEFILESYSTEM);if(SUCCEEDED(d->Show(window))){ComPtr<IShellItemArray>a;d->GetResults(&a);DWORD n=0;a->GetCount(&n);for(DWORD i=0;i<n;i++){ComPtr<IShellItem>item;a->GetItemAt(i,&item);PWSTR p=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){importFile(p);CoTaskMemFree(p);}}}}
void App::openFolderDialog(){ComPtr<IFileOpenDialog>d;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d))))return;d->SetTitle(L"打开项目文件夹");d->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);if(SUCCEEDED(d->Show(window))){ComPtr<IShellItem>i;d->GetResult(&i);PWSTR p=nullptr;if(SUCCEEDED(i->GetDisplayName(SIGDN_FILESYSPATH,&p))){scanProject(p);CoTaskMemFree(p);}}}
void App::atomicProjectExport(const fs::path&dest,std::string label){saveAll();std::vector<std::string>paths;for(auto&[p,d]:documents)paths.push_back(p);auto dir=root;io.push([this,dest,dir,paths=std::move(paths),label]{auto e=new FileEvent;e->path="@toast";try{json files=json::object();for(auto&p:paths)files[p]=readFile(dir/fs::u8path(p));json j={{"format","orbit-project-v1"},{"name",utf8(dir.filename().wstring())},{"files",files}};atomicWrite(dest,j.dump(2));e->text=label;}catch(const std::exception&ex){e->error=ex.what();}sendFile(this,e);});}
void App::exportProject(){ComPtr<IFileSaveDialog>d;if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d))))return;d->SetTitle(L"导出 Orbit 项目");d->SetFileName(L"orbit-python.orbit.json");d->SetDefaultExtension(L"json");COMDLG_FILTERSPEC f{L"Orbit 项目",L"*.orbit.json"};d->SetFileTypes(1,&f);if(SUCCEEDED(d->Show(window))){ComPtr<IShellItem>i;d->GetResult(&i);PWSTR p=nullptr;if(SUCCEEDED(i->GetDisplayName(SIGDN_FILESYSPATH,&p))){atomicProjectExport(p,"项目已导出");CoTaskMemFree(p);}}}
void App::downloadFile(){if(active.empty())return;ComPtr<IFileSaveDialog>d;if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d))))return;auto name=wide(baseName(active));d->SetTitle(L"另存当前文件副本");d->SetFileName(name.c_str());if(SUCCEEDED(d->Show(window))){ComPtr<IShellItem>i;d->GetResult(&i);PWSTR p=nullptr;if(SUCCEEDED(i->GetDisplayName(SIGDN_FILESYSPATH,&p))){try{atomicWrite(p,currentText());toast("文件副本已保存");}catch(const std::exception&ex){toast(ex.what());}CoTaskMemFree(p);}}}
void App::checkpoint(std::string name){saveAll();auto dir=root;std::vector<std::string>paths;for(auto&[p,d]:documents)paths.push_back(p);SYSTEMTIME st;GetLocalTime(&st);char date[64];sprintf_s(date,"%04d-%02d-%02d %02d:%02d:%02d",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);auto dest=dir/L".orbit"/L"checkpoints"/(std::to_wstring(GetTickCount64())+L".json");io.push([this,name,dir,paths=std::move(paths),dest,date=std::string(date)]{auto e=new FileEvent;e->path="@checkpoint";try{json files=json::object();for(auto&p:paths)files[p]=readFile(dir/fs::u8path(p));atomicWrite(dest,json{{"name",name},{"date",date},{"files",files}}.dump(2));auto metadataPath=dest;metadataPath+=L".meta.json";atomicWrite(metadataPath,json{{"name",name},{"date",date}}.dump());e->text="检查点已保存";}catch(const std::exception&ex){e->error=ex.what();}sendFile(this,e);});}
void App::listCheckpoints() {
    const auto directory = root / L".orbit" / L"checkpoints";
    const auto generation = ++checkpointGeneration;
    io.push([this, directory, generation] {
        auto* event = new FileEvent;
        event->path = "@checkpoint-list";
        event->revision = generation;
        json items = json::array();
        try {
            std::vector<std::pair<fs::file_time_type, fs::path>> paths;
            if (fs::exists(directory)) {
                for (const auto& entry : fs::directory_iterator(directory)) {
                    if (entry.path().extension() == L".json" && !entry.path().filename().wstring().ends_with(L".meta.json")) {
                        paths.emplace_back(entry.last_write_time(), entry.path());
                    }
                }
            }
            std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& [date, path] : paths) {
                if (items.size() == 10) break;
                try {
                    auto metadataPath = path;
                    metadataPath += L".meta.json";
                    const auto snapshot = json::parse(readFile(fs::exists(metadataPath) ? metadataPath : path));
                    if (!snapshot.contains("name") || !snapshot.contains("date")) continue;
                    items.push_back({{"path", utf8(path.wstring())},
                        {"metadata", {{"name", snapshot["name"]}, {"date", snapshot["date"]}}}});
                } catch (...) { /* Skip incomplete or invalid historical snapshots. */ }
            }
        } catch (...) { /* An unavailable history directory produces an empty list. */ }
        event->text = items.dump();
        sendFile(this, event);
    });
}
void App::restoreCheckpoint(int index){if(running)throw std::runtime_error("请先停止正在运行的程序。");if(index<0||index>=(int)checkpoints.size())return;saveAll();io.drain();json before=json::object();for(auto&[p,d]:documents)before[p]=readFile(root/fs::u8path(p));SYSTEMTIME st;GetLocalTime(&st);char date[64];sprintf_s(date,"%04d-%02d-%02d %02d:%02d:%02d",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);auto backup=root/L".orbit"/L"checkpoints"/(std::to_wstring(GetTickCount64())+L"-backup.json");atomicWrite(backup,json{{"name","恢复前的自动备份"},{"date",date},{"files",before}}.dump(2));auto target=json::parse(readFile(checkpoints[index].first)).at("files");for(auto&[p,d]:documents){if(!target.contains(p)){auto src=root/fs::u8path(p),dest=root/L".orbit"/L"trash"/(std::to_wstring(GetTickCount64())+L"-"+src.filename().wstring());fs::create_directories(dest.parent_path());if(fs::exists(src)&&inside(root,src))fs::rename(src,dest);}}for(auto&[p,data]:target.items())if(validPath(p)&&!p.starts_with(".orbit/")&&data.is_string()&&inside(root,root/fs::u8path(p)))atomicWrite(root/fs::u8path(p),data.get<std::string>());scanProject(root,true);toast("已恢复检查点，当前版本已自动备份。");}
void App::findInFile(bool replace,bool next){if(active.empty()||!popupEdit)return;auto query=utf8(editText(popupEdit));if(query.empty())return;sci(SCI_SETSEARCHFLAGS,0);auto start=next?sci(SCI_GETSELECTIONEND):sci(SCI_GETSELECTIONSTART);if(replace){auto selStart=sci(SCI_GETSELECTIONSTART),selEnd=sci(SCI_GETSELECTIONEND);sci(SCI_SETTARGETSTART,selStart);sci(SCI_SETTARGETEND,selEnd);if(sci(SCI_SEARCHINTARGET,query.size(),(sptr_t)query.c_str())>=0){auto value=utf8(editText(popupEdit2));sci(SCI_REPLACETARGET,value.size(),(sptr_t)value.c_str());start=sci(SCI_GETTARGETEND);}}
 auto n=sci(SCI_GETLENGTH);sci(SCI_SETTARGETSTART,start);sci(SCI_SETTARGETEND,n);auto found=sci(SCI_SEARCHINTARGET,query.size(),(sptr_t)query.c_str());if(found<0){sci(SCI_SETTARGETSTART,0);sci(SCI_SETTARGETEND,start);found=sci(SCI_SEARCHINTARGET,query.size(),(sptr_t)query.c_str());}if(found>=0){sci(SCI_SETSEL,found,found+query.size());sci(SCI_SCROLLCARET);}else toast("没有找到匹配内容");}
void App::finishClose(){if(closing)return;closing=true;quickPick.close();if(popup)closePopup();closeMenu();if(running)stopRun();saveAll();io.drain();saveSession();DestroyWindow(window);}

#include "app.h"
// A real application-level smoke test. It uses an isolated generated project,
// the same document model, I/O queues and Python process manager as the UI.
int App::integrationTest(const fs::path&report){
 const auto session=dataDir/L"session.json";std::string previousSession;try{previousSession=readFile(session);}catch(...){}
 json results=json::array();auto check=[&](const std::string&name,bool value){results.push_back({{"name",name},{"passed",value}});};
 auto pump=[&](std::function<bool()>ready,int milliseconds=8000){auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);do{MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}if(ready())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(Clock::now()<deadline);return false;};
 auto textOf=[&](HWND h){auto n=(size_t)SendMessageW(h,SCI_GETLENGTH,0,0);std::string text(n+1,0);SendMessageW(h,SCI_GETTEXT,n+1,(LPARAM)text.data());text.resize(n);return text;};
 try{
  pump([&]{return !active.empty()&&documents.contains(active)&&documents[active].loaded;});
  auto fixture=exeDir/(L"integration-fixture-"+std::to_wstring(GetCurrentProcessId()));fs::create_directories(fixture);fs::copy(exeDir/L"sample-project",fixture,fs::copy_options::recursive|fs::copy_options::overwrite_existing);scanProject(fixture);
  check("open_project_async",pump([&]{return root==fixture&&documents.contains("src/main.py")&&documents["src/main.py"].loaded;}));
  newFile("src/native_smoke.py");check("new_file_and_tab",active=="src/native_smoke.py"&&tabs.back()==active);
  const std::string code="from utils import progress_bar\nprint('原生编辑 ✓', 6 * 7)\nprint(progress_bar(2, 4))\n";
  sci(SCI_SETTEXT,0,(sptr_t)code.c_str());saveActive();check("utf8_edit_and_disk_save",pump([&]{return !documents[active].dirty;})&&readFile(root/L"src/native_smoke.py")==code);
  sci(SCI_APPENDTEXT,9,(sptr_t)"# change\n");bool undo=sci(SCI_CANUNDO)!=0;openFile("src/main.py");openFile("src/native_smoke.py");check("tab_switch_preserves_undo",undo&&sci(SCI_CANUNDO)!=0);sci(SCI_UNDO);saveActive();pump([&]{return !documents[active].dirty;});
  run(false,active);check("run_local_python_multifile",pump([&]{return !running;})&&problems==0&&textOf(output).find("原生编辑 ✓ 42")!=std::string::npos);
  SetWindowTextW(searchEdit,L"progress_bar");findProject();check("project_search_async",pump([&]{return searchHits.size()>=9;})&&searchHits[0].line>=0);
  auto doc=documents[active].pointer;checkpoint("Native integration checkpoint");check("checkpoint_preserves_editor_document",pump([&]{return !checkpoints.empty();})&&documents[active].pointer==doc);
  const std::string changedCode="print('changed')\n";sci(SCI_SETTEXT,0,(sptr_t)changedCode.c_str());saveActive();pump([&]{return !documents[active].dirty;});restoreCheckpoint(0);check("checkpoint_restore_and_backup",pump([&]{return documents.contains("src/native_smoke.py")&&documents["src/native_smoke.py"].loaded&&checkpoints.size()>=2&&currentText()==code&&readFile(root/L"src/native_smoke.py")==code;}));
  auto exported=fixture/L"export.orbit.json";atomicProjectExport(exported,"Exported");check("web_compatible_project_export",pump([&]{return fs::exists(exported);})&&json::parse(readFile(exported)).value("format","")=="orbit-project-v1");
  renameFile("src/native_smoke.py","src/native_renamed.py");check("rename_updates_document_and_disk",active=="src/native_renamed.py"&&fs::exists(root/L"src/native_renamed.py")&&!fs::exists(root/L"src/native_smoke.py"));
  run(true);check("unittest_four_cases",pump([&]{return !running;})&&problems==0&&textOf(output).find("Ran 4 tests")!=std::string::npos);
  run(false,"","print(1 / 0)");check("runtime_error_in_problems_panel",pump([&]{return !running;})&&problems==1&&lastError.find("ZeroDivisionError")!=std::string::npos);
  run(false,"","while True:\n    pass\n");bool launched=pump([&]{std::lock_guard lock(processMutex);return processJob!=nullptr;});stopRun();check("stop_infinite_loop",launched&&pump([&]{return !running;},5000)&&problems==0);
  settings.stdinText="7\n";run(false,"","print(int(input()) * 6)");check("standard_input",pump([&]{return !running;})&&problems==0&&textOf(output).find("42")!=std::string::npos);
  settings.stdinText=std::string(200000,'x');run(false,"","import sys\nprint('x' * 100000)\nprint(len(sys.stdin.read()))");check("bidirectional_pipe_backpressure",pump([&]{return !running;})&&problems==0&&textOf(output).find("200000")!=std::string::npos);
  settings.stdinText.clear();settings.wrap=true;applySettings();check("word_wrap_setting",sci(SCI_GETWRAPMODE)==SC_WRAP_WORD);settings.wrap=false;settings.light=true;applySettings();check("light_theme_updates_editor",sci(SCI_STYLEGETBACK,STYLE_DEFAULT)==rgb(Theme::light().bg));settings.light=false;applySettings();
  deleteFile("src/native_renamed.py");check("recoverable_delete",!documents.contains("src/native_renamed.py")&&fs::exists(root/L".orbit/trash"));
 }catch(const std::exception&e){results.push_back({{"name","unexpected_exception"},{"passed",false},{"error",e.what()}});if(running){stopRun();pump([&]{return !running;},5000);}}
 io.drain();searchWorker.drain();if(!previousSession.empty())atomicWrite(session,previousSession);
 bool passed=true;for(auto&r:results)if(!r["passed"].get<bool>())passed=false;atomicWrite(report,json{{"passed",passed},{"checks",results.size()},{"results",results},{"scope","Isolated real document, disk, search, checkpoint and local Python workflows. Visual verification is recorded separately."}}.dump(2));return passed?0:1;
}

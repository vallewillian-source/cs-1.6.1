// Exercise the actual UI input callback without needing a display or GL context.
#include "../../sdk/host/modui.cpp"
#include <cassert>
#include <filesystem>
#include <thread>
#include <chrono>
extern "C" void Mods_DebugPanels(const csmod_ui_t *,int) {}
extern "C" void Mods_DrawWorld(const csmod_view_t *,const csmod_draw_t *) {}
int main(int argc,char **argv) {
    assert(argc==2);
    auto base=std::filesystem::path(argv[1]);
    std::filesystem::create_directories(base/"packages/example");
    std::ofstream(base/"packages/example/mod.json")<<R"({"manifest_version":1,"id":"example"})";
    MH_Init((base/"packages").c_str(),(base/"state").c_str(),"client");
    auto *m=MH_At(0);assert(m);m->toggle_key='z';m->panel_key='p';
    main_ctx=ImGui::CreateContext();manager=true;
    auto& io=ImGui::GetIO();io.DisplaySize={1280,720};io.DeltaTime=1.f/60;
    io.IniFilename=nullptr;io.Fonts->Build();
    ImGui::NewFrame();ImGui::SetNextWindowPos({1467,155});ImGui::SetNextWindowSize({435,450});
    constrain_window();ImGui::Begin("restored window");keep_window_visible();
    auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
    assert(p.x>=8&&p.x+s.x<=1272&&p.y+s.y<=712);
    ImGui::End();ImGui::Render();
    ImGui::GetIO().WantTextInput=true;
    ModUI_Key('z',1);ModUI_Key('z',0);
    ModUI_Key('p',1);ModUI_Key('p',0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));MH_Sync();
    assert(!m->desired&&!m->panel); // typing never invokes direct shortcuts
    capture_binding=&m->panel_key;
    manager=false; // same close state produced by the title-bar close button
    assert(!ModUI_Key('w',1));assert(!ModUI_Key('w',0));
    assert(!capture_binding&&m->panel_key=='p'); // no hidden capture after close
    ImGui::GetIO().WantTextInput=false;
    ModUI_Key('p',1);ModUI_Key('p',0);assert(m->panel);
    ModUI_Key('z',1);ModUI_Key('z',0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));MH_Sync();assert(m->desired);
    MH_Shutdown();ImGui::DestroyContext(main_ctx);main_ctx=nullptr;
}

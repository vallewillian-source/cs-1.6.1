// In-game mod tools. SDL2/OpenGL2 backends; the public facade remains pure C.
// GPL-3.0.
#include "runtime.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl2.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
namespace {
SDL_Window *window=nullptr;
ImGuiContext *main_ctx=nullptr,*world_ctx=nullptr;
bool manager=false, all_panels=true;
int selected=0;
int *capture_binding=nullptr;
std::string binding_error;
std::array<bool,256> key_down{}, consumed{};
std::string ini_path;
void load_font(){
    char *base=SDL_GetBasePath();
    std::string path=std::string(base?base:"")+"mod-tools/LiberationSans-Regular.ttf";SDL_free(base);
    if(std::ifstream(path).good()){
        static const ImWchar ranges[]={0x20,0xff,0x2000,0x206f,0x2190,0x21ff,0};
        ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(),16,nullptr,ranges);
    }
}
using BindFramebuffer=void(APIENTRY *)(GLenum,GLuint);
BindFramebuffer bind_fbo=nullptr;
using ActiveTexture=void(APIENTRY *)(GLenum);
using BindBuffer=void(APIENTRY *)(GLenum,GLuint);
using UseProgram=void(APIENTRY *)(GLuint);
ActiveTexture active_texture=nullptr,client_texture=nullptr;
BindBuffer bind_buffer=nullptr;UseProgram use_program=nullptr;
// The official legacy backend restores its own state. The engine additionally
// uses fog/alpha testing, multiple texture units and client vertex arrays.
struct GLGuard {
    GLint active=0x84C0,client=0x84C0,array=0,element=0,program=0;
    GLGuard(){
        if(active_texture)glGetIntegerv(0x84E0,&active);
        if(client_texture)glGetIntegerv(0x84E1,&client);
        if(bind_buffer){glGetIntegerv(0x8894,&array);glGetIntegerv(0x8895,&element);}
        if(use_program)glGetIntegerv(0x8B8D,&program);
        glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        glDisable(GL_ALPHA_TEST);glDisable(GL_FOG);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        if(active_texture){
            GLint units=1;glGetIntegerv(0x84E2,&units);
            for(int i=0;i<std::min(units,8);i++){active_texture(0x84C0+i);glDisable(GL_TEXTURE_2D);}
            active_texture(0x84C0);
        }
        if(client_texture)client_texture(0x84C0);
        if(bind_buffer){bind_buffer(0x8892,0);bind_buffer(0x8893,0);}
        if(use_program)use_program(0);
    }
    ~GLGuard(){
        glPopClientAttrib();glPopAttrib();
        if(bind_buffer){bind_buffer(0x8892,array);bind_buffer(0x8893,element);}
        if(use_program)use_program(program);
        if(active_texture)active_texture(active);
        if(client_texture)client_texture(client);
    }
};
struct Panel {char search[128]{}; bool pause=false;int minimum=0;std::vector<mh_log_t> rows;};
Panel panels[MH_MAX_MODS];
void ui_text(const char *text) {ImGui::TextWrapped("%s",text);}
int ui_check(const char *text,int *value) {bool on=*value!=0;bool changed=ImGui::Checkbox(text,&on);*value=on;return changed;}
int ui_slider(const char *text,float *value,float min,float max) {return ImGui::SliderFloat(text,value,min,max);}
void ui_sep() {ImGui::Separator();}
const csmod_ui_t ui={sizeof(ui),ui_text,ui_check,ui_slider,ui_sep};
void draw_line(float x1,float y1,float x2,float y2,uint32_t color) {ImGui::GetBackgroundDrawList()->AddLine({x1,y1},{x2,y2},color,1.5f);}
void draw_text(float x,float y,uint32_t color,const char *text) {auto *d=ImGui::GetBackgroundDrawList();d->AddText({x+1,y+1},0xff111111,text);d->AddText({x,y},color,text);}
const csmod_draw_t draw={sizeof(draw),draw_line,draw_text};
void style() {
    ImGui::StyleColorsDark(); auto& s=ImGui::GetStyle();
    s.WindowRounding=5;s.FrameRounding=3;s.ScrollbarRounding=3;s.WindowPadding={14,12};s.ItemSpacing={8,7};
    s.Colors[ImGuiCol_WindowBg]={0.065f,0.075f,0.08f,0.96f};
    s.Colors[ImGuiCol_TitleBg]={0.09f,0.12f,0.13f,1};s.Colors[ImGuiCol_TitleBgActive]={0.10f,0.22f,0.22f,1};
    s.Colors[ImGuiCol_Header]={0.14f,0.28f,0.28f,1};s.Colors[ImGuiCol_Button]={0.13f,0.29f,0.28f,1};
    s.Colors[ImGuiCol_ButtonHovered]={0.18f,0.40f,0.36f,1};s.Colors[ImGuiCol_CheckMark]={0.36f,0.85f,0.69f,1};
    s.Colors[ImGuiCol_Text]={0.94f,0.95f,0.91f,1};s.Colors[ImGuiCol_TextDisabled]={0.64f,0.70f,0.69f,1};
}
void state(mh_mod_t *m) {
    int health=MH_Health(m);
    if(m->error[0]) ImGui::TextColored({1,.53f,.40f,1},"Erro: %s",m->error);
    else if(health<0)ImGui::TextColored({1,.53f,.40f,1},"Falha em um componente");
    else if(m->desired!=(health==1)) ImGui::TextColored({1,.80f,.44f,1},"%s",m->desired?"Iniciando componentes…":"Desativando…");
    else ImGui::TextColored(health==1?ImVec4(.40f,.87f,.70f,1):ImVec4(.69f,.73f,.72f,1),"%s",health==1?"Ativo":"Desativado");
}
void key_setting(const char *label,int *key) {
    ImGui::PushID(label);
    std::string name=*key==0?"Sem atalho":*key>=135&&*key<=146?"F"+std::to_string(*key-134):*key>=32&&*key<127?std::string(1,char(*key)):"Tecla "+std::to_string(*key);
    if(capture_binding==key)name="Pressione uma tecla…";
    if(ImGui::Button(name.c_str(),{160,0})){capture_binding=key;binding_error.clear();}
    ImGui::SameLine();ImGui::TextUnformatted(label);
    ImGui::PopID();
}
void constrain_window() {
    auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSizeConstraints({std::min(320.f,display.x-16),180},
        {std::max(320.f,display.x-16),std::max(180.f,display.y-32)});
}
void keep_window_visible() {
    auto display=ImGui::GetIO().DisplaySize,p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
    p.x=std::clamp(p.x,8.f,std::max(8.f,display.x-s.x-8));
    p.y=std::clamp(p.y,8.f,std::max(8.f,display.y-s.y-8));
    ImGui::SetWindowPos(p);
}
void show_manager() {
    ImGui::SetNextWindowSize({680,430},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({30,40},ImGuiCond_FirstUseEver);
    constrain_window();
    bool expanded=ImGui::Begin("Mods · CS 1.6.1",&manager);keep_window_visible();
    if(expanded) {
        ImGui::TextUnformatted("F6  Voltar ao jogo     F7  Painéis     F8  Alternar selecionado");
        ImGui::TextDisabled("Instale ou remova pastas em game/mods e reinicie a sessão.");
        ImGui::Separator();
        if(ImGui::BeginTable("mods",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Mod",0,2);ImGui::TableSetupColumn("Estado",0,2);ImGui::TableSetupColumn("Ativação");ImGui::TableSetupColumn("Debug");ImGui::TableHeadersRow();
            for(int i=0;i<MH_Count();i++) {
                auto *m=MH_At(i);ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableNextColumn();
                if(ImGui::Selectable(m->title,selected==i))selected=i;
                ImGui::TableNextColumn();state(m);ImGui::TableNextColumn();
                if(ImGui::Button(m->desired?"Desativar":"Ativar")) {if(!m->desired)m->error[0]=0;MH_Request(m,!m->desired);}
                ImGui::TableNextColumn();bool visible=m->panel;
                if(ImGui::Checkbox("Painel",&visible)) {m->panel=visible;MH_Preferences();}
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if(auto *m=MH_At(selected)) {
            ImGui::Separator();ImGui::Text("Selecionado: %s",m->id);
            ImGui::TextWrapped("Atalhos diretos: clique e pressione a tecla. Esc limpa. F6–F9 ficam reservados.");
            key_setting("Ativar/desativar",&m->toggle_key);key_setting("Mostrar painel",&m->panel_key);
            if(!binding_error.empty())ImGui::TextWrapped("%s",binding_error.c_str());
        }
        ImGui::Separator();ImGui::TextDisabled("%s",MH_Managed()?"Sessão local gerenciada · cliente + servidor + sidecars":"Cliente independente · componentes locais");
    }
    ImGui::End();
    if(!manager)capture_binding=nullptr;
}
void show_panel(int i,int w,int h) {
    auto *m=MH_At(i);auto& p=panels[i];bool open=m->panel;
    float width=std::min(435.f,float(w-32));
    ImGui::SetNextWindowSize({width,std::min(450.f,float(h-80))},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({i%2?float(w)-width-18.f:18.f,65.f+float(i/2)*90},ImGuiCond_FirstUseEver);
    std::string title=std::string(m->title)+" · Debug###"+m->id;
    auto flags=manager?ImGuiWindowFlags_None:ImGuiWindowFlags_NoInputs;
    constrain_window();
    bool expanded=ImGui::Begin(title.c_str(),&open,flags);keep_window_visible();
    if(expanded) {
        state(m); ImGui::SameLine();ImGui::TextDisabled("%s",m->id);
        char status[1024];MH_Status(m,status,sizeof(status));if(status[0])ImGui::TextWrapped("%s",status);
        if(ImGui::Button(m->desired?"Desativar mod":"Ativar mod")) {if(!m->desired)m->error[0]=0;MH_Request(m,!m->desired);}
        Mods_DebugPanels(&ui,i);
        char metrics[2048];MH_Metrics(m,metrics,sizeof(metrics));if(metrics[0])ImGui::TextWrapped("%s",metrics);
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##busca","Filtrar mensagens ou origem…",p.search,sizeof(p.search));
        ImGui::Checkbox("Pausar",&p.pause);ImGui::SameLine();
        ImGui::SetNextItemWidth(110);ImGui::Combo("##nivel",&p.minimum,"Tudo\0Avisos\0Erros\0");ImGui::SameLine();
        if(ImGui::SmallButton("Limpar")) {MH_Logs(m,nullptr,0,1);p.rows.clear();}
        if(!p.pause) {p.rows.resize(512);int n=MH_Logs(m,p.rows.data(),512,0);p.rows.resize(n);}
        if(ImGui::BeginChild("log",{0,0},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar)) {
            bool bottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-2;
            for(auto& row:p.rows) {
                if(row.severity<p.minimum)continue;
                if(p.search[0]&&!std::strstr(row.text,p.search)&&!std::strstr(row.origin,p.search))continue;
                ImVec4 color=row.severity>=2?ImVec4(1,.54f,.44f,1):row.severity==1?ImVec4(1,.82f,.47f,1):ImVec4(.85f,.89f,.86f,1);
                ImGui::PushStyleColor(ImGuiCol_Text,color);
                auto wall=std::time(nullptr)-time_t(m->services.MonotonicTime()-row.time);
                char stamp[16];std::strftime(stamp,sizeof(stamp),"%H:%M:%S",std::localtime(&wall));
                ImGui::Text("%s [%s] [%s] %s",stamp,row.severity>=2?"ERRO":row.severity==1?"AVISO":"INFO",row.origin,row.text);ImGui::PopStyleColor();
            }
            if(bottom&&!p.pause)ImGui::SetScrollHereY(1);
        }
        ImGui::EndChild();
    }
    ImGui::End();if(open!=bool(m->panel)){m->panel=open;MH_Preferences();}
}
}
extern "C" {
void ModUI_Init(void *win,void *context) {
    if(main_ctx) return;
    window=static_cast<SDL_Window*>(win);
    IMGUI_CHECKVERSION();main_ctx=ImGui::CreateContext();style();
    auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;load_font();
    ini_path=std::string(MH_StateRoot())+"/host/windows.ini";io.IniFilename=ini_path.c_str();
    ImGui_ImplSDL2_InitForOpenGL(window,context);ImGui_ImplOpenGL2_Init();
    world_ctx=ImGui::CreateContext();ImGui::SetCurrentContext(world_ctx);ImGui::GetIO().IniFilename=nullptr;load_font();ImGui_ImplOpenGL2_Init();
    unsigned char *px;int w,h;ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px,&w,&h);
    ImGui::SetCurrentContext(main_ctx);
    bind_fbo=reinterpret_cast<BindFramebuffer>(SDL_GL_GetProcAddress("glBindFramebuffer"));
    active_texture=reinterpret_cast<ActiveTexture>(SDL_GL_GetProcAddress("glActiveTexture"));
    client_texture=reinterpret_cast<ActiveTexture>(SDL_GL_GetProcAddress("glClientActiveTexture"));
    bind_buffer=reinterpret_cast<BindBuffer>(SDL_GL_GetProcAddress("glBindBuffer"));
    use_program=reinterpret_cast<UseProgram>(SDL_GL_GetProcAddress("glUseProgram"));
}
int ModUI_Capturing() {return main_ctx&&manager;}
int ModUI_Event(void *ptr) {
    if(!main_ctx)return 0;
    ImGui::SetCurrentContext(main_ctx);
    auto *e=static_cast<SDL_Event*>(ptr);ImGui_ImplSDL2_ProcessEvent(e);
    if(e->type==SDL_WINDOWEVENT&&e->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {manager=false;capture_binding=nullptr;key_down.fill(false);consumed.fill(false);}
    if(e->type==SDL_KEYDOWN||e->type==SDL_KEYUP)return 0; // engine routes keys to ModUI_Key
    return manager&&(e->type==SDL_MOUSEMOTION||e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_MOUSEBUTTONUP||e->type==SDL_MOUSEWHEEL||e->type==SDL_TEXTINPUT);
}
int ModUI_Key(int key,int down) {
    if(!main_ctx||key<0||key>=256)return 0;
    bool edge=down&&!key_down[key];key_down[key]=down;
    if(!manager)capture_binding=nullptr;
    if(capture_binding){
        if(edge){
            bool conflict=key>=140&&key<=143;
            for(int i=0;i<MH_Count();i++){
                auto *m=MH_At(i);
                if((&m->toggle_key!=capture_binding&&m->toggle_key==key)||(&m->panel_key!=capture_binding&&m->panel_key==key))conflict=true;
            }
            if(key==27){*capture_binding=0;capture_binding=nullptr;MH_Preferences();}
            else if(conflict)binding_error="Essa tecla já está em uso. Escolha outra.";
            else{*capture_binding=key;capture_binding=nullptr;MH_Preferences();}
        }
        return 1;
    }
    bool used=false;
    if(key==140) {if(edge)manager=!manager;used=true;}
    else if(key==141) {if(edge)all_panels=!all_panels;used=true;}
    else if(key==142) {if(edge)if(auto *m=MH_At(selected)){if(!m->desired)m->error[0]=0;MH_Request(m,!m->desired);}used=true;}
    else if(manager&&key==27) {if(edge)manager=false;used=true;}
    else if(manager&&(key==128||key==129)) {if(edge&&MH_Count())selected=(selected+(key==128?MH_Count()-1:1))%MH_Count();used=true;}
    else if((key<140||key>143)&&!(manager&&ImGui::GetIO().WantTextInput)) for(int i=0;i<MH_Count();i++) {
        auto *m=MH_At(i);
        if(key&&key==m->toggle_key){if(edge){m->error[0]=0;MH_Request(m,!m->desired);}used=true;}
        if(key&&key==m->panel_key){if(edge){m->panel=!m->panel;MH_Preferences();}used=true;}
    }
    if(manager)used=true;
    else capture_binding=nullptr;
    if(down&&used)consumed[key]=true;
    if(!down&&consumed[key]){consumed[key]=false;return 1;}
    return used;
}
void ModUI_Draw() {
    if(!main_ctx)return;
    ImGui::SetCurrentContext(main_ctx);
    GLGuard guard;
    ImGui_ImplOpenGL2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
    MH_UITelemetry(manager,all_panels,selected,ImGui::GetIO().DeltaTime*1000.0);
    int w,h;SDL_GetWindowSize(window,&w,&h);
    if(manager)show_manager();
    if(all_panels)for(int i=0;i<MH_Count();i++)if(MH_At(i)->panel)show_panel(i,w,h);
    ImGui::Render();
    GLint read=0,write=0;if(bind_fbo){glGetIntegerv(0x8CAA,&read);glGetIntegerv(0x8CA6,&write);bind_fbo(0x8D40,0);}
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    if(bind_fbo){bind_fbo(0x8CA8,read);bind_fbo(0x8CA9,write);}
}
void ModUI_World(const csmod_view_t *view) {
    if(!world_ctx||!view||view->width<=0||view->height<=0)return;
    GLGuard guard;
    ImGui::SetCurrentContext(world_ctx);auto& io=ImGui::GetIO();io.DisplaySize={float(view->width),float(view->height)};io.DeltaTime=1.f/100;
    ImGui_ImplOpenGL2_NewFrame();ImGui::NewFrame();Mods_DrawWorld(view,&draw);ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());ImGui::SetCurrentContext(main_ctx);
}
void ModUI_Shutdown() {
    if(!main_ctx)return;
    ImGui::SetCurrentContext(world_ctx);ImGui_ImplOpenGL2_Shutdown();ImGui::DestroyContext(world_ctx);world_ctx=nullptr;
    ImGui::SetCurrentContext(main_ctx);ImGui_ImplOpenGL2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext(main_ctx);main_ctx=nullptr;
}
}

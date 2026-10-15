// Package telemetry uses only the public SDK and copied JSON snapshots.
#include "mod_api.h"
#include "json.hpp"
#include <algorithm>
#include <cstdio>
using json=nlohmann::json;
namespace {
const csmod_services_t *host;
int init(const mod_engine_api_t *services) {
    host=services->services;
    return 1;
}
json snapshot(const char *channel,double max_age_ms) {
    char bytes[8192];
    int count=host->ReadSnapshot(host->context,channel,bytes,sizeof(bytes),max_age_ms);
    return count?json::parse(bytes,bytes+count,nullptr,false):json();
}
void panel(const csmod_ui_t *ui) {
    ui->Text("Jev escolhe a estratégia; os bots mantêm navegação, combate e mira.");
    auto advisor=snapshot("advisor-status",2000);
    if(!advisor.is_object()) {
        ui->Text("Aguardando telemetria do advisor local.");
        return;
    }
    try {
        char line[256];
        double age=std::max(0.0,host->MonotonicTime()-advisor.value("observed_at",host->MonotonicTime()));
        std::snprintf(line,sizeof(line),"Telemetria: %.0f ms",age*1000);
        ui->Text(line);
        for(auto& team:advisor.at("teams")) {
            auto text=team.at("team").get<std::string>()+": "+team.at("intent").get<std::string>()+" · "+team.at("source").get<std::string>();
            ui->Text(text.c_str());
        }
        auto status=snapshot("brain-status",5000);
        if(!status.is_object())return;
        std::snprintf(line,sizeof(line),"Jev: %d chamadas · cache: %d · erros: %d",status.value("model_calls",0),status.value("cache_hits",0),status.value("errors",0));
        ui->Text(line);
        if(status.value("errors",0)) {
            std::snprintf(line,sizeof(line),"Transporte/configuração: %d · respostas inválidas: %d",status.value("transport_errors",0),status.value("invalid_responses",0));
            ui->Text(line);
        }
        std::snprintf(line,sizeof(line),"Última chamada: %d ms · tokens: %lld entrada / %lld saída",status.value("latency_ms",0),status.value("input_tokens",0LL),status.value("output_tokens",0LL));
        ui->Text(line);
    } catch(const json::exception&) {
        ui->Text("Telemetria inválida; aguardando uma atualização válida.");
    }
}
mod_api_t api{};
}
extern "C" __attribute__((visibility("default"))) mod_api_t *ModGetAPI() {
    api.api_version=MOD_API_VERSION;
    api.mod_name="bots-jev";
    api.size=sizeof(api);
    api.Mod_Init=init;
    api.Mod_DebugPanel=panel;
    return &api;
}

#ifndef VR_PATHS_H
#define VR_PATHS_H
#include "csmod_services.h"
void vr_paths_init(const csmod_services_t *services);
const char *vr_config_path(const char *name);
const char *vr_asset_path(const char *legacy_or_relative);
void vr_diagnostic(int severity,const char *text);
#endif

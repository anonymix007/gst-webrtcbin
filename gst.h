#pragma once

#include <string>

typedef void (*offer_cb_t)(const char *, const char *);

namespace gst {

void init(int *, char **[]);
void run();
void create_pipeline();
void start_pipeline();
void set_offer(std::string, const offer_cb_t);
void wait_ice_complete();
} // namespace gst
